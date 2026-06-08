#pragma once

#include <ostream>

#include "model.hpp"

// Renders a SynthesizedProgram as readable straight-line text.  Console output
// is a separate responsibility from synthesis, so this lives apart from the
// Synthesizer and depends only on the decoded program, not on any z3 state.
class ProgramPrinter {
public:
    explicit ProgramPrinter(std::ostream& os) : os_(os) {}

    void print(const SynthesizedProgram& program) const;

private:
    std::ostream& os_;
};
