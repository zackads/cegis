#include "observer.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << message << '\n' << std::flush;
}

namespace {

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
    out += "return " + join(p.return_labels, ", ");
    return out;
}

} // namespace

void ConsoleObserver::on_synthesis_round(std::size_t example_set_size) {
    log("synthesis round, |S| = " + std::to_string(example_set_size));
}
void ConsoleObserver::on_candidate(const SynthesizedProgram& candidate) {
    log("candidate L: " + render_L(candidate));
}
void ConsoleObserver::on_counterexample(const std::vector<std::string>& input) {
    log("counterexample: x = (" + join(input, ", ") + ")");
}
void ConsoleObserver::on_no_program() { log("no program exists over the library"); }

void ConsoleObserver::log(const std::string& message) {
    logger_.log("[core " + std::to_string(core_) + "] " + title_ + ": " + message);
}
