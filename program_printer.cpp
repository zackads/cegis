#include "program_printer.hpp"

void ProgramPrinter::print(const SynthesizedProgram& program) const {
    for (const SynthesizedProgram::Instruction& inst : program.instructions) {
        os_ << "  " << inst.result << " = " << inst.component << "(";
        for (unsigned k = 0; k < inst.args.size(); ++k) {
            os_ << inst.args[k];
            if (k + 1 < inst.args.size()) os_ << ", ";
        }
        os_ << ")\n";
    }
    os_ << "  return ";
    for (unsigned j = 0; j < program.return_labels.size(); ++j) {
        os_ << program.return_labels[j];
        if (j + 1 < program.return_labels.size()) os_ << ", ";
    }
    os_ << "\n";
}
