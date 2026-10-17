#pragma once

#include <nlohmann/json.hpp>

namespace citysim::content {

// JSON Merge Patch, RFC 7386. Object members of `patch` are merged into `target` recursively; a null member
// deletes that key; anything that is not an object (a number, string, array, ...) replaces the target value
// outright, so arrays are replaced whole, never merged element by element.
nlohmann::json merge_patch(nlohmann::json target, const nlohmann::json& patch);

} // namespace citysim::content
