#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <z3++.h>

#include "observer.hpp"
#include "problems.hpp"
#include "program_printer.hpp"
#include "synthesizer.hpp"

// The driver runs a fixed pool of worker threads, one per requested job, and
// lets macOS schedule them.  There is no portable way to bind a thread to a
// specific core (the mach affinity-tag API is only an advisory hint and is
// unsupported on Apple Silicon), so each worker carries a `core` index purely
// as a label for its log lines.

namespace {

// One synthesis task: a human-readable title and the problem builder.
struct Task {
    std::string title;
    std::function<Problem(z3::context&)> build;
};

std::vector<Task> all_tasks() {
    return {
        {"P1  turn off rightmost 1-bit", turn_off_rightmost_bit_problem},
        {"P2  test 2^n - 1", test_power_of_two_minus_one_problem},
        {"P3  isolate rightmost 1-bit", isolate_rightmost_one_bit_problem},
        {"P4  mask rightmost 1-bit and trailing 0s", mask_rightmost_one_and_trailing_zeros_problem},
        {"P5  right-propagate rightmost 1-bit", right_propagate_rightmost_one_problem},
        {"P6  turn on rightmost 0-bit", turn_on_rightmost_zero_bit_problem},
        {"P7  isolate rightmost 0-bit", isolate_rightmost_zero_bit_problem},
        {"P8  mask trailing 0s", mask_trailing_zeros_problem},
        {"P9  absolute value", absolute_value_problem},
        {"P10 nlz(x) == nlz(y)", test_equal_leading_zeros_problem},
        {"P11 nlz(x) < nlz(y)", test_fewer_leading_zeros_problem},
        {"P12 nlz(x) <= nlz(y)", test_no_more_leading_zeros_problem},
        {"P13 sign function", sign_function_problem},
        {"P14 floor average", floor_average_problem},
        {"P15 ceil average", ceil_average_problem},
        {"P16 unsigned max", unsigned_max_problem},
        {"P17 turn off rightmost contiguous 1s", turn_off_rightmost_contiguous_ones_problem},
        {"P18 is power of 2", is_power_of_two_problem},
        {"P19 exchange register fields", exchange_register_fields_problem},
        {"P20 next higher same popcount", next_higher_same_popcount_problem},
        {"P21 cycle three values", cycle_three_values_problem},
        {"P22 compute parity", compute_parity_problem},
        {"P23 count bits", count_bits_problem},
        {"P24 round up to power of 2", round_up_to_power_of_two_problem},
        {"P25 high half of product", high_half_of_product_problem},
        {"P26 SecAnd (first-order masked)", sec_and_problem},
        {"P27 SecOr (first-order masked)", sec_or_problem},
    };
}

unsigned parse_jobs(int argc, char** argv, unsigned hw, bool& help,
                    std::vector<std::string>& filters) {
    unsigned jobs = 1;
    auto to_uint = [](const std::string& s) -> unsigned {
        return static_cast<unsigned>(std::max(1L, std::strtol(s.c_str(), nullptr, 10)));
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            help = true;
        } else if (arg == "-j" || arg == "--jobs") {
            if (i + 1 < argc) jobs = to_uint(argv[++i]);
        } else if (arg.rfind("-j", 0) == 0 && arg.size() > 2) {
            jobs = to_uint(arg.substr(2));
        } else if (arg.rfind("--jobs=", 0) == 0) {
            jobs = to_uint(arg.substr(std::string("--jobs=").size()));
        } else if (!arg.empty() && arg[0] != '-') {
            filters.push_back(arg); // task-title prefix, e.g. "P26"
        } else {
            std::cerr << "ignoring unknown argument: " << arg << '\n';
        }
    }
    return std::clamp(jobs, 1u, std::max(1u, hw));
}

// Keep only the tasks whose title starts with one of the given prefixes, where
// the prefix must end on a word boundary so "P1" matches only the P1 task and
// not P10–P19.
std::vector<Task> filter_tasks(std::vector<Task> tasks, const std::vector<std::string>& filters) {
    if (filters.empty()) return tasks;
    std::erase_if(tasks, [&](const Task& t) {
        return std::ranges::none_of(filters, [&](const std::string& f) {
            return t.title.rfind(f, 0) == 0 &&
                   (t.title.size() == f.size() || t.title[f.size()] == ' ');
        });
    });
    return tasks;
}

} // namespace

int main(int argc, char** argv) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());

    bool help = false;
    std::vector<std::string> filters;
    const unsigned jobs = parse_jobs(argc, argv, hw, help, filters);
    if (help) {
        std::cout << "usage: emofom [-j N | --jobs N] [PROBLEM...]\n"
                  << "  -j, --jobs N   synthesise up to N problems in parallel, each pinned\n"
                  << "                 to its own core (1.." << hw << " on this machine).\n"
                  << "  PROBLEM        only run tasks whose title starts with this prefix,\n"
                  << "                 e.g. \"emofom P26 P27\".\n";
        return 0;
    }

    std::vector<Task> tasks = filter_tasks(all_tasks(), filters);
    if (tasks.empty()) {
        std::cerr << "no tasks match the given filters\n";
        return 1;
    }
    Logger logger;
    logger.log("Synthesising " + std::to_string(tasks.size()) + " problems with " +
               std::to_string(jobs) + " parallel job(s) on a machine with " +
               std::to_string(hw) + " cores.");

    std::atomic<std::size_t> next{0};

    // Each worker pulls problems from the shared queue until it is drained.
    auto worker = [&](unsigned core) {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= tasks.size()) break;
            const Task& task = tasks[i];

            logger.log("[core " + std::to_string(core) + "] starting " + task.title);

            z3::context ctx; // a private context per thread: z3 contexts are independent
            Synthesizer synth(ctx, task.build(ctx));
            ConsoleObserver observer(logger, task.title, core);
            std::optional<SynthesizedProgram> program = synth.solve(&observer);

            std::ostringstream out;
            out << "[core " << core << "] FINISHED " << task.title << ":\n";
            if (program.has_value())
                ProgramPrinter(out).print(program.value());
            else
                out << "  synthesis failed\n";
            logger.log(out.str());
        }
    };

    std::vector<std::thread> pool;
    for (unsigned w = 0; w < jobs; ++w)
        pool.emplace_back(worker, w);
    for (std::thread& t : pool)
        t.join();

    return 0;
}
