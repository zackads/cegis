#include "synthesizer.hpp"

#include <algorithm>
#include <string>
#include <vector>

Synthesizer::Locations Synthesizer::make_locations() const {
    z3::expr_vector out_line(ctx_);
    std::vector<z3::expr_vector> in_line;
    for (unsigned n = 0; n < num_components(); ++n) {
        const ComponentSpec& spec = problem_.library[n];
        out_line.push_back(ctx_.int_const(("line_" + spec.name + std::to_string(n)).c_str()));
        z3::expr_vector args(ctx_);
        for (unsigned k = 0; k < spec.arity; ++k)
            args.push_back(ctx_.int_const(
                ("line_" + spec.name + std::to_string(n) + "_arg" + std::to_string(k)).c_str()));
        in_line.push_back(std::move(args));
    }
    return Locations{out_line, std::move(in_line), ctx_.int_const("line_return")};
}

// Build a fresh trace of the program for one evaluation, identified by `tag`.
// Value variables are fresh (suffixed with the tag); line variables come from
// `loc_`, so every instance shares the same — as yet unknown — program.
ProblemInstance Synthesizer::instantiate(const std::string& tag) {
    ProblemInstance inst;

    // Program inputs are definitions pinned to lines 0 .. M-1.
    for (unsigned j = 0; j < num_inputs(); ++j) {
        z3::expr v = ctx_.bv_const(("in" + std::to_string(j) + "_" + tag).c_str(), BV_LENGTH);
        inst.inputs.push_back(Port{v, ctx_.int_val(static_cast<int>(j))});
    }

    // Each library component: fresh parameter/result values, shared lines.
    for (unsigned n = 0; n < num_components(); ++n) {
        const ComponentSpec& spec = problem_.library[n];
        z3::expr_vector params(ctx_);
        std::vector<Port> in;
        for (unsigned k = 0; k < spec.arity; ++k) {
            z3::expr p = ctx_.bv_const(
                ("p" + std::to_string(n) + "_" + std::to_string(k) + "_" + tag).c_str(),
                BV_LENGTH);
            in.push_back(Port{p, loc_.in_line[n][k]});
            params.push_back(p);
        }
        z3::expr r = ctx_.bv_const(("r" + std::to_string(n) + "_" + tag).c_str(), BV_LENGTH);
        inst.component_placements.push_back(
            ComponentPlacement{&spec, std::move(in), Port{r, loc_.out_line[n]},
                               spec.semantics(params, r)});
    }

    // The program output reads from some line.
    z3::expr o = ctx_.bv_const(("out_" + tag).c_str(), BV_LENGTH);
    inst.output = Port{o, loc_.output_line};
    return inst;
}

// psi_wfp: the location variables describe a valid straight-line program.
z3::expr Synthesizer::psi_wfp() const {
    const int M = static_cast<int>(num_inputs());
    const int N = static_cast<int>(num_components());
    z3::expr_vector c(ctx_);

    // Ranges: components occupy lines M .. M+N-1; reads land anywhere in 0 .. M+N-1.
    for (unsigned n = 0; n < num_components(); ++n) {
        c.push_back(loc_.out_line[n] >= M && loc_.out_line[n] < M + N);
        for (unsigned k = 0; k < loc_.in_line[n].size(); ++k)
            c.push_back(loc_.in_line[n][k] >= 0 && loc_.in_line[n][k] < M + N);
    }
    c.push_back(loc_.output_line >= 0 && loc_.output_line < M + N);

    // Consistency: no two components share a line.
    for (unsigned i = 0; i < num_components(); ++i)
        for (unsigned j = i + 1; j < num_components(); ++j)
            c.push_back(loc_.out_line[i] != loc_.out_line[j]);

    // Acyclicity: a parameter may only read a strictly earlier line.
    for (unsigned n = 0; n < num_components(); ++n)
        for (unsigned k = 0; k < loc_.in_line[n].size(); ++k)
            c.push_back(loc_.in_line[n][k] < loc_.out_line[n]);

    return z3::mk_and(c);
}

// phi_lib: every placed component computes its operation.
z3::expr Synthesizer::phi_lib(const ProblemInstance& inst) const {
    z3::expr_vector c(ctx_);
    for (const ComponentPlacement& comp : inst.component_placements)
        c.push_back(comp.semantics);
    return z3::mk_and(c);
}

z3::expr Synthesizer::psi_conn(const ProblemInstance& inst) const {
    std::vector<const Port*> defs;  // values produced: inputs + component results
    std::vector<const Port*> uses;  // values consumed: parameters + program output
    for (const Port& i : inst.inputs) defs.push_back(&i);
    for (const ComponentPlacement& comp : inst.component_placements) defs.push_back(&comp.out);
    for (const ComponentPlacement& comp : inst.component_placements)
        for (const Port& p : comp.in) uses.push_back(&p);
    uses.push_back(&*inst.output);

    z3::expr_vector c(ctx_);
    for (const Port* u : uses) {
        z3::expr_vector reaches(ctx_); // the use must bind to one of these definitions
        for (const Port* d : defs) {
            c.push_back(z3::implies(u->line == d->line, u->value == d->value));
            reaches.push_back(u->line == d->line);
        }
        c.push_back(z3::mk_or(reaches));
    }
    return z3::mk_and(c);
}

z3::expr Synthesizer::phi_spec(const ProblemInstance& inst) const {
    z3::expr_vector in(ctx_);
    for (const Port& i : inst.inputs)
        in.push_back(i.value);
    return problem_.spec(in, inst.output->value);
}

// A concrete starting example (all zero)
z3::expr_vector Synthesizer::all_zero_example() const {
    z3::expr_vector in(ctx_);
    for (unsigned j = 0; j < num_inputs(); ++j)
        in.push_back(ctx_.bv_val(0, BV_LENGTH));
    return in;
}

std::optional<SynthesizedProgram> Synthesizer::solve(SynthesisObserver* observer) {
    std::vector<z3::expr_vector> examples;
    examples.push_back(all_zero_example());

    while (true) {
        if (observer) observer->on_synthesis_round(examples.size());

        // === Finite synthesis (the exists): find a single program that meets the spec on every example gathered so far. ===
        z3::solver finite_synthesis(ctx_);
        finite_synthesis.add(psi_wfp());
        for (unsigned s = 0; s < examples.size(); ++s) {
            ProblemInstance inst = instantiate("e" + std::to_string(s));
            for (unsigned j = 0; j < inst.inputs.size(); ++j)
                finite_synthesis.add(inst.inputs[j].value == examples[s][j]);
            finite_synthesis.add(phi_lib(inst));
            finite_synthesis.add(psi_conn(inst));
            finite_synthesis.add(phi_spec(inst));
        }
        if (finite_synthesis.check() != z3::sat) {
            if (observer) observer->on_no_program();
            return std::nullopt;
        }
        z3::model candidate = finite_synthesis.get_model();
        if (observer) observer->on_candidate(decode(candidate));

        // === Verification (the forall): does some input make this candidate violate the spec?  Look for a counterexample. ===
        z3::solver verification(ctx_);
        ProblemInstance probe = instantiate("v");
        for (unsigned n = 0; n < num_components(); ++n) { // pin lines to the candidate program
            verification.add(loc_.out_line[n] == candidate.eval(loc_.out_line[n], true));
            for (z3::expr && k : loc_.in_line[n])
                verification.add(k == candidate.eval(k, true));
        }
        verification.add(loc_.output_line == candidate.eval(loc_.output_line, true));
        verification.add(phi_lib(probe));
        verification.add(psi_conn(probe));
        verification.add(!phi_spec(probe));

        if (verification.check() == z3::sat) {
            z3::model cex = verification.get_model();
            z3::expr_vector input(ctx_);
            for (const Port& i : probe.inputs)
                input.push_back(cex.eval(i.value, true));
            if (observer) {
                std::vector<std::string> shown;
                for (const z3::expr& v : input)
                    shown.push_back(v.to_string());
                observer->on_counterexample(shown);
            }
            examples.push_back(input); // refine and retry
        } else {
            return decode(candidate); // no counterexample: the program is correct
        }
    }
}

std::string Synthesizer::line_label(int line) const {
    if (line < static_cast<int>(num_inputs())) return "in" + std::to_string(line);
    return "v" + std::to_string(line);
}

SynthesizedProgram Synthesizer::decode(const z3::model& m) const {
    auto line_of = [&](const z3::expr& e) { return m.eval(e, true).get_numeral_int(); };

    std::vector<unsigned> order(num_components());
    for (unsigned n = 0; n < num_components(); ++n) order[n] = n;
    std::ranges::sort(order,
                      [&](const unsigned a, const unsigned b) {
                          return line_of(loc_.out_line[a]) < line_of(loc_.out_line[b]);
                      });

    SynthesizedProgram program;
    for (const unsigned n : order) {
        SynthesizedProgram::Instruction inst;
        inst.result = line_label(line_of(loc_.out_line[n]));
        inst.component = problem_.library[n].name;
        for (unsigned k = 0; k < loc_.in_line[n].size(); ++k)
            inst.args.push_back(line_label(line_of(loc_.in_line[n][k])));
        program.instructions.push_back(std::move(inst));
    }
    program.return_label = line_label(line_of(loc_.output_line));
    return program;
}
