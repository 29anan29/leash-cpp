#pragma once
#include <stdexcept>
#include <string>

namespace leash {

struct CompileError : std::runtime_error {
    int line = 0;
    CompileError(const std::string& msg, int lineNumber = 0)
        : std::runtime_error(msg), line(lineNumber) {}
};
struct RuntimeError : std::runtime_error {
    int line = 0;
    RuntimeError(const std::string& msg, int lineNumber = 0)
        : std::runtime_error(msg), line(lineNumber) {}
};

} // namespace leash
