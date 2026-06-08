# emofom

A small C++ implementation of **component-based program
synthesis**: given a behavioural specification and a library of components, it
searches for a *loop-free* program that wires those components together to meet
the spec. The search is encoded as a sequence of SMT queries and solved with
[Z3](https://github.com/Z3Prover/z3).

The synthesised program is represented entirely by integer "location" (line)
variables that say, for every component port, which earlier line it reads from.
Solving for those line variables *is* synthesis. The included example task
synthesises "turn off the rightmost 1-bit" from a library of
`dec` and `and` components.

## Paper

This code implements the approach described in:

> Sumit Gulwani, Susmit Jha, Ashish Tiwari, Ramarathnam Venkatesan.
> **Synthesis of loop-free programs.**
> *PLDI 2011.* <https://dl.acm.org/doi/10.1145/1993316.1993506>

In particular it follows the constraint encoding (`ψ_wfp`, `φ_lib`, `ψ_conn`,
`φ_spec`) and the counterexample-guided inductive synthesis (CEGIS) loop from
that paper.

## Building and running

### Dependencies

- A C++20 compiler (Clang or GCC)
- [CMake](https://cmake.org/) 4.2 or newer
- [Z3](https://github.com/Z3Prover/z3), including its C++ bindings (`z3++.h`)

Z3 can be supplied in either of two ways:

1. **System install** — if CMake finds a Z3 CONFIG package, it is used
   directly. On macOS, `brew install z3`; on Debian/Ubuntu, `apt install
   libz3-dev`.
2. **Vendored submodule** — otherwise CMake builds the copy of Z3 checked out
   under `third_party/z3`. This is fetched via the git submodule declared in
   [`.gitmodules`](.gitmodules), which is useful for offline or locked-down
   environments (e.g. HPC).

If you intend to use the vendored build, clone with submodules:

```sh
git clone --recurse-submodules <this-repo-url>
# or, in an existing checkout:
git submodule update --init --recursive
```

### Build

```sh
cmake -S . -B build
cmake --build build
```

### Run

```sh
./build/emofom
```

Expected output:

```
turn off rightmost 1-bit:
  <synthesised straight-line program>
```

## Components

| File | Responsibility |
| --- | --- |
| [`main.cpp`](main.cpp) | Entry point. Builds a problem, runs the `Synthesizer`, and prints the result. |
| [`model.hpp`](model.hpp) | Core data model: `Port`, `ComponentSpec`, `ComponentPlacement`, `ProblemInstance`, `Problem`, and the decoded `SynthesizedProgram`. Documents how programs are encoded in line variables. |
| [`synthesizer.hpp`](synthesizer.hpp) / [`synthesizer.cpp`](synthesizer.cpp) | The `Synthesizer`: builds the SMT constraints (`psi_wfp`, `phi_lib`, `psi_conn`, `phi_spec`) and runs the CEGIS loop, alternately finding a candidate program and searching for a counterexample. |
| [`problems.hpp`](problems.hpp) / [`problems.cpp`](problems.cpp) | The library of example synthesis tasks, including `turn_off_rightmost_bit_problem`. |
| [`program_printer.hpp`](program_printer.hpp) / [`program_printer.cpp`](program_printer.cpp) | The `ProgramPrinter`: renders a decoded `SynthesizedProgram` as readable straight-line text. |

All values flowing through synthesised programs are 16-bit bitvectors
(`BV_LENGTH` in [`model.hpp`](model.hpp)).
