#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace citysim::save {

// The .csav container (architecture §10), independent of what the sections hold.
//
//   header   "CSAV", u16 container version, game version string, u64 world seed, u64 tick,
//            u64 content hash, mod list (count, then id and version strings), i64 save time (Unix seconds;
//            metadata only, never read by the simulation)
//   body     u32 section count, then each section:
//              4-character tag, u16 section version, u32 compressed length, u32 raw length, zstd bytes
//   footer   u64 XXH3 of every byte before it, so a damaged file is detected before anything is trusted
//
// The header is not compressed, so a load menu can show it without decompressing the sections. Not tied to
// a clock.

constexpr std::uint16_t k_csav_container_version = 1;

struct ModStamp {
    std::string id;
    std::string version;
    bool operator==(const ModStamp&) const = default;
};

struct SaveHeader {
    std::uint16_t container_version = k_csav_container_version;
    std::string game_version;
    std::uint64_t world_seed = 0;
    std::uint64_t tick = 0;
    std::uint64_t content_hash = 0;
    std::vector<ModStamp> mods; // in load order
    std::int64_t saved_at_unix_seconds = 0;
};

struct RawSection {
    std::string tag; // exactly 4 characters
    std::uint16_t version = 1;
    std::vector<std::byte> data; // uncompressed
};

struct CsavContents {
    SaveHeader header;
    std::vector<RawSection> sections;
};

// Largest raw size accepted for one section; a corrupt length cannot make the loader allocate more.
constexpr std::uint32_t k_max_section_bytes = 1U << 30;

// Throws std::invalid_argument for a tag that is not 4 characters or a section over the size limit.
std::vector<std::byte> write_csav(const CsavContents& contents);

// Throws SaveFormatError for a wrong magic, a newer container version, a failed checksum, truncated or
// undecompressable sections, or a section whose decompressed size is not the one recorded.
CsavContents read_csav(std::span<const std::byte> bytes);

// Only the header, still verifying the magic and checksum: for save-game lists.
SaveHeader read_csav_header(std::span<const std::byte> bytes);

} // namespace citysim::save
