#include "citysim/save/binary_writer.hpp"

#include <stdexcept>

namespace citysim::save {

void BinaryWriter::varint(const std::uint64_t& value) {
    std::uint64_t rest = value;
    while (rest >= 0x80U) {
        bytes_.push_back(static_cast<std::byte>((rest & 0x7FU) | 0x80U));
        rest >>= 7;
    }
    bytes_.push_back(static_cast<std::byte>(rest));
}

void BinaryWriter::svarint(const std::int64_t& value) {
    // Zigzag: small magnitudes of either sign stay short.
    const std::uint64_t zigzag =
        (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
    varint(zigzag);
}

void BinaryWriter::string(const std::string& value) {
    varint(static_cast<std::uint64_t>(value.size()));
    raw(value.data(), value.size());
}

void BinaryWriter::raw(const void* data, std::size_t length) {
    const auto* begin = static_cast<const std::byte*>(data);
    bytes_.insert(bytes_.end(), begin, begin + length);
}

void BinaryWriter::entity(const entt::entity& value) {
    if (value == entt::null) {
        varint(0);
        return;
    }
    std::uint64_t index = 0;
    if (remap_ == nullptr || !remap_->index_of(value, index)) {
        throw std::logic_error(
            "saving a reference to an entity that does not exist (or without an entity remap)");
    }
    varint(index + 1);
}

void BinaryWriter::block(const BinaryWriter& inner) {
    varint(static_cast<std::uint64_t>(inner.bytes_.size()));
    bytes_.insert(bytes_.end(), inner.bytes_.begin(), inner.bytes_.end());
}

} // namespace citysim::save
