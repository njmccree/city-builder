#include "citysim/world/raster.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <xxhash.h>
#include <zstd.h>

namespace citysim::world {

static_assert(std::endian::native == std::endian::little,
              ".csr payloads are little-endian and are copied as they are; a big-endian port must swap bytes");

namespace {

constexpr std::array<char, 4> k_magic{'C', 'S', 'R', 'A'};
constexpr std::uint16_t k_version = 1;
constexpr std::size_t k_header_size = 48;
constexpr std::size_t k_checksum_size = 8;
constexpr std::uint8_t k_flag_nodata = 1;
// A sanity limit, so a corrupt header cannot make the reader allocate gigabytes: 2^28 cells.
constexpr std::uint64_t k_most_cells = 1ULL << 28;
constexpr int k_compression_level = 3;

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}
void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }
}
void put_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }
}

std::uint64_t get_le(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

bool known_type(std::uint8_t code) {
    return code >= static_cast<std::uint8_t>(RasterType::U8) && code <= static_cast<std::uint8_t>(RasterType::F32);
}

// The fields of the fixed 48-byte header.
struct ParsedHeader {
    RasterHeader header;
    std::uint64_t raw_size = 0;
    std::uint64_t compressed_size = 0;
};

ParsedHeader parse_header(std::span<const std::byte> bytes, const std::string& source) {
    if (bytes.size() < k_header_size) {
        throw MapFormatError(source, "not a .csr file: only " + std::to_string(bytes.size()) +
                                         " bytes, shorter than the " + std::to_string(k_header_size) +
                                         "-byte header");
    }
    if (std::memcmp(bytes.data(), k_magic.data(), k_magic.size()) != 0) {
        throw MapFormatError(source, "not a .csr file: the first four bytes are not \"CSRA\"");
    }
    const auto version = static_cast<std::uint16_t>(get_le(bytes, 4, 2));
    if (version != k_version) {
        throw MapFormatError(source, "unsupported .csr format version " + std::to_string(version) +
                                         " (this build reads version " + std::to_string(k_version) + ")");
    }
    ParsedHeader parsed;
    parsed.header.width = static_cast<std::uint32_t>(get_le(bytes, 6, 4));
    parsed.header.height = static_cast<std::uint32_t>(get_le(bytes, 10, 4));
    const auto type_code = static_cast<std::uint8_t>(get_le(bytes, 14, 1));
    if (!known_type(type_code)) {
        throw MapFormatError(source, "unknown element type code " + std::to_string(type_code));
    }
    parsed.header.type = static_cast<RasterType>(type_code);
    const auto flags = static_cast<std::uint8_t>(get_le(bytes, 15, 1));
    if ((flags & ~k_flag_nodata) != 0) {
        throw MapFormatError(source, "unknown flags " + std::to_string(flags));
    }
    parsed.header.scale = std::bit_cast<float>(static_cast<std::uint32_t>(get_le(bytes, 16, 4)));
    parsed.header.offset = std::bit_cast<float>(static_cast<std::uint32_t>(get_le(bytes, 20, 4)));
    const double nodata = std::bit_cast<double>(get_le(bytes, 24, 8));
    if ((flags & k_flag_nodata) != 0) {
        parsed.header.nodata = nodata;
    }
    parsed.raw_size = get_le(bytes, 32, 8);
    parsed.compressed_size = get_le(bytes, 40, 8);

    if (parsed.header.width == 0 || parsed.header.height == 0) {
        throw MapFormatError(source, "the raster is empty (" + std::to_string(parsed.header.width) + " x " +
                                         std::to_string(parsed.header.height) + ")");
    }
    const std::uint64_t cells = static_cast<std::uint64_t>(parsed.header.width) * parsed.header.height;
    if (cells > k_most_cells) {
        throw MapFormatError(source, "the raster is implausibly large (" + std::to_string(cells) + " cells)");
    }
    if (parsed.raw_size != cells * element_size(parsed.header.type)) {
        throw MapFormatError(source, "the payload size " + std::to_string(parsed.raw_size) +
                                         " does not match " + std::to_string(parsed.header.width) + " x " +
                                         std::to_string(parsed.header.height) + " cells of " +
                                         raster_type_name(parsed.header.type));
    }
    return parsed;
}

// Encodes a header and the raw (uncompressed, little-endian) cells.
std::vector<std::byte> encode_bytes(const RasterHeader& header, std::span<const std::byte> raw) {
    std::vector<std::byte> compressed(ZSTD_compressBound(raw.size()));
    const std::size_t size = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(),
                                           k_compression_level);
    if (ZSTD_isError(size) != 0U) {
        throw std::runtime_error(std::string("zstd compression failed: ") + ZSTD_getErrorName(size));
    }
    compressed.resize(size);

    std::vector<std::byte> out;
    out.reserve(k_header_size + compressed.size() + k_checksum_size);
    for (const char c : k_magic) {
        out.push_back(static_cast<std::byte>(c));
    }
    put_u16(out, k_version);
    put_u32(out, header.width);
    put_u32(out, header.height);
    out.push_back(static_cast<std::byte>(header.type));
    out.push_back(static_cast<std::byte>(header.nodata ? k_flag_nodata : 0));
    put_u32(out, std::bit_cast<std::uint32_t>(header.scale));
    put_u32(out, std::bit_cast<std::uint32_t>(header.offset));
    put_u64(out, std::bit_cast<std::uint64_t>(header.nodata.value_or(0.0)));
    put_u64(out, raw.size());
    put_u64(out, compressed.size());
    out.insert(out.end(), compressed.begin(), compressed.end());
    put_u64(out, XXH3_64bits(out.data(), out.size()));
    return out;
}

struct Decoded {
    RasterHeader header;
    std::vector<std::byte> raw; // the cells, little-endian
};

Decoded decode_bytes(std::span<const std::byte> bytes, const std::string& source) {
    const ParsedHeader parsed = parse_header(bytes, source);
    const std::uint64_t expected_total = k_header_size + parsed.compressed_size + k_checksum_size;
    if (bytes.size() != expected_total) {
        throw MapFormatError(source, "the file is " + std::string(bytes.size() < expected_total ? "truncated" : "too long") +
                                         ": it has " + std::to_string(bytes.size()) + " bytes but its header says " +
                                         std::to_string(expected_total));
    }
    const std::size_t body = bytes.size() - k_checksum_size;
    const std::uint64_t stored = get_le(bytes, body, k_checksum_size);
    if (XXH3_64bits(bytes.data(), body) != stored) {
        throw MapFormatError(source, "the checksum does not match: the file is corrupt");
    }
    Decoded decoded;
    decoded.header = parsed.header;
    decoded.raw.resize(parsed.raw_size);
    const std::size_t size = ZSTD_decompress(decoded.raw.data(), decoded.raw.size(), bytes.data() + k_header_size,
                                             parsed.compressed_size);
    if (ZSTD_isError(size) != 0U) {
        throw MapFormatError(source, std::string("the payload cannot be decompressed: ") + ZSTD_getErrorName(size));
    }
    if (size != decoded.raw.size()) {
        throw MapFormatError(source, "the payload decompresses to " + std::to_string(size) + " bytes, not " +
                                         std::to_string(decoded.raw.size()));
    }
    return decoded;
}

std::vector<std::byte> read_file(const std::filesystem::path& path, std::size_t limit = SIZE_MAX) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw MapFormatError(path.generic_string(), "cannot open the file");
    }
    const std::streamoff length = in.tellg();
    in.seekg(0);
    const std::size_t size = std::min(static_cast<std::size_t>(length), limit);
    std::vector<std::byte> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)); // NOLINT
    if (!in) {
        throw MapFormatError(path.generic_string(), "cannot read the file");
    }
    return bytes;
}

} // namespace

const char* raster_type_name(RasterType type) {
    switch (type) {
    case RasterType::U8:
        return "u8";
    case RasterType::U16:
        return "u16";
    case RasterType::I16:
        return "i16";
    case RasterType::F32:
        return "f32";
    }
    return "unknown";
}

std::size_t element_size(RasterType type) {
    switch (type) {
    case RasterType::U8:
        return 1;
    case RasterType::U16:
    case RasterType::I16:
        return 2;
    case RasterType::F32:
        return 4;
    }
    return 0;
}

template <class T> std::vector<std::byte> encode_csr(const Raster<T>& raster) {
    if (raster.width() == 0 || raster.height() == 0) {
        throw std::invalid_argument("cannot encode an empty raster");
    }
    const std::span<const T> cells = raster.data();
    return encode_bytes(raster.header(), std::as_bytes(cells));
}

template <class T> Raster<T> decode_csr(std::span<const std::byte> bytes, const std::string& source) {
    const Decoded decoded = decode_bytes(bytes, source);
    if (decoded.header.type != k_raster_type_of<T>) {
        throw MapFormatError(source, std::string("the raster holds ") + raster_type_name(decoded.header.type) +
                                         " cells, but " + raster_type_name(k_raster_type_of<T>) +
                                         " was expected");
    }
    Raster<T> raster(decoded.header.width, decoded.header.height, decoded.header.scale, decoded.header.offset,
                     decoded.header.nodata);
    std::memcpy(raster.data().data(), decoded.raw.data(), decoded.raw.size());
    return raster;
}

template <class T> void write_csr(const std::filesystem::path& path, const Raster<T>& raster) {
    const std::vector<std::byte> bytes = encode_csr(raster);
    if (path.has_parent_path()) {
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw MapFormatError(path.generic_string(), "cannot create the file");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); // NOLINT
    out.close();
    if (!out) {
        throw MapFormatError(path.generic_string(), "cannot write the file");
    }
}

template <class T> Raster<T> read_csr(const std::filesystem::path& path) {
    const std::vector<std::byte> bytes = read_file(path);
    return decode_csr<T>(bytes, path.generic_string());
}

RasterHeader read_csr_header(const std::filesystem::path& path) {
    const std::vector<std::byte> bytes = read_file(path, k_header_size);
    return parse_header(bytes, path.generic_string()).header;
}

#define CITYSIM_INSTANTIATE_CSR(T)                                                                                  \
    template std::vector<std::byte> encode_csr<T>(const Raster<T>&);                                                \
    template Raster<T> decode_csr<T>(std::span<const std::byte>, const std::string&);                               \
    template void write_csr<T>(const std::filesystem::path&, const Raster<T>&);                                     \
    template Raster<T> read_csr<T>(const std::filesystem::path&);

CITYSIM_INSTANTIATE_CSR(std::uint8_t)
CITYSIM_INSTANTIATE_CSR(std::uint16_t)
CITYSIM_INSTANTIATE_CSR(std::int16_t)
CITYSIM_INSTANTIATE_CSR(float)

#undef CITYSIM_INSTANTIATE_CSR

} // namespace citysim::world
