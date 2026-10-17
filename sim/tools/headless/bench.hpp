#pragma once

#include "headless/cli_options.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace citysim::headless {

struct TimingSummary {
    double avg = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double max = 0.0;
};

// Average, median, 95th percentile (nearest rank) and maximum of `milliseconds`. All zero for an empty list.
TimingSummary summarize(std::vector<double> milliseconds);

// Runs the walking skeleton as fast as it will go, with no pacing, and prints how long each tick took: for
// the whole run, and for its second half (after the agents have all been given their first path, which is the
// expensive start). Wall-clock timing is fine here: this is a tool, not simulation code. Returns the exit
// code.
int run_bench(const BenchSpec& spec, std::uint64_t seed, std::ostream& out);

} // namespace citysim::headless
