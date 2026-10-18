#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace citysim::save {

// Thrown when bytes being loaded are not a valid save: truncated, corrupt, from a newer build, or naming
// something that does not exist. Carries the offset (in the buffer being read) where it was noticed. Logic
// errors in the calling code, such as saving an unregistered type, throw std::logic_error instead.
class SaveFormatError : public std::runtime_error {
  public:
    SaveFormatError(const std::string& message, std::size_t offset)
        : std::runtime_error(message + " (at byte " + std::to_string(offset) + ")"), offset_(offset) {}

    std::size_t offset() const { return offset_; }

  private:
    std::size_t offset_;
};

} // namespace citysim::save
