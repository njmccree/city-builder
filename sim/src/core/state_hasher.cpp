#include "citysim/core/state_hasher.hpp"

#include <bit>
#include <limits>
#include <xxhash.h>

namespace citysim::core {

struct StateHasher::Impl {
    struct Deleter {
        void operator()(XXH3_state_t* raw) const { XXH3_freeState(raw); }
    };
    std::unique_ptr<XXH3_state_t, Deleter> state{XXH3_createState()};
};

StateHasher::StateHasher() : impl_(std::make_unique<Impl>()) {
    XXH3_64bits_reset(impl_->state.get());
}
StateHasher::~StateHasher() = default;
StateHasher::StateHasher(StateHasher&&) noexcept = default;
StateHasher& StateHasher::operator=(StateHasher&&) noexcept = default;

void StateHasher::add(std::span<const std::byte> bytes) {
    XXH3_64bits_update(impl_->state.get(), bytes.data(), bytes.size());
}

void StateHasher::add(std::string_view text) {
    add_pod(static_cast<std::uint64_t>(text.size()));
    add(std::as_bytes(std::span<const char>(text.data(), text.size())));
}

void StateHasher::add_float(float value) {
    if (value != value) {
        add_pod(std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    } else if (value == 0.0F) {
        add_pod(std::uint32_t{0});
    } else {
        add_pod(std::bit_cast<std::uint32_t>(value));
    }
}

void StateHasher::add_float(double value) {
    if (value != value) {
        add_pod(std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()));
    } else if (value == 0.0) {
        add_pod(std::uint64_t{0});
    } else {
        add_pod(std::bit_cast<std::uint64_t>(value));
    }
}

std::uint64_t StateHasher::digest() const {
    return XXH3_64bits_digest(impl_->state.get());
}

} // namespace citysim::core
