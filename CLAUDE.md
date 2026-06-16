# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A C++20 implementation of component-based program synthesis from Gulwani et al., "Synthesis of loop-free programs" (PLDI 2011, the "Brahma" approach). Given a behavioural spec and a library of components, it uses Z3 and a CEGIS loop to find a loop-free program wiring those components together. Problems P1–P25 are the bitvector tricks from Hacker's Delight; P26–P27 synthesise the first-order masked SecAnd/SecOr gadgets of Biryukov et al., "Optimal First-Order Boolean Masking for Embedded IoT Devices" (CARDIS 2017), where no intermediate value may leak the secrets behind the input shares.

## Build and run

Requires a C++20 compiler, CMake ≥ 4.2, and Z3 with C++ bindings. CMake prefers a system Z3 (`brew install z3` on macOS); if none is found it builds the vendored submodule under `third_party/z3` (`git submodule update --init --recursive`).

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew   # prefix path needed for Homebrew Z3 on macOS
cmake --build build
./build/emofom -j 8        # synthesise all problems, N parallel jobs (one core each)
./build/emofom P26 P27     # only tasks whose title starts with a given prefix
```

There is no test suite; verification is running the binary and checking the synthesised programs. Some problems (e.g. P20, P24) take a long time. For a quick syntax check of a single file: `c++ -std=c++20 -I/opt/homebrew/include -fsyntax-only problems.cpp`.

`cmake-build-debug/` is CLion's build directory; ignore it and use `build/`.

## Architecture

The key idea (documented at the top of `model.hpp`): the program being synthesised is encoded entirely in integer **location (line) variables** — for every component port, which earlier line it reads from. Solving for those line variables *is* synthesis. Line variables are shared across all evaluations; value variables are fresh per evaluation (per counterexample).

- `model.hpp` — core data model: `Port` (a value + the line it binds to), `ComponentSpec` (name, arity, semantics as a Z3 relation), `ComponentPlacement`, `ProblemInstance` (one unfolding of the unknown program for a single evaluation), `Problem` (inputs + outputs + library + spec, plus an optional `sensitive` set), and the Z3-free decoded `SynthesizedProgram`. All values are bitvectors of width `BV_LENGTH` (32). Programs may have multiple outputs (`Problem::num_outputs`); the spec relates the input vector to the output vector.
- `synthesizer.{hpp,cpp}` — the `Synthesizer` builds the paper's constraints (`psi_wfp` well-formed layout, `phi_lib` component semantics, `psi_conn` values agree where lines agree, `phi_spec` behavioural goal) and runs the CEGIS loop in `solve()`: finite synthesis over the example set, then verification searching for a counterexample, repeating until verification fails (program correct) or finite synthesis is UNSAT (no program exists). If `Problem::sensitive` is non-empty, finite synthesis also gets `phi_secure`, the CARDIS-2017 first-order non-leakage condition: the program is unfolded once at width 2^M on truth-table values (input j pinned to the projection constant whose bit i is bit j of i), so each component's output value *is* the truth table of the Boolean function it computes, and must be statistically independent of each sensitive function (a Hamming-weight cross-multiplication per pair, in pure bitvector arithmetic). This is sound only for libraries of bitwise components (the same Boolean function at every bit position, at any width), and it is a static constraint — built once outside the CEGIS loop; security needs no CEGIS, only functional correctness does. Progress is reported through the `SynthesisObserver` interface; callbacks fire on the calling thread, so a parallel driver's observer must be thread-safe.
- `problems.{hpp,cpp}` — the problem builders (P1–P27). Anonymous-namespace helpers (`un`, `bin`, `lit`, `as_bit`, `out1`, …) build `ComponentSpec`s and adapt single-output specs; constants (shift amounts, masks, ±1) are folded into components so the synthesiser only wires lines and never invents literals. Predicates are modelled as components returning full-width 0/1.
- `observer.{hpp,cpp}` — the `SynthesisObserver` interface the `Synthesizer` reports progress through, plus the driver's concrete `ConsoleObserver` and the `Logger` that serialises console output so lines from concurrent workers never tear.
- `main.cpp` — CLI driver: a fixed pool of worker threads (`-j N`), each pinned to its own core (hard guarantee on Linux, advisory on macOS), pulling problems from a shared queue. Each worker gets a private `z3::context` — Z3 contexts are not shared across threads, and a per-task `ConsoleObserver` reports its progress.
- `program_printer.{hpp,cpp}` — renders a decoded `SynthesizedProgram` as straight-line text.

To add a new problem: declare the builder in `problems.hpp`, define it in `problems.cpp` (component library + spec relation), and register it in `all_tasks()` in `main.cpp`.
