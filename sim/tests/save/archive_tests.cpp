#include "citysim/save/archive.hpp"
#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace citysim::save;

namespace {

enum class Color : std::uint8_t { Red = 1, Green = 2 };
enum class Signed : std::int16_t { Low = -5, High = 5 };

struct Inner {
    std::int32_t a = 0;
    std::string b;
    bool operator==(const Inner&) const = default;
};

template <class Ar> void serialize(Ar& ar, Inner& inner, std::uint32_t version) {
    io(ar, inner.a, version);
    io(ar, inner.b, version);
}

using Choice = std::variant<std::monostate, std::int64_t, std::string, Inner>;

struct Composite {
    std::vector<Inner> inners;
    std::array<std::uint16_t, 3> small{};
    std::optional<std::string> maybe;
    std::optional<std::int32_t> none;
    Choice choice;
    Color color = Color::Red;
    Signed sign = Signed::Low;
    double ratio = 0.0;
    std::uint32_t added_in_v2 = 7; // keeps this default for version-1 data
    bool operator==(const Composite&) const = default;
};

template <class Ar> void serialize(Ar& ar, Composite& c, std::uint32_t version) {
    io(ar, c.inners, version);
    io(ar, c.small, version);
    io(ar, c.maybe, version);
    io(ar, c.none, version);
    io(ar, c.choice, version);
    io(ar, c.color, version);
    io(ar, c.sign, version);
    io(ar, c.ratio, version);
    if (version >= 2) {
        io(ar, c.added_in_v2, version);
    }
}

Composite sample() {
    Composite c;
    c.inners = {{1, "one"}, {-2, ""}, {3, "three \xC3\xA9"}};
    c.small = {1, 65535, 3};
    c.maybe = "present";
    c.choice = Inner{9, "nested"};
    c.color = Color::Green;
    c.sign = Signed::High;
    c.ratio = 0.1;
    c.added_in_v2 = 42;
    return c;
}

std::vector<std::byte> bytes_of(const BinaryWriter& writer) {
    const auto span = writer.bytes();
    return {span.begin(), span.end()};
}

} // namespace

TEST_CASE("fixed-width integers are little-endian and round-trip at the extremes", "[save][archive]") {
    BinaryWriter writer;
    writer.fixed(std::uint32_t{0x01020304});
    const auto bytes = bytes_of(writer);
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[0] == std::byte{0x04});
    CHECK(bytes[3] == std::byte{0x01});

    BinaryWriter all;
    all.fixed(std::numeric_limits<std::int8_t>::min());
    all.fixed(std::numeric_limits<std::uint16_t>::max());
    all.fixed(std::numeric_limits<std::int32_t>::min());
    all.fixed(std::numeric_limits<std::int64_t>::min());
    all.fixed(std::numeric_limits<std::uint64_t>::max());
    BinaryReader reader(all.bytes());
    std::int8_t i8 = 0;
    std::uint16_t u16 = 0;
    std::int32_t i32 = 0;
    std::int64_t i64 = 0;
    std::uint64_t u64 = 0;
    reader.fixed(i8);
    reader.fixed(u16);
    reader.fixed(i32);
    reader.fixed(i64);
    reader.fixed(u64);
    CHECK(i8 == std::numeric_limits<std::int8_t>::min());
    CHECK(u16 == 65535);
    CHECK(i32 == std::numeric_limits<std::int32_t>::min());
    CHECK(i64 == std::numeric_limits<std::int64_t>::min());
    CHECK(u64 == std::numeric_limits<std::uint64_t>::max());
    CHECK(reader.at_end());
}

TEST_CASE("varints use LEB128 and zigzag", "[save][archive]") {
    BinaryWriter writer;
    writer.varint(300);
    writer.svarint(-1);
    writer.svarint(1);
    const auto bytes = bytes_of(writer);
    REQUIRE(bytes.size() == 4);
    CHECK(bytes[0] == std::byte{0xAC});
    CHECK(bytes[1] == std::byte{0x02});
    CHECK(bytes[2] == std::byte{0x01}); // zigzag(-1)
    CHECK(bytes[3] == std::byte{0x02}); // zigzag(1)

    for (const std::uint64_t value : {std::uint64_t{0}, std::uint64_t{127}, std::uint64_t{128},
                                      std::uint64_t{1} << 35, std::numeric_limits<std::uint64_t>::max()}) {
        BinaryWriter w;
        w.varint(value);
        BinaryReader r(w.bytes());
        std::uint64_t back = 0;
        r.varint(back);
        CHECK(back == value);
        CHECK(r.at_end());
    }
    for (const std::int64_t value :
         {std::int64_t{0}, std::int64_t{-1}, std::int64_t{63}, std::int64_t{-64},
          std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        BinaryWriter w;
        w.svarint(value);
        BinaryReader r(w.bytes());
        std::int64_t back = 0;
        r.svarint(back);
        CHECK(back == value);
    }
}

TEST_CASE("floats round-trip as bit patterns", "[save][archive]") {
    BinaryWriter writer;
    const float values[] = {0.0F, -0.0F, 1.5F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()};
    for (const float v : values) {
        writer.f32(v);
    }
    writer.f64(-0.0);
    writer.f64(0.1);
    BinaryReader reader(writer.bytes());
    for (const float v : values) {
        float back = 1.0F;
        reader.f32(back);
        CHECK(std::memcmp(&back, &v, sizeof v) == 0); // identical bits, including -0 and NaN
    }
    double negative_zero = 1.0;
    double tenth = 0.0;
    reader.f64(negative_zero);
    reader.f64(tenth);
    CHECK(std::signbit(negative_zero));
    CHECK(tenth == 0.1);
}

TEST_CASE("strings are length-prefixed UTF-8", "[save][archive]") {
    BinaryWriter writer;
    const std::string text = "caf\xC3\xA9 \xE2\x9C\x93";
    writer.string(text);
    writer.string("");
    BinaryReader reader(writer.bytes());
    std::string a;
    std::string b = "not empty";
    reader.string(a);
    reader.string(b);
    CHECK(a == text);
    CHECK(b.empty());
}

TEST_CASE("containers, optionals, variants, enums and nested types round-trip", "[save][archive]") {
    Composite original = sample();
    BinaryWriter writer;
    io(writer, original, 2);

    Composite loaded;
    BinaryReader reader(writer.bytes());
    io(reader, loaded, 2);
    CHECK(loaded == original);
    CHECK(reader.at_end());

    // Every variant alternative.
    for (const Choice& choice :
         {Choice{}, Choice{std::int64_t{-9}}, Choice{std::string("s")}, Choice{Inner{1, "x"}}}) {
        Choice source = choice;
        BinaryWriter w;
        io(w, source);
        Choice back;
        BinaryReader r(w.bytes());
        io(r, back);
        CHECK(back == choice);
    }
}

TEST_CASE("older versions load with defaults for newer fields", "[save][archive]") {
    Composite original = sample();
    BinaryWriter writer;
    io(writer, original, 1); // a version-1 save has no added_in_v2

    Composite loaded;
    BinaryReader reader(writer.bytes());
    io(reader, loaded, 1);
    CHECK(reader.at_end());
    CHECK(loaded.added_in_v2 == 7); // the default
    loaded.added_in_v2 = original.added_in_v2;
    CHECK(loaded == original);
}

TEST_CASE("every truncation of a save is a SaveFormatError, never a crash", "[save][archive]") {
    Composite original = sample();
    BinaryWriter writer;
    io(writer, original, 2);
    const auto bytes = bytes_of(writer);
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        Composite loaded;
        BinaryReader reader(std::span<const std::byte>(bytes.data(), length));
        CHECK_THROWS_AS(io(reader, loaded, 2), SaveFormatError);
    }
}

TEST_CASE("errors name the byte offset", "[save][archive]") {
    BinaryWriter writer;
    writer.fixed(std::uint16_t{1});
    BinaryReader reader(writer.bytes());
    std::uint16_t value = 0;
    reader.fixed(value);
    std::uint32_t more = 0;
    try {
        reader.fixed(more);
        FAIL("expected an error");
    } catch (const SaveFormatError& error) {
        CHECK(error.offset() == 2);
        CHECK(std::string(error.what()).find("at byte 2") != std::string::npos);
    }
}

TEST_CASE("malformed values are rejected", "[save][archive]") {
    SECTION("a flag that is neither 0 nor 1") {
        const std::byte data[] = {std::byte{2}};
        BinaryReader reader(data);
        bool flag = false;
        CHECK_THROWS_AS(reader.flag(flag), SaveFormatError);
    }
    SECTION("a varint that never ends") {
        const std::byte data[11] = {std::byte{0x80}, std::byte{0x80}, std::byte{0x80}, std::byte{0x80},
                                    std::byte{0x80}, std::byte{0x80}, std::byte{0x80}, std::byte{0x80},
                                    std::byte{0x80}, std::byte{0x80}, std::byte{0x01}};
        BinaryReader reader(data);
        std::uint64_t value = 0;
        CHECK_THROWS_AS(reader.varint(value), SaveFormatError);
    }
    SECTION("a varint too big for 64 bits") {
        const std::byte data[10] = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                    std::byte{0xFF}, std::byte{0x7F}};
        BinaryReader reader(data);
        std::uint64_t value = 0;
        CHECK_THROWS_AS(reader.varint(value), SaveFormatError);
    }
    SECTION("a collection claiming more elements than there are bytes") {
        BinaryWriter writer;
        writer.varint(1'000'000'000);
        BinaryReader reader(writer.bytes());
        std::vector<std::uint32_t> values;
        CHECK_THROWS_AS(io(reader, values), SaveFormatError);
    }
    SECTION("a string longer than the data") {
        BinaryWriter writer;
        writer.varint(50);
        writer.raw("abc", 3);
        BinaryReader reader(writer.bytes());
        std::string text;
        CHECK_THROWS_AS(reader.string(text), SaveFormatError);
    }
    SECTION("a variant alternative that does not exist") {
        BinaryWriter writer;
        writer.varint(9);
        BinaryReader reader(writer.bytes());
        Choice choice;
        CHECK_THROWS_AS(io(reader, choice), SaveFormatError);
    }
    SECTION("a block longer than the data") {
        BinaryWriter writer;
        writer.varint(10);
        BinaryReader reader(writer.bytes());
        CHECK_THROWS_AS(reader.block(), SaveFormatError);
    }
}

TEST_CASE("a block is bounded and can be skipped", "[save][archive]") {
    BinaryWriter writer;
    BinaryWriter inner = writer.child();
    inner.fixed(std::uint32_t{7});
    writer.block(inner);
    writer.fixed(std::uint8_t{99});

    BinaryReader reader(writer.bytes());
    BinaryReader block = reader.block();
    std::uint32_t value = 0;
    block.fixed(value);
    CHECK(value == 7);
    CHECK(block.at_end());
    std::uint8_t after = 0;
    reader.fixed(after);
    CHECK(after == 99);

    std::uint8_t too_much = 0;
    CHECK_THROWS_AS(block.fixed(too_much), SaveFormatError); // cannot read past its block
}
