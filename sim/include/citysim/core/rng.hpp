#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Deterministic randomness (architecture §4 rule 1, §5.4). This is the only sanctioned source of
// randomness in sim/. We write our own distributions because <random>'s output differs between
// standard libraries.
//
// Clock: none. Every result is a pure function of its inputs (tick and seed are passed in).
//
// COMPATIBILITY: the formulas below (mix64, hash64, stream, Pcg32 and each distribution's consumption
// of random bits) decide what saved games and replays contain. Changing any of them breaks saves and
// the golden values in sim/tests/core/rng_tests.cpp. Never change them silently.
namespace citysim::rng {

inline constexpr std::uint64_t k_golden_gamma = 0x9E3779B97F4A7C15ULL;

// SplitMix64 finalizer.
constexpr std::uint64_t mix64(std::uint64_t z) {
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

// Counter-based hash: depends only on its inputs, so it is safe inside parallel_for (use the entity
// id as `key`). Chained mixing, with every input mixed on its own before it is folded in:
//     h = k_golden_gamma
//     h = mix64(h ^ mix64(seed))
//     h = mix64(h ^ mix64(stream))
//     h = mix64(h ^ mix64(tick))
//     h = mix64(h ^ mix64(key))
//     h = mix64(h ^ mix64(n))
// Mixing each input first stops (seed + 1, stream) from colliding with (seed, stream + 1).
constexpr std::uint64_t hash64(std::uint64_t seed, std::uint64_t stream, std::uint64_t tick,
                               std::uint64_t key, std::uint64_t n) {
    std::uint64_t h = k_golden_gamma;
    h = mix64(h ^ mix64(seed));
    h = mix64(h ^ mix64(stream));
    h = mix64(h ^ mix64(tick));
    h = mix64(h ^ mix64(key));
    h = mix64(h ^ mix64(n));
    return h;
}

// Stream id for a system: FNV-1a 64 of its name, e.g. stream("citizens.marriage").
constexpr std::uint64_t stream(std::string_view name) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : name) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x100000001B3ULL;
    }
    return h;
}

// PCG-XSH-RR 64/32, for single-threaded code that needs a sequence.
class Pcg32 {
  public:
    Pcg32() { seed(0, 0); }
    // Seeds from a hash64 result: the state is the value, the stream selector is its mix.
    explicit Pcg32(std::uint64_t hashed) { seed(hashed, mix64(hashed)); }

    // Reference seeding routine (pcg32_srandom_r).
    constexpr void seed(std::uint64_t state, std::uint64_t inc) {
        state_ = 0;
        inc_ = (inc << 1U) | 1U;
        next_u32();
        state_ += state;
        next_u32();
    }

    constexpr std::uint32_t next_u32() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        const auto rot = static_cast<std::uint32_t>(old >> 59U);
        return (xorshifted >> rot) | (xorshifted << ((0U - rot) & 31U));
    }

    // High word first, so the result is the same on every platform.
    constexpr std::uint64_t next_u64() {
        const std::uint64_t hi = next_u32();
        const std::uint64_t lo = next_u32();
        return (hi << 32U) | lo;
    }

  private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
};

// Distributions. Each has a Pcg32& form (consumes from the sequence) and a uint64 form that seeds a
// fresh Pcg32 from a hash64 result, for counter-based use: uniform_int(ctx.at(s, key), 1, 6).

// Uniform in [lo, hi], unbiased (Lemire's method when the range fits 32 bits, rejection otherwise).
// Requires lo <= hi.
std::int64_t uniform_int(Pcg32& gen, std::int64_t lo, std::int64_t hi);
std::int64_t uniform_int(std::uint64_t hashed, std::int64_t lo, std::int64_t hi);

// Uniform in [0, 1): top 24 bits of a u32 (float) or top 53 bits of a u64 (double).
float uniform_float01(Pcg32& gen);
float uniform_float01(std::uint64_t hashed);
double uniform_double01(Pcg32& gen);
double uniform_double01(std::uint64_t hashed);

// True with probability p. chance(0) is never true and chance(1) is always true.
bool chance(Pcg32& gen, double p);
bool chance(std::uint64_t hashed, double p);

// Index chosen with probability proportional to its weight. Non-positive weights are never chosen.
// Returns weights.size() when no weight is positive.
std::size_t weighted_index(Pcg32& gen, std::span<const float> weights);
std::size_t weighted_index(std::uint64_t hashed, std::span<const float> weights);

// Box-Muller, using std::log, std::sqrt and std::cos. Consumes two doubles per call.
double normal(Pcg32& gen, double mean, double sd);
double normal(std::uint64_t hashed, double mean, double sd);

// A world seed plus the current tick: the usual input to counter-based randomness.
struct RngContext {
    std::uint64_t world_seed = 0;
    std::uint64_t tick = 0;

    constexpr std::uint64_t at(std::uint64_t stream_id, std::uint64_t key, std::uint64_t n = 0) const {
        return hash64(world_seed, stream_id, tick, key, n);
    }
};

} // namespace citysim::rng
