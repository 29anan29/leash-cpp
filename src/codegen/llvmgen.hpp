#pragma once
#include "codegen/bytecode.hpp"
#include <string>
#include <vector>
#include <sstream>

namespace leash {

class LLVMGen {
public:
    // Generate C++ source from compiled bytecode
    std::string generate(const std::vector<Function>& funcs,
                         const std::vector<std::string>& globalNames = {});

    // Compile with g++ to produce executable
    static bool compileToBinary(const std::string& cppPath,
                                const std::string& outputPath,
                                std::string& errorOut);

private:
    void emitInstr(std::ostringstream& os, const Function& fn, const std::vector<Function>& allFuncs, const Instr& inst, size_t ip, size_t fi);
    std::string safeName(const std::string& name);
    std::vector<std::string> funcNames_;
    int nextLabel_ = 0;
};

} // namespace leash
