#include <functional>
#include <iostream>
#include <string>
#include <z3++.h>

#include "problems.hpp"
#include "program_printer.hpp"
#include "synthesizer.hpp"

void run(const std::string& title, const std::function<Problem(z3::context&)>& build) {
    z3::context ctx;
    Synthesizer synth(ctx, build(ctx));
    std::cout << title << ":\n";

    std::optional<SynthesizedProgram> p = synth.solve();

    if (p.has_value()) {
        ProgramPrinter(std::cout).print(p.value());
    } else {
        std::cout << "  synthesis failed\n";
    }
    std::cout << "\n";
}

int main() {
    run("turn off rightmost 1-bit", turn_off_rightmost_bit_problem);
    return 0;
}
