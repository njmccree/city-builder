#include "citysim/world/raster.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

using namespace citysim::world;
using citysim::test::TempDir;

namespace {

// A raster with a simple pattern that every element type can hold exactly.
template <class T> Raster<T> make_pattern(std::uint32_t width, std::uint32_t height) {
    Raster<T> raster(width, height, 0.25F, -10.0F, 7.0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            raster.at(x, y) = static_cast<T>((x * 7U + y * 13U) % 100U);
        }
    }
    return raster;
}

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

} // namespace

TEMPLATE_TEST_CASE("a raster round-trips through a .csr file", "[world][raster]", std::uint8_t, std::uint16_t,
                   std::int16_t, float) {
    const TempDir dir("raster");
    const Raster<TestType> original = make_pattern<TestType>(37, 23); // not a power of two on purpose
    write_csr(dir / "layer.csr", original);

    const Raster<TestType> back = read_csr<TestType>(dir / "layer.csr");
    CHECK(back == original);
    CHECK(back.width() == 37);
    CHECK(back.height() == 23);
    CHECK(back.scale() == 0.25F);
    CHECK(back.offset() == -10.0F);
    REQUIRE(back.nodata().has_value());
    CHECK(*back.nodata() == 7.0);

    const RasterHeader header = read_csr_header(dir / "layer.csr");
    CHECK(header == original.header());
    CHECK(header.type == k_raster_type_of<TestType>);
}

TEST_CASE("cells decode through scale and offset, and no-data decodes to NaN", "[world][raster]") {
    Raster<std::int16_t> elevation(3, 2, 0.25F, 0.0F, -32768.0);
    elevation.at(0, 0) = 400;    // 100 m
    elevation.at(1, 0) = -8;     // -2 m
    elevation.at(2, 0) = -32768; // no data
    CHECK(elevation.value(0, 0) == 100.0F);
    CHECK(elevation.value(1, 0) == -2.0F);
    CHECK(std::isnan(elevation.value(2, 0)));
    CHECK(elevation.is_nodata(2, 0));
    CHECK_FALSE(elevation.is_nodata(0, 0));

    Raster<float> plain(2, 1); // no no-data value: every cell is data, even NaN-looking ones
    plain.at(0, 0) = 3.5F;
    CHECK(plain.value(0, 0) == 3.5F);
    CHECK_FALSE(plain.is_nodata(1, 0));

    Raster<float> with_nan(2, 1, 1.0F, 0.0F, std::numeric_limits<double>::quiet_NaN());
    with_nan.at(0, 0) = 1.0F;
    with_nan.at(1, 0) = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(with_nan.is_nodata(0, 0));
    CHECK(with_nan.is_nodata(1, 0)); // a NaN no-data value matches NaN cells
}

TEST_CASE("row 0 is the south edge", "[world][raster]") {
    Raster<std::uint8_t> raster(4, 3);
    raster.at(1, 2) = 9; // x = 1, y = 2 (the northern row)
    CHECK(raster.data()[2 * 4 + 1] == 9);
}

TEST_CASE("writing is deterministic and the bytes are the documented layout", "[world][raster]") {
    const Raster<std::uint16_t> raster = make_pattern<std::uint16_t>(16, 16);
    const std::vector<std::byte> a = encode_csr(raster);
    const std::vector<std::byte> b = encode_csr(raster);
    CHECK(a == b);

    REQUIRE(a.size() > 56);
    CHECK(a[0] == std::byte{'C'});
    CHECK(a[1] == std::byte{'S'});
    CHECK(a[2] == std::byte{'R'});
    CHECK(a[3] == std::byte{'A'});
    CHECK(a[4] == std::byte{1}); // version 1, little-endian
    CHECK(a[5] == std::byte{0});
    CHECK(a[6] == std::byte{16});  // width
    CHECK(a[10] == std::byte{16}); // height
    CHECK(a[14] == std::byte{2});  // u16
    CHECK(a[15] == std::byte{1});  // has no-data
}

TEST_CASE("a corrupt, truncated or foreign file is refused with a reason", "[world][raster]") {
    const Raster<float> raster = make_pattern<float>(20, 20);
    const std::vector<std::byte> good = encode_csr(raster);
    REQUIRE_NOTHROW(decode_csr<float>(good, "good"));

    SECTION("a flipped payload byte breaks the checksum") {
        std::vector<std::byte> bad = good;
        bad[60] = static_cast<std::byte>(static_cast<unsigned>(bad[60]) ^ 0xFFU);
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "layer.csr"),
                            Catch::Matchers::ContainsSubstring("layer.csr") &&
                                Catch::Matchers::ContainsSubstring("checksum"));
    }
    SECTION("a flipped checksum byte is caught too") {
        std::vector<std::byte> bad = good;
        bad.back() = static_cast<std::byte>(static_cast<unsigned>(bad.back()) ^ 0x01U);
        REQUIRE_THROWS_AS(decode_csr<float>(bad, "x"), MapFormatError);
    }
    SECTION("a truncated file") {
        std::vector<std::byte> bad(good.begin(), good.end() - 20);
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "x"), Catch::Matchers::ContainsSubstring("truncated"));
    }
    SECTION("a file shorter than the header") {
        std::vector<std::byte> bad(good.begin(), good.begin() + 10);
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "x"), Catch::Matchers::ContainsSubstring("shorter"));
    }
    SECTION("extra bytes at the end") {
        std::vector<std::byte> bad = good;
        bad.push_back(std::byte{0});
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "x"), Catch::Matchers::ContainsSubstring("too long"));
    }
    SECTION("the wrong magic") {
        std::vector<std::byte> bad = good;
        bad[0] = std::byte{'X'};
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "x"), Catch::Matchers::ContainsSubstring("CSRA"));
    }
    SECTION("a newer format version") {
        std::vector<std::byte> bad = good;
        bad[4] = std::byte{2};
        REQUIRE_THROWS_WITH(decode_csr<float>(bad, "x"), Catch::Matchers::ContainsSubstring("version 2"));
    }
    SECTION("reading it as another element type") {
        REQUIRE_THROWS_WITH(decode_csr<std::uint8_t>(good, "x"),
                            Catch::Matchers::ContainsSubstring("f32") &&
                                Catch::Matchers::ContainsSubstring("u8"));
    }
    SECTION("a missing file") {
        const TempDir dir("raster_missing");
        REQUIRE_THROWS_WITH(read_csr<float>(dir / "nope.csr"),
                            Catch::Matchers::ContainsSubstring("cannot open"));
        REQUIRE_THROWS_AS(read_csr_header(dir / "nope.csr"), MapFormatError);
    }
}

TEST_CASE("a file corrupted on disk is caught when read", "[world][raster]") {
    const TempDir dir("raster_disk");
    write_csr(dir / "a.csr", make_pattern<std::uint8_t>(30, 30));
    std::vector<std::byte> bytes = read_bytes(dir / "a.csr");
    bytes[bytes.size() / 2] = static_cast<std::byte>(static_cast<unsigned>(bytes[bytes.size() / 2]) ^ 0x55U);
    {
        std::ofstream out(dir / "a.csr", std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    CHECK_THROWS_AS(read_csr<std::uint8_t>(dir / "a.csr"), MapFormatError);
}

TEST_CASE("empty and mismatched rasters are refused", "[world][raster]") {
    CHECK_THROWS_AS(encode_csr(Raster<float>(0, 5)), std::invalid_argument);

    // A header that claims a payload size that does not fit its dimensions (made by editing a good file).
    std::vector<std::byte> bytes = encode_csr(make_pattern<std::uint8_t>(8, 8));
    bytes[6] = std::byte{9}; // width 9 but the payload is 8 x 8
    CHECK_THROWS_WITH(decode_csr<std::uint8_t>(bytes, "x"),
                      Catch::Matchers::ContainsSubstring("does not match"));
}

// The files in fixtures/maps/ were written by the Python pipeline
// (tools/terrain/scripts/make_cpp_fixture.py). The patterns below are the ones that script writes; change
// both together, and only with a format version bump.
TEST_CASE("a .csr written by the Python pipeline reads in C++", "[world][raster]") {
    const std::filesystem::path dir = std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "maps";

    const RasterHeader header = read_csr_header(dir / "python_i16.csr");
    CHECK(header.width == 20);
    CHECK(header.height == 11);
    CHECK(header.type == RasterType::I16);
    CHECK(header.scale == 0.25F);
    CHECK(header.offset == -5.0F);
    REQUIRE(header.nodata.has_value());
    CHECK(*header.nodata == -32768.0);

    const Raster<std::int16_t> i16 = read_csr<std::int16_t>(dir / "python_i16.csr"); // checksum verified too
    for (std::uint32_t y = 0; y < 11; ++y) {
        for (std::uint32_t x = 0; x < 20; ++x) {
            const bool missing = (x == 0 && y == 0) || (x == 19 && y == 10);
            if (missing) {
                CHECK(i16.is_nodata(x, y));
                CHECK(std::isnan(i16.value(x, y)));
            } else {
                const int raw = static_cast<int>((x * 7 + y * 13) % 200) - 100;
                REQUIRE(i16.at(x, y) == raw);
                REQUIRE(i16.value(x, y) == static_cast<float>(raw) * 0.25F - 5.0F);
            }
        }
    }

    const Raster<float> f32 = read_csr<float>(dir / "python_f32.csr");
    REQUIRE(f32.width() == 7);
    REQUIRE(f32.height() == 5);
    for (std::uint32_t y = 0; y < 5; ++y) {
        for (std::uint32_t x = 0; x < 7; ++x) {
            if (x == 3 && y == 2) {
                CHECK(f32.is_nodata(x, y)); // NaN is the no-data value
            } else {
                REQUIRE(f32.at(x, y) == static_cast<float>(x) * 0.5F - static_cast<float>(y));
            }
        }
    }
}
