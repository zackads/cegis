#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <z3++.h>

#include "model.hpp"

// Receives progress events from a CEGIS solve so a caller can report what the
// synthesiser is doing live, without the Synthesizer knowing anything about how
// (or whether) that progress is displayed.  All callbacks are invoked on the
// thread that called solve(); a parallel driver must make its implementation
// thread-safe.
struct SynthesisObserver {
    virtual ~SynthesisObserver() = default;

    // A finite-synthesis round is beginning against an example set of size S.
    virtual void on_synthesis_round(std::size_t example_set_size) {}
    // The finite-synthesis step found a candidate: an assignment to the location
    // variables L, decoded here into the straight-line program it represents.
    virtual void on_candidate(const SynthesizedProgram& candidate) {}
    // Verification refuted the candidate with this input, now added to the set.
    virtual void on_counterexample(const std::vector<std::string>& input) {}
    // Finite synthesis was unsatisfiable: no program over the library exists.
    virtual void on_no_program() {}
};

// Counterexample-guided inductive synthesis (CEGIS) over the component library:
// alternately find a candidate program that fits the examples gathered so far,
// then look for an input on which it violates the spec, adding any such
// counterexample to the example set and retrying.
class Synthesizer {
public:
    Synthesizer(z3::context& ctx, Problem problem)
        : ctx_(ctx), problem_(std::move(problem)), loc_(make_locations()) {}

    // Run CEGIS to completion.  If `observer` is non-null it receives progress
    // events as each round, candidate and counterexample is produced.
    std::optional<SynthesizedProgram> solve(SynthesisObserver* observer = nullptr);

private:
    // The shared location variables: collectively, this is the program we solve
    // for.  They are created once and reused by every Instance.
    struct Locations {
        z3::expr_vector out_line;             // [N]          where each component is placed
        std::vector<z3::expr_vector> in_line; // [N][arity_n] where each parameter reads from
        z3::expr output_line;                 //              where the program output reads from
    };

    Locations make_locations() const;
    ProblemInstance instantiate(const std::string& tag);

    // Constraints
    z3::expr psi_wfp() const;                                 // a valid loop-free layout
    z3::expr phi_lib(const ProblemInstance&) const;           // correct semantics for each component
    z3::expr psi_conn(const ProblemInstance&) const;          // values agree wherever lines agree
    z3::expr phi_spec(const ProblemInstance&) const;          // the behavioural goal

    z3::expr_vector all_zero_example() const;
    std::string line_label(int line) const;

    z3::sort bv() const { return ctx_.bv_sort(BV_LENGTH); }
    unsigned num_inputs() const { return problem_.num_inputs; }
    unsigned num_components() const { return static_cast<unsigned>(problem_.library.size()); }

    SynthesizedProgram decode(const z3::model&) const;

    z3::context& ctx_;
    Problem problem_;
    Locations loc_;
};
