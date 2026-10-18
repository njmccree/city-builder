#pragma once

#include <string>

namespace citysim::content {

// Info is for things worth knowing but never a failure, such as unused localization keys.
enum class Severity { Error, Warning, Info };

// One problem found while loading or validating content. `file` is relative to the mod root, `pointer` is a
// JSON Pointer (RFC 6901) into that file, empty for the whole file.
struct ContentIssue {
    Severity severity = Severity::Error;
    std::string file;
    std::string pointer;
    std::string message;
    std::string mod_root; // the mod root `file` is relative to, as it was given; empty if unknown

    // "file:pointer: message", the form the validator CLI prints.
    std::string format() const { return file + ":" + pointer + ": " + message; }

    // The same, with the file path starting at the mod root, so it can be opened from where the tool ran.
    std::string format_full() const {
        return (mod_root.empty() ? file : mod_root + "/" + file) + ":" + pointer + ": " + message;
    }
};

} // namespace citysim::content
