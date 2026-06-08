#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <z3++.h>

// ---------------------------------------------------------------------------
// Data model for component-based program synthesis
// (Gulwani/Jha/Seshia/Saraswat, "Brahma").
//
// We synthesise a *loop-free* program that wires together a fixed library of
// components so that it meets a behavioural specification.  The program being
// searched for is encoded entirely in integer "location" (line) variables: for
// every port they say which earlier line that port reads from.  Solving for
// those line variables *is* synthesis.
//
// Every value flowing through the program is a bitvector of width BV_LENGTH.
// ---------------------------------------------------------------------------

constexpr unsigned BV_LENGTH = 16;

// A value travelling on a wire, together with the program line that the wire
// binds to.
//   * For an output port (a program output, or a component's result), `line` is
//     the line on which the value is defined.
//   * For an input port (a component parameter, or the program output), `line`
//     is the line the port reads from.
// `value` is fresh for every evaluation (it differs per counterexample); `line`
// is shared across all evaluations, because it is part of the program we are
// synthesising rather than part of any single run.
struct Port {
    z3::expr value;
    z3::expr line;
};

// Immutable description of a library component: its arity, plus a builder for
// its semantic relation given concrete parameter/result values.
struct ComponentSpec {
    std::string name;
    unsigned arity;
    std::function<z3::expr(const z3::expr_vector& params, const z3::expr& result)> semantics;
};

// A library component placed somewhere in the program: a `ComponentSpec`
// instantiated with fresh per-evaluation values.  `out.line` is the
// component's location (the line it occupies); each `in[k].line` says where
// that parameter reads from.  `semantics` is `spec`'s relation evaluated on
// this placement's input/output values (e.g. out == in0 - 1).
struct ComponentPlacement {
    const ComponentSpec* spec;
    std::vector<Port> in;
    Port out;
    z3::expr semantics;
};

// One concrete unfolding of the (still unknown) program for a single
// evaluation: fresh value variables everywhere, but carrying the shared
// location variables inside each port.
struct ProblemInstance {
    std::vector<Port> inputs;    // program inputs  (defined on lines 0..M-1)
    std::vector<ComponentPlacement> component_placements; // the placed library components
    std::optional<Port> output;  // program output  (reads some line)
};

// The whole synthesis task: how many inputs the program takes, the component
// library, and the behavioural specification relating program inputs to the
// program output.
struct Problem {
    unsigned num_inputs;
    std::vector<ComponentSpec> library;
    std::function<z3::expr(const z3::expr_vector& inputs, const z3::expr& output)> spec;
};

// A solved program, decoded out of the synthesis model into plain values: an
// ordered list of straight-line instructions plus the line the result reads
// from.  Carries no z3 state, so it can be consumed independently of how it was
// produced (e.g. printed).
struct SynthesizedProgram {
    struct Instruction {
        std::string result;            // line label being defined, e.g. "v1"
        std::string component;         // component name, e.g. "dec"
        std::vector<std::string> args; // line labels each parameter reads from
    };
    std::vector<Instruction> instructions; // in order of the lines they occupy
    std::string return_label;              // line label the program returns
};
