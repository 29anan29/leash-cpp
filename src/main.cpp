#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "checker/typecheck.hpp"
#include "codegen/compiler.hpp"
#include "codegen/llvmgen.hpp"
#include "vm/vm.hpp"
#include "host/host.hpp"
#include "host/native.hpp"
#include "common/json.hpp"
#include "common/error.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

using namespace leash;

// 标准库搜索路径（裸模块名 import 时依次尝试），由 main 在启动时填充
static std::vector<std::string> g_libDirs;

// Inject native functions from a known built-in package into prog.fns
static void injectPackage(Program& prog, const Package& pkg) {
    for (const auto& fdef : pkg.funcs) {
        // avoid duplicates
        bool dup = false;
        for (const auto& f : prog.fns)
            if (f->name == fdef.name && f->isNative) { dup = true; break; }
        if (dup) continue;

        auto fn = std::make_shared<Stmt>();
        fn->k = Stmt::SFn;
        fn->name = fdef.name;
        fn->params.resize(fdef.arity); // arity only; no param names needed
        fn->isNative = true;
        fn->nativeFn = fdef.fn;
        fn->body = {}; // no user body
        prog.fns.push_back(fn);
    }
}

// 读取文件内容为字符串
static std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw CompileError("无法打开导入文件: " + path);
    std::stringstream buf;
    buf << ifs.rdbuf();
    return buf.str();
}

// 取路径所在目录
static std::string dirOf(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? "." : path.substr(0, pos);
}

// 把一个模块文件解析并合并进 allFns / allPackages；收集 import（含 .json）进 queue
static void loadModule(const std::string& path,
                        std::vector<std::string>& queue,
                        std::unordered_set<std::string>& seen,
                        std::vector<std::shared_ptr<Stmt>>& allFns,
                        std::vector<std::string>& allPackages,
                        std::vector<std::string>& allJsonImports) {
    if (seen.count(path)) return;     // 已处理（含循环引用防护）
    seen.insert(path);
    Program p = Parser(Lexer::tokenize(readFile(path))).parse();
    for (auto& f : p.fns) allFns.push_back(f);
    for (auto& pkg : p.packages)
        if (std::find(allPackages.begin(), allPackages.end(), pkg) == allPackages.end())
            allPackages.push_back(pkg);
    for (auto& j : p.jsonImports) {
        std::string resolved = j;
        if (j.find('/') != std::string::npos)
            resolved = (j[0] == '/' || j.find(':') != std::string::npos) ? j : dirOf(path) + "/" + j;
        else
            resolved = dirOf(path) + "/" + j;
        allJsonImports.push_back(resolved);
    }
    std::string d = dirOf(path);
    for (auto& imp : p.imports) {
        std::string resolved = imp;
        if (imp.find('/') != std::string::npos || imp.find('.') != std::string::npos)
            resolved = (imp[0] == '/' || imp.find(':') != std::string::npos) ? imp : d + "/" + imp;
        else {
            // 裸模块名：先在目录内找，再依次尝试标准库路径
            resolved = d + "/" + imp + ".ae";
            if (!std::ifstream(resolved)) {
                for (const auto& lib : g_libDirs) {
                    std::string cand = lib + "/" + imp + ".ae";
                    if (std::ifstream(cand)) { resolved = cand; break; }
                }
            }
        }
        if (resolved.size() < 3 || resolved.substr(resolved.size() - 3) != ".ae")
            resolved += ".ae";
        queue.push_back(resolved);
    }
}

// 以入口文件为根，递归收集所有模块，拼成一个完整的 Program
static Program assemble(const std::string& entryPath, std::vector<std::string>& jsonImports) {
    std::vector<std::string> queue{entryPath};
    std::unordered_set<std::string> seen;
    std::vector<std::shared_ptr<Stmt>> allFns;
    std::vector<std::string> allPackages;
    for (size_t i = 0; i < queue.size(); ++i)
        loadModule(queue[i], queue, seen, allFns, allPackages, jsonImports);
    Program merged;
    merged.fns = allFns;
    merged.packages = allPackages;
    merged.jsonImports = jsonImports;
    return merged;
}

// 从已解析的 JSON 值里收集顶层键作为全局变量名
static std::vector<std::string> collectGlobalNames(const Store& store, const Value& v) {
    std::vector<std::string> names;
    if (v.t == Value::T::Map) {
        for (const auto& kv : store.maps[v.idx()])
            names.push_back(kv.first);
    }
    return names;
}

int main(int argc, char** argv) {
    std::string auditFilePath;
    bool compileMode = false;
    bool emitCpp = false;
    std::string outputPath;
    int fileArgIdx = 1;
    // Parse flags
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--audit-file" && i + 1 < argc) {
            auditFilePath = argv[++i];
            fileArgIdx = i + 1;
        } else if (arg == "-c" || arg == "--compile") {
            compileMode = true;
            fileArgIdx = i + 1;
        } else if (arg == "-S" || arg == "--emit-cpp") {
            emitCpp = true;
            fileArgIdx = i + 1;
        } else if (arg == "-o" && i + 2 < argc) {
            outputPath = argv[++i];
            fileArgIdx = i + 1;
        } else {
            fileArgIdx = i;
            break;
        }
    }
    if (argc <= fileArgIdx) {
        std::cerr << "用法: " << argv[0] << " [--audit-file <file>] [-c|-S] [-o <output>] <file.ae>" << std::endl;
        return 1;
    }
    std::string entryFile = argv[fileArgIdx];
    std::string shebangTmpFile;
    bool shebangDetected = false;

    // 检查入口文件第一行："#c" 启用编译模式，"#" 为解释模式（默认）
    // 若第一行以 # 开头，将其从源文件中剥离（以免词法分析器报错）
    {
        std::ifstream ifs(entryFile);
        std::string firstLine;
        if (std::getline(ifs, firstLine)) {
            if (!firstLine.empty() && firstLine.back() == '\r')
                firstLine.pop_back();
            if (firstLine == "#c") {
                compileMode = true;
                shebangDetected = true;
            } else if (!firstLine.empty() && firstLine[0] == '#') {
                shebangDetected = true;
            }
            if (shebangDetected) {
                std::string rest;
                std::ostringstream oss;
                oss << ifs.rdbuf();
                rest = oss.str();
                static int shebangCount = 0;
                shebangTmpFile = "/tmp/leash_shebang_" + std::to_string(++shebangCount) + ".ae";
                std::ofstream ofs(shebangTmpFile);
                ofs << rest;
                entryFile = shebangTmpFile;
            }
        }
    }

    if (outputPath.empty()) {
        if (compileMode || emitCpp) {
            // 若 entryFile 因 shebang 变成了临时文件，用原始文件名派生输出路径
            std::string baseFile = shebangDetected ? std::string(argv[fileArgIdx]) : entryFile;
            outputPath = baseFile;
            if (outputPath.size() > 3 && outputPath.substr(outputPath.size()-3) == ".ae")
                outputPath = outputPath.substr(0, outputPath.size()-3);
        } else {
            outputPath = "a.out";
        }
    }

    try {
        initBuiltinPackages();

        // 标准库搜索路径：入口目录/lib、LEASH_LIB 环境变量、可执行文件目录/lib
        std::string entryDir = dirOf(entryFile);
        g_libDirs.push_back(entryDir + "/lib");
        g_libDirs.push_back("lib");
        if (const char* envLib = std::getenv("LEASH_LIB"))
            g_libDirs.push_back(envLib);
        {
            std::string exe = argv[0];
            std::string exeDir = dirOf(exe);
            if (!exeDir.empty() && exeDir != ".") g_libDirs.push_back(exeDir + "/lib");
            g_libDirs.push_back("/usr/local/lib/leash");
        }

        // 1. 词法/语法/模块收集：以入口文件为根组装完整 Program
        std::vector<std::string> jsonImports;
        Program prog = assemble(entryFile, jsonImports);

        // 1b. 解析 JSON 配置，提取顶层键名作为全局变量名（先用临时 Store 取键名）
        std::vector<std::string> globalNames;
        {
            Store tmpStore;
            for (const auto& jp : jsonImports) {
                std::string text = readFile(jp);
                Value root = parseJson(text, tmpStore);
                for (const auto& n : collectGlobalNames(tmpStore, root))
                    globalNames.push_back(n);
            }
        }

        // 1c. 导入了配置文件（import "*.json"）即自动获得 chat 能力
        if (!jsonImports.empty()) {
            if (std::find(prog.packages.begin(), prog.packages.end(), std::string("ai")) == prog.packages.end())
                prog.packages.push_back("ai");
        }

        // 1d. core（len 等基础函数）与 json（映射操作）始终可用
        //     映射字面量 {"k": v} 已是一等公民，json_keys/json_get 等也应随时可用
        if (std::find(prog.packages.begin(), prog.packages.end(), std::string("core")) == prog.packages.end())
            prog.packages.push_back("core");
        if (std::find(prog.packages.begin(), prog.packages.end(), std::string("json")) == prog.packages.end())
            prog.packages.push_back("json");

        // 2. Inject native functions from declared packages (skip "agent" which is special)
        for (const auto& pkgName : prog.packages) {
            if (pkgName == "agent") continue;
            const Package* pkg = findPackage(pkgName);
            if (pkg) {
                injectPackage(prog, *pkg);
            }
            // unknown package names are silently allowed (future external modules)
        }

        // 4. Type / capability check
        Checker checker;
        checker.check(prog, prog.packages, globalNames);

        // 5. Compile to bytecode
        Compiler compiler;
        auto funcs = compiler.compile(prog, prog.packages, globalNames);

        // 5b. 编译/S输出模式
        if (compileMode || emitCpp) {
            LLVMGen llvmgen;
            std::string src = llvmgen.generate(funcs, globalNames);
            std::string cppPath = outputPath + ".cpp";
            {
                std::ofstream ofs(cppPath);
                if (!ofs) throw CompileError("无法写入 " + cppPath);
                ofs << src;
            }
            if (emitCpp) {
                std::cerr << "已生成: " << cppPath << std::endl;
                return 0;
            }
            std::string errorMsg;
            if (!LLVMGen::compileToBinary(cppPath, outputPath, errorMsg))
                throw CompileError(errorMsg);
            std::remove(cppPath.c_str());
            std::cerr << "编译成功: " << outputPath << std::endl;
            return 0;
        }

        // 6. Run
        Store store;
        AuditLog audit;
        HostContext hostCtx(store, audit);
        VM vm(funcs, hostCtx, 1'000'000);

        // 6b. 用真正的 Store 重新解析 JSON，构建全局变量并注入 VM / HostContext
        {
            std::unordered_map<std::string, Value> globals;
            for (const auto& jp : jsonImports) {
                std::string text = readFile(jp);
                Value root = parseJson(text, store);
                if (root.t == Value::T::Map) {
                    for (const auto& kv : store.maps[root.idx()])
                        globals[kv.first] = kv.second;
                }
            }
            vm.setGlobals(globals);
            hostCtx.setGlobals(globals);
        }

        audit.outputPath = auditFilePath;
        Value result = vm.run();
        audit.printAll();
        (void)result;

    } catch (const CompileError& e) {
        if (e.line > 0)
            std::cerr << "[错误] 第 " << e.line << " 行: " << e.what() << std::endl;
        else
            std::cerr << "[错误] " << e.what() << std::endl;
        return 1;
    } catch (const RuntimeError& e) {
        if (e.line > 0)
            std::cerr << "[运行时错误] 第 " << e.line << " 行: " << e.what() << std::endl;
        else
            std::cerr << "[运行时错误] " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[异常] " << e.what() << std::endl;
        return 1;
    }

    // 清理 shebang 临时文件
    if (!shebangTmpFile.empty())
        std::remove(shebangTmpFile.c_str());

    return 0;
}
