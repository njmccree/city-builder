#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace citysim::world {

// Thrown when a map file is not what the format says: a `.csr` raster that is truncated, corrupt or from a
// newer build, a package.json with a missing or wrong field, a file whose checksum does not match. `where` is
// the file (or, for in-memory data, the name the caller gave it), and `reason` says what is wrong, with a
// JSON pointer for manifest problems. Logic errors in the calling code (a raster of the wrong element type,
// say) throw std::invalid_argument instead.
class MapFormatError : public std::runtime_error {
  public:
    MapFormatError(std::string where, std::string reason)
        : std::runtime_error(where + ": " + reason), where_(std::move(where)), reason_(std::move(reason)) {}

    const std::string& where() const { return where_; }
    const std::string& reason() const { return reason_; }

  private:
    std::string where_;
    std::string reason_;
};

} // namespace citysim::world
