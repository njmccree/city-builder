#pragma once

#include "citysim/world/map_format_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace citysim::world {

// The element types a `.csr` raster can hold (architecture §15.2).
enum class RasterType : std::uint8_t { U8 = 1, U16 = 2, I16 = 3, F32 = 4 };

const char* raster_type_name(RasterType type);
std::size_t element_size(RasterType type);

template <class T> struct RasterTypeOf; // T -> RasterType, for the four supported types
template <> struct RasterTypeOf<std::uint8_t> {
    static constexpr RasterType value = RasterType::U8;
};
template <> struct RasterTypeOf<std::uint16_t> {
    static constexpr RasterType value = RasterType::U16;
};
template <> struct RasterTypeOf<std::int16_t> {
    static constexpr RasterType value = RasterType::I16;
};
template <> struct RasterTypeOf<float> {
    static constexpr RasterType value = RasterType::F32;
};
template <class T> inline constexpr RasterType k_raster_type_of = RasterTypeOf<T>::value;

// What the file header says about a raster, without its cells.
struct RasterHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RasterType type = RasterType::F32;
    // A stored value `raw` means `raw * scale + offset` (for example elevation in 0.25 m units: scale 0.25).
    float scale = 1.0F;
    float offset = 0.0F;
    // The stored value that means "no data here", if the raster has one.
    std::optional<double> nodata;

    friend bool operator==(const RasterHeader&, const RasterHeader&) = default;
};

// A 2D grid of T, row-major, with row 0 at the SOUTH edge (y grows north, like the world frame), so a cell
// (x, y) is `data()[y * width + x]`. This is the in-memory form of a `.csr` file: the same grid for every
// layer of a map package, whatever it means (elevation, land mask, fertility, ...). Not tied to a clock.
template <class T> class Raster {
  public:
    static constexpr RasterType type = k_raster_type_of<T>;

    Raster() = default;
    // A raster of zeros.
    Raster(std::uint32_t width, std::uint32_t height, float scale = 1.0F, float offset = 0.0F,
           std::optional<double> nodata = std::nullopt)
        : width_(width), height_(height), scale_(scale), offset_(offset), nodata_(nodata),
          cells_(static_cast<std::size_t>(width) * height) {}

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    float scale() const { return scale_; }
    float offset() const { return offset_; }
    const std::optional<double>& nodata() const { return nodata_; }
    RasterHeader header() const { return RasterHeader{width_, height_, type, scale_, offset_, nodata_}; }

    bool in_bounds(std::int64_t x, std::int64_t y) const {
        return x >= 0 && y >= 0 && x < static_cast<std::int64_t>(width_) && y < static_cast<std::int64_t>(height_);
    }
    // The stored value of cell (x, y); the caller keeps it in bounds.
    T& at(std::uint32_t x, std::uint32_t y) { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    const T& at(std::uint32_t x, std::uint32_t y) const {
        return cells_[static_cast<std::size_t>(y) * width_ + x];
    }
    std::span<T> data() { return cells_; }
    std::span<const T> data() const { return cells_; }

    bool is_nodata(std::uint32_t x, std::uint32_t y) const { return is_nodata_raw(at(x, y)); }
    // The decoded value `raw * scale + offset`, or a quiet NaN for a no-data cell.
    float value(std::uint32_t x, std::uint32_t y) const {
        const T raw = at(x, y);
        if (is_nodata_raw(raw)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return static_cast<float>(static_cast<double>(raw) * static_cast<double>(scale_) +
                                  static_cast<double>(offset_));
    }

    friend bool operator==(const Raster&, const Raster&) = default;

  private:
    bool is_nodata_raw(T raw) const {
        if (!nodata_) {
            return false;
        }
        const double stored = static_cast<double>(raw);
        return stored == *nodata_ || (stored != stored && *nodata_ != *nodata_); // NaN matches NaN
    }

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float scale_ = 1.0F;
    float offset_ = 0.0F;
    std::optional<double> nodata_;
    std::vector<T> cells_;
};

// `.csr` files. Layout (little-endian), the contract the simulation, the Python pipeline and the tests share:
//
//   offset size  field
//   0      4     magic "CSRA"
//   4      2     format version (u16, currently 1)
//   6      4     width in cells (u32)
//   10     4     height in cells (u32)
//   14     1     element type (1 = u8, 2 = u16, 3 = i16, 4 = f32)
//   15     1     flags (bit 0: the raster has a no-data value)
//   16     4     scale (f32)
//   20     4     offset (f32)
//   24     8     no-data value (f64; 0 when the flag is not set)
//   32     8     payload size in bytes before compression (u64): width * height * element size
//   40     8     compressed payload size in bytes (u64)
//   48     n     the payload: one zstd frame of the cells, row-major, row 0 = south, each in the element type
//   48+n   8     XXH3-64 checksum (u64) of every byte before it
//
// Writing is deterministic: the same raster gives the same bytes.
template <class T> std::vector<std::byte> encode_csr(const Raster<T>& raster);
// `source` names the data in error messages (a path, usually).
template <class T> Raster<T> decode_csr(std::span<const std::byte> bytes, const std::string& source);

// Writes the file, creating its folder if needed. Throws MapFormatError if it cannot.
template <class T> void write_csr(const std::filesystem::path& path, const Raster<T>& raster);
// Reads and verifies the whole file (checksum included). Throws MapFormatError if it is not a valid `.csr`
// file or holds another element type than T.
template <class T> Raster<T> read_csr(const std::filesystem::path& path);
// Reads only the header, to learn the size and type without loading the cells. The checksum is not checked.
RasterHeader read_csr_header(const std::filesystem::path& path);

} // namespace citysim::world
