#include "citysim/save/csav_file.hpp"

#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"

#include <algorithm>
#include <stdexcept>
#include <xxhash.h>
#include <zstd.h>

namespace citysim::save {

namespace {

constexpr char k_magic[4] = {'C', 'S', 'A', 'V'};
constexpr int k_compression_level = 3;
constexpr std::size_t k_footer_bytes = sizeof(std::uint64_t);

void write_header(BinaryWriter& out, const SaveHeader& header) {
    out.raw(k_magic, sizeof k_magic);
    out.fixed(header.container_version);
    out.string(header.game_version);
    out.fixed(header.world_seed);
    out.fixed(header.tick);
    out.fixed(header.content_hash);
    out.varint(header.mods.size());
    for (const ModStamp& mod : header.mods) {
        out.string(mod.id);
        out.string(mod.version);
    }
    out.fixed(header.saved_at_unix_seconds);
}

SaveHeader read_header(BinaryReader& in) {
    char magic[4] = {};
    in.raw(magic, sizeof magic);
    if (!std::equal(std::begin(magic), std::end(magic), std::begin(k_magic))) {
        in.fail("not a CSAV file (wrong magic)");
    }
    SaveHeader header;
    in.fixed(header.container_version);
    if (header.container_version == 0 || header.container_version > k_csav_container_version) {
        in.fail("save container version " + std::to_string(header.container_version) +
                " is not supported (this build reads up to " + std::to_string(k_csav_container_version) +
                ")");
    }
    in.string(header.game_version);
    in.fixed(header.world_seed);
    in.fixed(header.tick);
    in.fixed(header.content_hash);
    std::uint64_t mod_count = 0;
    in.size(mod_count);
    header.mods.resize(static_cast<std::size_t>(mod_count));
    for (ModStamp& mod : header.mods) {
        in.string(mod.id);
        in.string(mod.version);
    }
    in.fixed(header.saved_at_unix_seconds);
    return header;
}

// Checks the footer and returns the bytes before it.
std::span<const std::byte> verified_body(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof k_magic + k_footer_bytes) {
        throw SaveFormatError("file is too short to be a save", bytes.size());
    }
    const std::span<const std::byte> body = bytes.first(bytes.size() - k_footer_bytes);
    BinaryReader footer(bytes.last(k_footer_bytes), nullptr, nullptr, body.size());
    std::uint64_t stored = 0;
    footer.fixed(stored);
    // Say "not a save" before "damaged" when the file is something else entirely.
    if (!std::equal(std::begin(k_magic), std::end(k_magic), reinterpret_cast<const char*>(bytes.data()))) {
        throw SaveFormatError("not a CSAV file (wrong magic)", 0);
    }
    if (XXH3_64bits(body.data(), body.size()) != stored) {
        throw SaveFormatError("the save is damaged (checksum mismatch)", body.size());
    }
    return body;
}

} // namespace

std::vector<std::byte> write_csav(const CsavContents& contents) {
    BinaryWriter out;
    write_header(out, contents.header);
    out.fixed(static_cast<std::uint32_t>(contents.sections.size()));
    for (const RawSection& section : contents.sections) {
        if (section.tag.size() != 4) {
            throw std::invalid_argument("section tags are exactly 4 characters: '" + section.tag + "'");
        }
        if (section.data.size() > k_max_section_bytes) {
            throw std::invalid_argument("section '" + section.tag + "' is too large");
        }
        std::vector<std::byte> compressed(ZSTD_compressBound(section.data.size()));
        const std::size_t size = ZSTD_compress(compressed.data(), compressed.size(), section.data.data(),
                                               section.data.size(), k_compression_level);
        if (ZSTD_isError(size) != 0U) {
            throw std::runtime_error(std::string("compression failed: ") + ZSTD_getErrorName(size));
        }
        out.raw(section.tag.data(), 4);
        out.fixed(section.version);
        out.fixed(static_cast<std::uint32_t>(size));
        out.fixed(static_cast<std::uint32_t>(section.data.size()));
        out.raw(compressed.data(), size);
    }
    const auto written = out.bytes();
    const std::uint64_t checksum = XXH3_64bits(written.data(), written.size());
    out.fixed(checksum);
    return out.take();
}

CsavContents read_csav(std::span<const std::byte> bytes) {
    BinaryReader in(verified_body(bytes));
    CsavContents contents;
    contents.header = read_header(in);

    std::uint32_t section_count = 0;
    in.fixed(section_count);
    if (section_count > in.remaining()) {
        in.fail("section count " + std::to_string(section_count) + " is larger than the data that is left");
    }
    for (std::uint32_t i = 0; i < section_count; ++i) {
        RawSection section;
        section.tag.resize(4);
        in.raw(section.tag.data(), 4);
        in.fixed(section.version);
        std::uint32_t compressed_length = 0;
        std::uint32_t raw_length = 0;
        in.fixed(compressed_length);
        in.fixed(raw_length);
        if (raw_length > k_max_section_bytes) {
            in.fail("section '" + section.tag + "' claims " + std::to_string(raw_length) + " bytes");
        }
        if (compressed_length > in.remaining()) {
            in.fail("section '" + section.tag + "' is longer than the data that is left");
        }
        std::vector<std::byte> compressed(compressed_length);
        in.raw(compressed.data(), compressed.size());
        section.data.resize(raw_length);
        const std::size_t size =
            ZSTD_decompress(section.data.data(), section.data.size(), compressed.data(), compressed.size());
        if (ZSTD_isError(size) != 0U) {
            in.fail("section '" + section.tag + "' cannot be decompressed: " + ZSTD_getErrorName(size));
        }
        if (size != raw_length) {
            in.fail("section '" + section.tag + "' decompressed to " + std::to_string(size) +
                    " bytes, expected " + std::to_string(raw_length));
        }
        contents.sections.push_back(std::move(section));
    }
    if (!in.at_end()) {
        in.fail("unexpected data after the last section");
    }
    return contents;
}

SaveHeader read_csav_header(std::span<const std::byte> bytes) {
    BinaryReader in(verified_body(bytes));
    return read_header(in);
}

} // namespace citysim::save
