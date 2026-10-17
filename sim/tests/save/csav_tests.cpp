#include "citysim/save/csav_file.hpp"
#include "citysim/save/save_format_error.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace citysim::save;

namespace {

std::vector<std::byte> bytes_from(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

CsavContents sample_contents() {
    CsavContents contents;
    contents.header.game_version = "1.2.3";
    contents.header.world_seed = 0xDEADBEEFCAFEF00DULL;
    contents.header.tick = 123456789;
    contents.header.content_hash = 0x0123456789ABCDEFULL;
    contents.header.mods = {{"base", "1.0.0"}, {"example_mod", "0.1.0"}};
    contents.header.saved_at_unix_seconds = -5; // metadata only; any value must survive
    contents.sections.push_back({"AAAA", 1, bytes_from("small")});
    contents.sections.push_back({"EMPT", 3, {}});
    contents.sections.push_back({"BIGG", 2, bytes_from(std::string(100000, 'x'))});
    return contents;
}

} // namespace

TEST_CASE("a container round-trips its header and sections", "[save][csav]") {
    const CsavContents original = sample_contents();
    const std::vector<std::byte> bytes = write_csav(original);
    const CsavContents loaded = read_csav(bytes);

    CHECK(loaded.header.container_version == k_csav_container_version);
    CHECK(loaded.header.game_version == "1.2.3");
    CHECK(loaded.header.world_seed == original.header.world_seed);
    CHECK(loaded.header.tick == original.header.tick);
    CHECK(loaded.header.content_hash == original.header.content_hash);
    CHECK(loaded.header.mods == original.header.mods);
    CHECK(loaded.header.saved_at_unix_seconds == -5);

    REQUIRE(loaded.sections.size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(loaded.sections[i].tag == original.sections[i].tag);
        CHECK(loaded.sections[i].version == original.sections[i].version);
        CHECK(loaded.sections[i].data == original.sections[i].data);
    }
}

TEST_CASE("sections are compressed", "[save][csav]") {
    const std::vector<std::byte> bytes = write_csav(sample_contents());
    CHECK(bytes.size() < 5000); // 100000 repeated bytes shrink to almost nothing
}

TEST_CASE("the header can be read alone", "[save][csav]") {
    const std::vector<std::byte> bytes = write_csav(sample_contents());
    const SaveHeader header = read_csav_header(bytes);
    CHECK(header.tick == 123456789);
    CHECK(header.mods.size() == 2);
}

TEST_CASE("the same contents always give the same bytes", "[save][csav]") {
    CHECK(write_csav(sample_contents()) == write_csav(sample_contents()));
}

TEST_CASE("a wrong magic is reported as not a save", "[save][csav]") {
    std::vector<std::byte> bytes = write_csav(sample_contents());
    bytes[0] = std::byte{'X'};
    try {
        read_csav(bytes);
        FAIL("expected an error");
    } catch (const SaveFormatError& error) {
        CHECK(std::string(error.what()).find("magic") != std::string::npos);
    }
    CHECK_THROWS_AS(read_csav_header(bytes), SaveFormatError);
    CHECK_THROWS_AS(read_csav({}), SaveFormatError);
}

TEST_CASE("any damaged byte is detected", "[save][csav]") {
    const std::vector<std::byte> bytes = write_csav(sample_contents());
    // Every byte of the header and footer, then a spread through the body.
    for (std::size_t i = 0; i < bytes.size(); i += (i < 200 || i + 40 > bytes.size()) ? 1U : 97U) {
        std::vector<std::byte> damaged = bytes;
        damaged[i] = static_cast<std::byte>(~std::to_integer<unsigned>(damaged[i]));
        CHECK_THROWS_AS(read_csav(damaged), SaveFormatError);
    }
}

TEST_CASE("every truncation is detected", "[save][csav]") {
    const std::vector<std::byte> bytes = write_csav(sample_contents());
    for (std::size_t length = 0; length < bytes.size(); length += (length < 200 ? 1U : 53U)) {
        CHECK_THROWS_AS(read_csav(std::span<const std::byte>(bytes.data(), length)), SaveFormatError);
    }
}

TEST_CASE("a newer container version is refused", "[save][csav]") {
    CsavContents contents = sample_contents();
    contents.header.container_version = static_cast<std::uint16_t>(k_csav_container_version + 1);
    CHECK_THROWS_AS(read_csav(write_csav(contents)), SaveFormatError);
}

TEST_CASE("section tags must be four characters", "[save][csav]") {
    CsavContents contents;
    contents.sections.push_back({"TOOLONG", 1, {}});
    CHECK_THROWS_AS(write_csav(contents), std::invalid_argument);
}
