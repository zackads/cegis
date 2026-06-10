#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <z3++.h>

#include "problems.hpp"
#include "program_printer.hpp"
#include "synthesizer.hpp"

// ---------------------------------------------------------------------------
// Core affinity.  The driver runs a fixed pool of worker threads, one per
// requested job, and pins each to a distinct core.  Because there are exactly
// `jobs` workers and each is bound to its own core, the problems running
// concurrently are always on different cores.
//
//   * Linux: pthread_setaffinity_np gives a hard guarantee.
//   * macOS: there is no public API to bind a thread to a specific core; the
//     affinity-tag API is only a scheduling hint, so distinct cores are
//     requested but not guaranteed.  pin_to_core reports which we got.
// ---------------------------------------------------------------------------
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
static bool pin_to_core(unsigned core) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}
static constexpr bool kAffinityIsHard = true;
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
static bool pin_to_core(unsigned core) {
    // A distinct non-zero tag asks the scheduler to keep this thread on a core
    // separate from threads carrying other tags.  It is advisory only.
    thread_affinity_policy_data_t policy = {static_cast<integer_t>(core) + 1};
    thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
    return thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY,
                             reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS;
}
static constexpr bool kAffinityIsHard = false;
#else
static bool pin_to_core(unsigned) { return false; }
static constexpr bool kAffinityIsHard = false;
#endif

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
    };
}

// Serialises all console output so lines from concurrent workers never tear.
class Logger {
public:
    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << message << '\n' << std::flush;
    }

private:
    std::mutex mutex_;
};

std::string join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// A candidate program (the location assignment L) on a single line.
std::string render_L(const SynthesizedProgram& p) {
    std::string out;
    for (const SynthesizedProgram::Instruction& ins : p.instructions) {
        out += ins.result + " = " + ins.component + "(" + join(ins.args, ", ") + "); ";
    }
    out += "return " + p.return_label;
    return out;
}

// Reports CEGIS progress for one problem, tagged with its title and the core
// the worker is pinned to.
class ConsoleObserver : public SynthesisObserver {
public:
    ConsoleObserver(Logger& logger, std::string title, unsigned core)
        : logger_(logger), title_(std::move(title)), core_(core) {}

    void on_synthesis_round(std::size_t example_set_size) override {
        log("synthesis round, |S| = " + std::to_string(example_set_size));
    }
    void on_candidate(const SynthesizedProgram& candidate) override {
        log("candidate L: " + render_L(candidate));
    }
    void on_counterexample(const std::vector<std::string>& input) override {
        log("counterexample: x = (" + join(input, ", ") + ")");
    }
    void on_no_program() override { log("no program exists over the library"); }

private:
    void log(const std::string& message) {
        logger_.log("[core " + std::to_string(core_) + "] " + title_ + ": " + message);
    }

    Logger& logger_;
    std::string title_;
    unsigned core_;
};

unsigned parse_jobs(int argc, char** argv, unsigned hw, bool& help) {
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
        } else {
            std::cerr << "ignoring unknown argument: " << arg << '\n';
        }
    }
    return std::clamp(jobs, 1u, std::max(1u, hw));
}

} // namespace

int main(int argc, char** argv) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());

    bool help = false;
    const unsigned jobs = parse_jobs(argc, argv, hw, help);
    if (help) {
        std::cout << "usage: emofom [-j N | --jobs N]\n"
                  << "  -j, --jobs N   synthesise up to N problems in parallel, each pinned\n"
                  << "                 to its own core (1.." << hw << " on this machine).\n";
        return 0;
    }

    std::vector<Task> tasks = all_tasks();
    Logger logger;
    logger.log("Synthesising " + std::to_string(tasks.size()) + " problems with " +
               std::to_string(jobs) + " parallel job(s) on a machine with " +
               std::to_string(hw) + " cores.");

    std::atomic<std::size_t> next{0};

    // Each worker pins itself to a distinct core, then pulls problems from the
    // shared queue.  With `jobs` pinned workers, no two problems run on the same
    // core at the same time.
    auto worker = [&](unsigned core) {
        const bool pinned = pin_to_core(core);
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= tasks.size()) break;
            const Task& task = tasks[i];

            std::string start = "[core " + std::to_string(core) + "] starting " + task.title;
            if (!pinned)
                start += "  (warning: could not pin thread to core)";
            else if (!kAffinityIsHard)
                start += "  (core affinity is advisory on this OS)";
            logger.log(start);

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
