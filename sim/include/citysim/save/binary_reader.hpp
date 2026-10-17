#pragma once

#include "citysim/save/entity_remap.hpp"
#include "citysim/save/save_format_error.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <span>
#include <string>
#include <type_traits>

namespace citysim::content {
class ContentDatabase;
}

namespace citysim::save {

// Reads what BinaryWriter wrote. Every read is bounds-checked and throws SaveFormatError, with the byte
// offset, on truncated or malformed input; it never reads past the buffer and never allocates more than the
// bytes left could describe. Same member names as BinaryWriter. Not tied to a clock.
class BinaryReader {
  public:
    static constexpr bool is_loading = true;

    explicit BinaryReader(std::span<const std::byte> bytes, const EntityRemap* remap = nullptr,
                          const content::ContentDatabase* content = nullptr, std::size_t base_offset = 0)
        : bytes_(bytes), remap_(remap), content_(content), base_(base_offset) {}

    // --- primitives (same names as BinaryWriter) -----------------------------------------------------------
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    void fixed(T& value) {
        using U = std::make_unsigned_t<T>;
        need(sizeof(U), "integer");
        U bits = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i) {
            bits = static_cast<U>(
                bits | (static_cast<U>(std::to_integer<unsigned>(bytes_[position_ + i])) << (8 * i)));
        }
        position_ += sizeof(U);
        value = static_cast<T>(bits);
    }
    void flag(bool& value);
    void varint(std::uint64_t& value);
    void svarint(std::int64_t& value);
    void f32(float& value) {
        std::uint32_t bits = 0;
        fixed(bits);
        value = std::bit_cast<float>(bits);
    }
    void f64(double& value) {
        std::uint64_t bits = 0;
        fixed(bits);
        value = std::bit_cast<double>(bits);
    }
    void string(std::string& value);
    // A collection length; must not exceed the bytes left, because every element takes at least one byte.
    void size(std::uint64_t& count);
    void raw(void* data, std::size_t length);
    void entity(entt::entity& value);

    // --- blocks --------------------------------------------------------------------------------------------
    // Reads a length written by BinaryWriter::block and returns a reader over exactly those bytes.
    BinaryReader block();
    void skip(std::size_t length);

    // --- state ---------------------------------------------------------------------------------------------
    std::size_t remaining() const { return bytes_.size() - position_; }
    bool at_end() const { return position_ == bytes_.size(); }
    // Position in the outermost buffer, for messages.
    std::size_t offset() const { return base_ + position_; }
    [[noreturn]] void fail(const std::string& message) const { throw SaveFormatError(message, offset()); }

    const EntityRemap* remap() const { return remap_; }
    void set_remap(const EntityRemap* remap) { remap_ = remap; }
    const content::ContentDatabase* content() const { return content_; }

  private:
    void need(std::size_t count, const char* what) const;

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
    const EntityRemap* remap_;
    const content::ContentDatabase* content_;
    std::size_t base_;
};

} // namespace citysim::save
