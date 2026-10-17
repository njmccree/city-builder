#include "citysim/core/rng.hpp"

#include <cmath>

namespace citysim::rng {

namespace {

constexpr double k_two_pi = 6.283185307179586476925286766559;

std::uint64_t uniform_below_u32(Pcg32& gen, std::uint32_t range) {
    // Lemire's nearly-divisionless method. range > 0.
    std::uint64_t m = static_cast<std::uint64_t>(gen.next_u32()) * range;
    auto low = static_cast<std::uint32_t>(m);
    if (low < range) {
        const std::uint32_t threshold = (0U - range) % range;
        while (low < threshold) {
            m = static_cast<std::uint64_t>(gen.next_u32()) * range;
            low = static_cast<std::uint32_t>(m);
        }
    }
    return m >> 32U;
}

std::uint64_t uniform_below_u64(Pcg32& gen, std::uint64_t range) {
    // Rejection of the biased low part, then modulo. range > 2^32.
    const std::uint64_t threshold = (0ULL - range) % range;
    std::uint64_t x = gen.next_u64();
    while (x < threshold) {
        x = gen.next_u64();
    }
    return x % range;
}

} // namespace

std::int64_t uniform_int(Pcg32& gen, std::int64_t lo, std::int64_t hi) {
    // Work in unsigned space so hi - lo cannot overflow.
    const std::uint64_t span = static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo);
    std::uint64_t offset = 0;
    if (span == UINT64_MAX) {
        offset = gen.next_u64(); // the whole 64-bit range
    } else if (span < UINT32_MAX) {
        offset = uniform_below_u32(gen, static_cast<std::uint32_t>(span + 1));
    } else {
        offset = uniform_below_u64(gen, span + 1);
    }
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(lo) + offset);
}

float uniform_float01(Pcg32& gen) {
    return static_cast<float>(gen.next_u32() >> 8U) * (1.0F / 16777216.0F); // 2^-24
}

double uniform_double01(Pcg32& gen) {
    return static_cast<double>(gen.next_u64() >> 11U) * (1.0 / 9007199254740992.0); // 2^-53
}

bool chance(Pcg32& gen, double p) {
    return uniform_double01(gen) < p;
}

std::size_t weighted_index(Pcg32& gen, std::span<const float> weights) {
    double total = 0.0;
    std::size_t last_positive = weights.size();
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0.0F) {
            total += static_cast<double>(weights[i]);
            last_positive = i;
        }
    }
    if (last_positive == weights.size()) {
        return weights.size();
    }

    const double target = uniform_double01(gen) * total;
    double cumulative = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0.0F) {
            cumulative += static_cast<double>(weights[i]);
            if (target < cumulative) {
                return i;
            }
        }
    }
    return last_positive; // rounding left target >= the final cumulative sum
}

double normal(Pcg32& gen, double mean, double sd) {
    const double u1 = 1.0 - uniform_double01(gen); // (0, 1], so log() is finite
    const double u2 = uniform_double01(gen);
    return mean + sd * std::sqrt(-2.0 * std::log(u1)) * std::cos(k_two_pi * u2);
}

// Counter-based forms: seed a fresh generator from the hash.

std::int64_t uniform_int(std::uint64_t hashed, std::int64_t lo, std::int64_t hi) {
    Pcg32 gen(hashed);
    return uniform_int(gen, lo, hi);
}

float uniform_float01(std::uint64_t hashed) {
    Pcg32 gen(hashed);
    return uniform_float01(gen);
}

double uniform_double01(std::uint64_t hashed) {
    Pcg32 gen(hashed);
    return uniform_double01(gen);
}

bool chance(std::uint64_t hashed, double p) {
    Pcg32 gen(hashed);
    return chance(gen, p);
}

std::size_t weighted_index(std::uint64_t hashed, std::span<const float> weights) {
    Pcg32 gen(hashed);
    return weighted_index(gen, weights);
}

double normal(std::uint64_t hashed, double mean, double sd) {
    Pcg32 gen(hashed);
    return normal(gen, mean, sd);
}

} // namespace citysim::rng
