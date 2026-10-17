#pragma once

#include <string>

namespace citysim::content {

enum class Severity { Error, Warning };

// One problem found while loading or validating content. `file` is relative to the mod root, `pointer` is a
// JSON Pointer (RFC 6901) into that file, empty for the whole file.
struct ContentIssue {
    Severity severity = Severity::Error;
    std::string file;
    std::string pointer;
    std::string message;

    // "file:pointer: message", the form the validator CLI prints.
    std::string format() const { return file + ":" + pointer + ": " + message; }
};

} // namespace citysim::content
