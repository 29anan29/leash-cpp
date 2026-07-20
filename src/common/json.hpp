#pragma once
#include "common/value.hpp"
#include <string>

namespace leash {

// Parse a JSON text into a Leash Value (Map/List/Int/Float/Bool/Str/Nil).
// Throws std::runtime_error on malformed input.
Value parseJson(const std::string& text, Store& store);

} // namespace leash
