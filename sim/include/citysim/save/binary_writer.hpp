#pragma once

#include "citysim/save/entity_remap.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace citysim::content {
class ContentDatabase;
}

namespace citysim::save {

// Writes the binary save format (architecture §10) into a byte buffer: little-endian fixed-width integers,
// LEB128 varints (zigzag for signed), floats as bit patterns, length-prefixed UTF-8 strings.
//
// It has the same member names as BinaryReader, so one `serialize` function serves both (see archive.hpp).
// Writing never changes the value, so the members take `const T&`. Not tied to a clock.
class BinaryWriter {
  public:
    static constexpr bool is_loading = false;

    explicit BinaryWriter(const EntityRemap* remap = nullptr,
                          const content::ContentDatabase* content = nullptr)
        : remap_(remap), content_(content) {}

    // --- primitives (same names as BinaryReader) -----------------------------------------------------------
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    void fixed(const T& value) {
        using U = std::make_unsigned_t<T>;
        const U bits = static_cast<U>(value);
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            bytes_.push_back(static_cast<std::byte>((bits >> (8 * i)) & 0xFFU));
        }
    }
    void flag(const bool& value) { bytes_.push_back(static_cast<std::byte>(value ? 1 : 0)); }
    void varint(const std::uint64_t& value);
    void svarint(const std::int64_t& value);
    void f32(const float& value) { fixed(std::bit_cast<std::uint32_t>(value)); }
    void f64(const double& value) { fixed(std::bit_cast<std::uint64_t>(value)); }
    void string(const std::string& value);
    // A collection length. The reader checks it against the bytes that are left.
    void size(const std::uint64_t& count) { varint(count); }
    // Raw bytes, no length (the reader must know it).
    void raw(const void* data, std::size_t length);
    // An entity reference, as its dense index + 1 (0 is "no entity"). Needs a remap.
    void entity(const entt::entity& value);

    // --- blocks --------------------------------------------------------------------------------------------
    // A writer for a nested block that shares this one's remap and content; append it with block().
    BinaryWriter child() const { return BinaryWriter(remap_, content_); }
    // Writes `inner`'s bytes prefixed by their length, so a reader can skip or bound them.
    void block(const BinaryWriter& inner);

    // --- state ---------------------------------------------------------------------------------------------
    std::span<const std::byte> bytes() const { return bytes_; }
    std::size_t offset() const { return bytes_.size(); }
    std::vector<std::byte> take() { return std::move(bytes_); }

    const EntityRemap* remap() const { return remap_; }
    void set_remap(const EntityRemap* remap) { remap_ = remap; }
    const content::ContentDatabase* content() const { return content_; }

  private:
    std::vector<std::byte> bytes_;
    const EntityRemap* remap_;
    const content::ContentDatabase* content_;
};

} // namespace citysim::save
