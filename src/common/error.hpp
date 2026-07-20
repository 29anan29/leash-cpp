#pragma once
#include <stdexcept>
#include <string>

namespace leash {

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct RuntimeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace leash
