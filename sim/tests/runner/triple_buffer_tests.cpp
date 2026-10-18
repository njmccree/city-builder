#include "citysim/runner/triple_buffer.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <thread>

using citysim::runner::TripleBuffer;

namespace {
struct Payload {
    std::uint64_t version = 0;
    std::array<std::uint64_t, 16> copies{}; // all equal to `version` unless a read was torn
};
} // namespace

TEST_CASE("TripleBuffer: reading before any publish gives the default value", "[runner][buffer]") {
    TripleBuffer<int> buffer;
    REQUIRE(buffer.read() == 0);
}

TEST_CASE("TripleBuffer: the consumer sees the newest published value", "[runner][buffer]") {
    TripleBuffer<int> buffer;
    buffer.back() = 1;
    buffer.publish();
    REQUIRE(buffer.read() == 1);
    REQUIRE(buffer.read() == 1); // nothing new: same value again

    buffer.back() = 2;
    buffer.publish();
    buffer.back() = 3;
    buffer.publish();
    buffer.back() = 4;
    buffer.publish(); // the consumer skips 2 and 3
    REQUIRE(buffer.read() == 4);
}

TEST_CASE("TripleBuffer: a value being read is not disturbed by later publishes", "[runner][buffer]") {
    TripleBuffer<int> buffer;
    buffer.back() = 10;
    buffer.publish();
    const int& held = buffer.read();
    REQUIRE(held == 10);

    for (int i = 11; i < 20; ++i) {
        buffer.back() = i;
        buffer.publish();
    }
    REQUIRE(held == 10); // the producer never writes the consumer's slot
    REQUIRE(buffer.read() == 19);
}

TEST_CASE("TripleBuffer: concurrent producer and consumer never see a torn or older value",
          "[runner][buffer][threads]") {
    TripleBuffer<Payload> buffer;
    constexpr std::uint64_t k_publishes = 200000;
    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (std::uint64_t v = 1; v <= k_publishes; ++v) {
            Payload& slot = buffer.back();
            slot.version = v;
            for (auto& copy : slot.copies) {
                copy = v;
            }
            buffer.publish();
        }
        done.store(true);
    });

    std::uint64_t last_seen = 0;
    std::uint64_t reads = 0;
    bool torn = false;
    bool went_backwards = false;
    while (!done.load() || last_seen < k_publishes) {
        const Payload& value = buffer.read();
        for (const auto copy : value.copies) {
            torn = torn || copy != value.version;
        }
        went_backwards = went_backwards || value.version < last_seen;
        last_seen = value.version;
        ++reads;
        if (done.load() && last_seen == k_publishes) {
            break;
        }
    }
    producer.join();

    REQUIRE_FALSE(torn);
    REQUIRE_FALSE(went_backwards);
    REQUIRE(last_seen == k_publishes);
    REQUIRE(reads > 0);
}
