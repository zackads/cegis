#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

// Serialises all console output so lines from concurrent workers never tear.
class Logger {
public:
    void log(const std::string& message);

private:
    std::mutex mutex_;
};

// Reports CEGIS progress for one problem, tagged with its title and the core
// the worker is pinned to.
class ConsoleObserver : public SynthesisObserver {
public:
    ConsoleObserver(Logger& logger, std::string title, unsigned core)
        : logger_(logger), title_(std::move(title)), core_(core) {}

    void on_synthesis_round(std::size_t example_set_size) override;
    void on_candidate(const SynthesizedProgram& candidate) override;
    void on_counterexample(const std::vector<std::string>& input) override;
    void on_no_program() override;

private:
    void log(const std::string& message);

    Logger& logger_;
    std::string title_;
    unsigned core_;
};
