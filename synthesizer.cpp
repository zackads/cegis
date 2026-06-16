#include "synthesizer.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

Synthesizer::Locations Synthesizer::make_locations() const {
    z3::expr_vector out_line(ctx_);
    std::vector<z3::expr_vector> in_line;
    for (unsigned n = 0; n < num_components(); ++n) {
        const BitwiseComponentSpec& spec = problem_.library[n];
        out_line.push_back(ctx_.int_const(("line_" + spec.name + std::to_string(n)).c_str()));
        z3::expr_vector args(ctx_);
        for (unsigned k = 0; k < spec.arity; ++k)
            args.push_back(ctx_.int_const(
                ("line_" + spec.name + std::to_string(n) + "_arg" + std::to_string(k)).c_str()));
        in_line.push_back(std::move(args));
    }
    z3::expr_vector output_lines(ctx_);
    for (unsigned j = 0; j < num_outputs(); ++j)
        output_lines.push_back(ctx_.int_const(("line_return" + std::to_string(j)).c_str()));
    return Locations{out_line, std::move(in_line), std::move(output_lines)};
}

// Build a fresh trace of the program for one evaluation, identified by `tag`.
// Value variables are fresh (suffixed with the tag); line variables come from
// `loc_`, so every instance shares the same program.
ProblemInstance Synthesizer::instantiate(const std::string& tag, unsigned width) {
    ProblemInstance inst;

    // Program inputs are definitions pinned to lines 0 .. M-1.
    for (unsigned j = 0; j < num_inputs(); ++j) {
        z3::expr v = ctx_.bv_const(("in" + std::to_string(j) + "_" + tag).c_str(), width);
        inst.inputs.push_back(Port{v, ctx_.int_val(static_cast<int>(j))});
    }

    // Each library component: fresh parameter/result values, shared lines.
    for (unsigned n = 0; n < num_components(); ++n) {
        const BitwiseComponentSpec& spec = problem_.library[n];
        z3::expr_vector params(ctx_);
        std::vector<Port> in;
        for (unsigned k = 0; k < spec.arity; ++k) {
            z3::expr p = ctx_.bv_const(
                ("p" + std::to_string(n) + "_" + std::to_string(k) + "_" + tag).c_str(),
                width);
            in.push_back(Port{p, loc_.in_line[n][k]});
            params.push_back(p);
        }
        z3::expr r = ctx_.bv_const(("r" + std::to_string(n) + "_" + tag).c_str(), width);
        inst.component_placements.push_back(
            BitwiseComponentPlacement{&spec, std::move(in), Port{r, loc_.out_line[n]},
                               spec.semantics(params, r)});
    }

    // Each program output reads from some line.
    for (unsigned j = 0; j < num_outputs(); ++j) {
        z3::expr o = ctx_.bv_const(("out" + std::to_string(j) + "_" + tag).c_str(), width);
        inst.outputs.push_back(Port{o, loc_.output_lines[j]});
    }
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
    for (const z3::expr& out : loc_.output_lines)
        c.push_back(out >= 0 && out < M + N);

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
    for (const BitwiseComponentPlacement& comp : inst.component_placements)
        c.push_back(comp.semantics);
    return z3::mk_and(c);
}


// psi_conn: interconnections, including the mapping of formals (parameters, e.g. a component's inputs) to actuals
// (e.g. values driven onto a components inputs) and from the return variable of some component to the output of the
// program.
z3::expr Synthesizer::psi_conn(const ProblemInstance& inst) const {
    std::vector<const Port*> defs;  // values produced: inputs + component results
    std::vector<const Port*> uses;  // values consumed: parameters + program output
    for (const Port& i : inst.inputs) defs.push_back(&i);
    for (const BitwiseComponentPlacement& comp : inst.component_placements) defs.push_back(&comp.out);
    for (const BitwiseComponentPlacement& comp : inst.component_placements)
        for (const Port& p : comp.in) uses.push_back(&p);
    for (const Port& o : inst.outputs) uses.push_back(&o);

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

// phi_spec: the statement of correctness of the program
z3::expr Synthesizer::phi_spec(const ProblemInstance& inst) const {
    z3::expr_vector in(ctx_);
    for (const Port& i : inst.inputs)
        in.push_back(i.value);
    z3::expr_vector out(ctx_);
    for (const Port& o : inst.outputs)
        out.push_back(o.value);
    return problem_.spec(in, out);
}

// phi_secure: the first-order non-leakage condition from Biryukov et al. (2017).
//
// Construct a single z3 expression that says "no intermediate value computed by
// any component in the program may be statistically correlated with any of the
// sensitive functions."
//
// *** REQUIRES EVERY COMPONENT TO BE BITWISE ***
// A naive approach to encode this constraint would be, for each candidate program,
// to create 2^M Z3 expressions, one for each row in a truth table on M inputs.
// Then, since each component is bitwise, we can assert independence via the Hamming
// weight formula on a single bit.  This approach involves creating 2^M *
//
// Every component is required to be bitwise — the same Boolean function at
// every bit position, at any width — so the program is unfolded *once* at
// width 2^M on truth-table values: input j is pinned to the constant whose
// bit i is bit j of i, which makes each component's output value the truth
// table of the Boolean function it computes (bit i is its value on the input
// assignment encoded by i).
//
// A function f leaks about a sensitive function k iff HW(k&f)/HW(f) != HW(k&~f)/HW(~f).
// We assert the cross-multiplied equality for every (component, k) pair.  Constant
// f satisfies it vacuously.  Hamming weights are sums of single-bit extracts at
// width W, chosen so the products (at most rows^2) cannot overflow, keeping the
// whole condition inside QF_BV.
z3::expr Synthesizer::phi_secure() {
    const unsigned M = num_inputs();
    const unsigned rows = 1u << M; // 2^M rows in a truth table on M inputs
    const unsigned W = 2 * M + 2;  // bit-width for Hamming-weight arithmetic

    // A width-`rows` constant whose bit i is table[i], built 64 bits at a time
    // so tables wider than a machine word still work.
    // Helper function to turn a 
    const auto table_const = [&](const std::vector<bool>& table) {
        std::optional<z3::expr> v;
        for (unsigned lo = 0; lo < rows; lo += 64) {
            const unsigned n = std::min(64u, rows - lo);
            uint64_t chunk = 0;
            for (unsigned i = 0; i < n; ++i)
                if (table[lo + i]) chunk |= uint64_t{1} << i;
            z3::expr e = ctx_.bv_val(chunk, n);
            v = v ? z3::concat(e, *v) : e; // higher chunk in front
        }
        return *v;
    };

    z3::expr_vector c(ctx_);

    // One unfolding of the unknown program over truth-table values.
    ProblemInstance inst = instantiate("s", rows);
    for (unsigned j = 0; j < M; ++j) {
        std::vector<bool> projection(rows);
        for (unsigned i = 0; i < rows; ++i)
            projection[i] = (i >> j) & 1u;
        c.push_back(inst.inputs[j].value == table_const(projection));
    }
    c.push_back(phi_lib(inst));
    c.push_back(psi_conn(inst));

    // Construct the sensitive truth tables we want to avoid
    std::vector<std::vector<bool>> k_tables;
    for (const auto& k : problem_.sensitive_fns) {
        std::vector<bool> table(rows);
        for (unsigned i = 0; i < rows; ++i) {
            // Unpack the truth table row i into its input bits
            std::vector<bool> bits(M);
            for (unsigned j = 0; j < M; ++j)
                bits[j] = (i >> j) & 1u;
            table[i] = k(bits);
        }
        k_tables.push_back(std::move(table));
    }

    // HW of f restricted to the rows where `where` is true, as a width-W value.
    const auto hw = [&](const z3::expr& f, const std::vector<bool>& where) {
        z3::expr sum = ctx_.bv_val(0, W);
        for (unsigned i = 0; i < rows; ++i)
            if (where[i])
                sum = sum + z3::zext(f.extract(i, i), W - 1);
        return sum;
    };
    const std::vector<bool> everywhere(rows, true);

    for (unsigned n = 0; n < num_components(); ++n) {
        const z3::expr& f = inst.component_placements[n].out.value;
        const z3::expr hw_f = hw(f, everywhere);

        for (const std::vector<bool>& k : k_tables) {
            unsigned hw_k = 0;
            for (unsigned i = 0; i < rows; ++i)
                if (k[i]) ++hw_k;
            const z3::expr hw_kf = hw(f, k);
            // HW(k&f) * HW(~f) == HW(k&~f) * HW(f)
            c.push_back(hw_kf * (ctx_.bv_val(rows, W) - hw_f) ==
                        (ctx_.bv_val(hw_k, W) - hw_kf) * hw_f);
        }
    }
    return z3::mk_and(c);
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

    std::optional<z3::expr> secure;
    if (!problem_.sensitive_fns.empty())
        secure = phi_secure();

    while (true) {
        if (observer) observer->on_synthesis_round(examples.size());

        // === Finite synthesis (the exists): find a single program that meets the spec on every example gathered so far. ===
        z3::solver synth_solver(ctx_);
        synth_solver.add(psi_wfp());
        if (secure)
            synth_solver.add(*secure);
        for (unsigned s = 0; s < examples.size(); ++s) {
            ProblemInstance synth_example_instance = instantiate("e" + std::to_string(s)); // Create an instance of the program...
            for (unsigned j = 0; j < synth_example_instance.inputs.size(); ++j)
                synth_solver.add(synth_example_instance.inputs[j].value == examples[s][j]); // ...and wire in the values from this example.
            synth_solver.add(phi_lib(synth_example_instance));
            synth_solver.add(psi_conn(synth_example_instance));
            synth_solver.add(phi_spec(synth_example_instance));
        }
        if (synth_solver.check() != z3::sat) {
            if (observer) observer->on_no_program();
            return std::nullopt;
        }
        z3::model candidate = synth_solver.get_model();
        if (observer) observer->on_candidate(decode(candidate));

        // === Verification (the forall): does some input make this candidate violate the spec?  Look for a counterexample. ===
        z3::solver verif_solver(ctx_);
        ProblemInstance verif_instance = instantiate("v");
        for (unsigned n = 0; n < num_components(); ++n) { // pin lines to the candidate program
            verif_solver.add(loc_.out_line[n] == candidate.eval(loc_.out_line[n], true));
            for (z3::expr && k : loc_.in_line[n])
                verif_solver.add(k == candidate.eval(k, true));
        }
        for (z3::expr && out : loc_.output_lines)
            verif_solver.add(out == candidate.eval(out, true));
        verif_solver.add(phi_lib(verif_instance));
        verif_solver.add(psi_conn(verif_instance));
        verif_solver.add(!phi_spec(verif_instance));

        if (verif_solver.check() == z3::sat) {
            z3::model cex = verif_solver.get_model();
            z3::expr_vector input(ctx_);
            for (const Port& i : verif_instance.inputs)
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
    for (z3::expr && out : loc_.output_lines)
        program.return_labels.push_back(line_label(line_of(out)));
    return program;
}
