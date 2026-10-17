#include "citysim/core/rng.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>

using namespace citysim::rng;

// GOLDEN VALUES: changing any of these changes saved games and replays. If a test here fails, the
// RNG changed; do not just update the numbers.

static_assert(stream("a") == 0xaf63dc4c8601ec8cULL);
static_assert(stream("") == 0xcbf29ce484222325ULL);
static_assert(stream("a") != stream("b"));
static_assert(mix64(k_golden_gamma) == 0xe220a8397b1dcdafULL); // first output of SplitMix64 seeded with 0

TEST_CASE("stream hashes names", "[core][rng]") {
    REQUIRE(stream("a") != stream("b"));
    REQUIRE(stream("citizens.marriage") != stream("citizens.birth"));
    REQUIRE(stream("citizens.marriage") == stream("citizens.marriage"));
}

TEST_CASE("mix64 golden values", "[core][rng]") {
    constexpr std::array<std::uint64_t, 5> expected{
        0x5692161d100b05e5ULL, 0xdbd238973a2b148aULL, 0x1e535eede31428f0ULL,
        0xb7a4712c74562914ULL, 0xb6bf613dbebb45dcULL,
    };
    for (std::uint64_t i = 0; i < expected.size(); ++i) {
        REQUIRE(mix64(i + 1) == expected[i]);
    }
}

TEST_CASE("hash64 golden values", "[core][rng]") {
    constexpr std::array<std::uint64_t, 5> expected{
        0xf5a83bced392b72fULL, 0x3e95b18f548e49f7ULL, 0x438f354b6815253dULL,
        0x7bf515b82e907106ULL, 0x7a858eeec19dab7eULL,
    };
    const auto s = stream("test.stream");
    for (std::uint64_t n = 0; n < expected.size(); ++n) {
        REQUIRE(hash64(12345, s, 7, 99, n) == expected[n]);
    }
}

TEST_CASE("hash64 depends on every input", "[core][rng]") {
    const auto base = hash64(1, 2, 3, 4, 5);
    REQUIRE(hash64(2, 2, 3, 4, 5) != base);
    REQUIRE(hash64(1, 3, 3, 4, 5) != base);
    REQUIRE(hash64(1, 2, 4, 4, 5) != base);
    REQUIRE(hash64(1, 2, 3, 5, 5) != base);
    REQUIRE(hash64(1, 2, 3, 4, 6) != base);
    // Shifting one unit between inputs must not collide.
    REQUIRE(hash64(2, 2, 3, 4, 5) != hash64(1, 3, 3, 4, 5));
}

TEST_CASE("RngContext::at matches hash64", "[core][rng]") {
    const RngContext ctx{42, 100};
    REQUIRE(ctx.at(7, 9) == hash64(42, 7, 100, 9, 0));
    REQUIRE(ctx.at(7, 9, 3) == hash64(42, 7, 100, 9, 3));
}

TEST_CASE("Pcg32 matches the published reference sequence", "[core][rng]") {
    // pcg32-demo, pcg32_srandom(42, 54)
    Pcg32 gen;
    gen.seed(42, 54);
    constexpr std::array<std::uint32_t, 6> expected{0xa15c02b7U, 0x7b47f409U, 0xba1d3330U,
                                                    0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
    for (const auto value : expected) {
        REQUIRE(gen.next_u32() == value);
    }
}

TEST_CASE("Pcg32 seeded from hash64 golden values", "[core][rng]") {
    Pcg32 gen(hash64(12345, stream("test.stream"), 7, 99, 0));
    constexpr std::array<std::uint32_t, 5> expected{0x81c7e8b7U, 0xf8b8c08fU, 0xfd60c52aU, 0x5620c8f2U,
                                                    0xe5da4f86U};
    for (const auto value : expected) {
        REQUIRE(gen.next_u32() == value);
    }
}

TEST_CASE("uniform_int golden values", "[core][rng]") {
    Pcg32 gen(777);
    constexpr std::array<std::int64_t, 5> expected{3, 4, 5, 4, 5};
    for (const auto value : expected) {
        REQUIRE(uniform_int(gen, 1, 6) == value);
    }
}

TEST_CASE("uniform_int stays in range and is roughly uniform", "[core][rng]") {
    Pcg32 gen(hash64(1, stream("test.uniform"), 0, 0, 0));
    constexpr int k_buckets = 10;
    constexpr int k_samples = 100000;
    std::array<int, k_buckets> counts{};
    for (int i = 0; i < k_samples; ++i) {
        const auto v = uniform_int(gen, -5, 4);
        REQUIRE(v >= -5);
        REQUIRE(v <= 4);
        ++counts[static_cast<std::size_t>(v + 5)];
    }
    const double expected = static_cast<double>(k_samples) / k_buckets;
    double chi2 = 0.0;
    for (const int c : counts) {
        const double d = static_cast<double>(c) - expected;
        chi2 += d * d / expected;
    }
    // 9 degrees of freedom: the 99.99th percentile is about 33. Loose on purpose; the seed is fixed.
    REQUIRE(chi2 < 40.0);
}

TEST_CASE("uniform_int handles degenerate and extreme ranges", "[core][rng]") {
    Pcg32 gen(5);
    REQUIRE(uniform_int(gen, 7, 7) == 7);
    for (int i = 0; i < 100; ++i) {
        const auto wide = uniform_int(gen, 0, std::int64_t{1} << 40);
        REQUIRE(wide >= 0);
        REQUIRE(wide <= std::int64_t{1} << 40);
    }
    // The full 64-bit range must not hang or overflow.
    (void)uniform_int(gen, INT64_MIN, INT64_MAX);
}

TEST_CASE("float and double in [0,1)", "[core][rng]") {
    Pcg32 gen(9);
    for (int i = 0; i < 10000; ++i) {
        const float f = uniform_float01(gen);
        const double d = uniform_double01(gen);
        REQUIRE(f >= 0.0F);
        REQUIRE(f < 1.0F);
        REQUIRE(d >= 0.0);
        REQUIRE(d < 1.0);
    }
}

TEST_CASE("chance respects probability bounds", "[core][rng]") {
    Pcg32 gen(3);
    int hits = 0;
    for (int i = 0; i < 10000; ++i) {
        REQUIRE_FALSE(chance(gen, 0.0));
        REQUIRE(chance(gen, 1.0));
        hits += chance(gen, 0.25) ? 1 : 0;
    }
    REQUIRE(hits > 2200);
    REQUIRE(hits < 2800);
}

TEST_CASE("weighted_index respects zero weights", "[core][rng]") {
    Pcg32 gen(11);
    const std::array<float, 5> weights{0.0F, 3.0F, 0.0F, 1.0F, 0.0F};
    std::array<int, 5> counts{};
    for (int i = 0; i < 10000; ++i) {
        ++counts[weighted_index(gen, weights)];
    }
    REQUIRE(counts[0] == 0);
    REQUIRE(counts[2] == 0);
    REQUIRE(counts[4] == 0);
    REQUIRE(counts[1] > counts[3] * 2); // about 3:1

    const std::array<float, 3> none{0.0F, 0.0F, -1.0F};
    REQUIRE(weighted_index(gen, none) == none.size());
}

TEST_CASE("normal has roughly the requested mean and deviation", "[core][rng]") {
    Pcg32 gen(21);
    constexpr int k_samples = 50000;
    double sum = 0.0;
    double sum_sq = 0.0;
    for (int i = 0; i < k_samples; ++i) {
        const double x = normal(gen, 10.0, 2.0);
        sum += x;
        sum_sq += x * x;
    }
    const double mean = sum / k_samples;
    const double sd = std::sqrt(sum_sq / k_samples - mean * mean);
    REQUIRE(std::abs(mean - 10.0) < 0.1);
    REQUIRE(std::abs(sd - 2.0) < 0.1);
}

TEST_CASE("counter-based overloads are pure functions of the hash", "[core][rng]") {
    const auto h = hash64(1, stream("test.pure"), 5, 6, 0);
    REQUIRE(uniform_int(h, 0, 1000) == uniform_int(h, 0, 1000));
    REQUIRE(uniform_double01(h) == uniform_double01(h));
    REQUIRE(normal(h, 0.0, 1.0) == normal(h, 0.0, 1.0));
    // The uint64 form is exactly the first draw of a Pcg32 seeded from the same hash.
    Pcg32 gen(h);
    REQUIRE(uniform_int(h, 0, 1000) == uniform_int(gen, 0, 1000));
}
