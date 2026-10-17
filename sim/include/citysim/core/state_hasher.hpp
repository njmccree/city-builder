#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

namespace citysim::core {

// Streaming 64-bit fingerprint of simulation state (architecture §4, §13), built on XXH3.
// Not tied to a clock. Bytes are hashed in native byte order: the guarantee is reproducibility
// within one binary and platform (§4 rule 7), not across platforms.
class StateHasher {
  public:
    StateHasher();
    ~StateHasher();
    StateHasher(const StateHasher&) = delete;
    StateHasher& operator=(const StateHasher&) = delete;
    StateHasher(StateHasher&&) noexcept;
    StateHasher& operator=(StateHasher&&) noexcept;

    void add(std::span<const std::byte> bytes);

    // Length-prefixed, so ("ab", "c") and ("a", "bc") hash differently.
    void add(std::string_view text);

    // Only for types whose every byte is meaningful: no padding and no floating point. Padding bytes
    // are indeterminate and would make equal states hash differently; hash such types field by field,
    // and floats through add_float().
    template <class T>
        requires(std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>)
    void add_pod(const T& value) {
        add(std::as_bytes(std::span<const T, 1>(&value, 1)));
    }

    // Hashes the bit pattern, except that -0.0 hashes as 0.0 and every NaN hashes as one canonical NaN,
    // since those compare or behave the same in the simulation.
    void add_float(float value);
    void add_float(double value);

    // The hash of everything added so far. Does not reset, so more data can follow.
    std::uint64_t digest() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace citysim::core
