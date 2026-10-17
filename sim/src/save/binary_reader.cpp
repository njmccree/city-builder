#include "citysim/save/binary_reader.hpp"

#include <algorithm>
#include <cstring>

namespace citysim::save {

void BinaryReader::need(std::size_t count, const char* what) const {
    if (remaining() < count) {
        fail(std::string("unexpected end of data while reading ") + what);
    }
}

void BinaryReader::flag(bool& value) {
    need(1, "a flag");
    const auto byte = std::to_integer<unsigned>(bytes_[position_]);
    if (byte > 1) {
        fail("a flag must be 0 or 1");
    }
    ++position_;
    value = byte == 1;
}

void BinaryReader::varint(std::uint64_t& value) {
    std::uint64_t result = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        need(1, "a varint");
        const auto byte = std::to_integer<unsigned>(bytes_[position_++]);
        if (shift == 63 && (byte & 0x7EU) != 0) {
            fail("varint does not fit in 64 bits");
        }
        result |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0) {
            value = result;
            return;
        }
    }
    fail("varint is too long");
}

void BinaryReader::svarint(std::int64_t& value) {
    std::uint64_t zigzag = 0;
    varint(zigzag);
    value = static_cast<std::int64_t>((zigzag >> 1) ^ (~(zigzag & 1U) + 1U));
}

void BinaryReader::string(std::string& value) {
    std::uint64_t length = 0;
    varint(length);
    if (length > remaining()) {
        fail("string is longer than the data that is left");
    }
    value.resize(static_cast<std::size_t>(length));
    raw(value.data(), value.size());
}

void BinaryReader::size(std::uint64_t& count) {
    varint(count);
    if (count > remaining()) {
        fail("collection size " + std::to_string(count) + " is larger than the data that is left");
    }
}

void BinaryReader::raw(void* data, std::size_t length) {
    need(length, "bytes");
    if (length > 0) {
        std::memcpy(data, bytes_.data() + position_, length);
    }
    position_ += length;
}

void BinaryReader::entity(entt::entity& value) {
    std::uint64_t encoded = 0;
    varint(encoded);
    if (encoded == 0) {
        value = entt::null;
        return;
    }
    if (remap_ == nullptr || !remap_->entity_at(encoded - 1, value)) {
        fail("entity reference " + std::to_string(encoded - 1) + " does not exist");
    }
}

BinaryReader BinaryReader::block() {
    std::uint64_t length = 0;
    varint(length);
    if (length > remaining()) {
        fail("block is longer than the data that is left");
    }
    BinaryReader inner(bytes_.subspan(position_, static_cast<std::size_t>(length)), remap_, content_,
                       offset());
    position_ += static_cast<std::size_t>(length);
    return inner;
}

void BinaryReader::skip(std::size_t length) {
    need(length, "bytes to skip");
    position_ += length;
}

} // namespace citysim::save
