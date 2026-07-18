#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "checker/typecheck.hpp"
#include "codegen/compiler.hpp"
#include "vm/vm.hpp"
#include "host/host.hpp"
#include "host/native.hpp"
#include "common/error.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <algorithm>

using namespace aegis;

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

// 把一个模块文件解析并合并进 allFns / allPackages，并收集其 import 进 queue
static void loadModule(const std::string& path,
                       std::vector<std::string>& queue,
                       std::unordered_set<std::string>& seen,
                       std::vector<std::shared_ptr<Stmt>>& allFns,
                       std::vector<std::string>& allPackages) {
    if (seen.count(path)) return;     // 已处理（含循环引用防护）
    seen.insert(path);
    Program p = Parser(Lexer::tokenize(readFile(path))).parse();
    for (auto& f : p.fns) allFns.push_back(f);
    for (auto& pkg : p.packages)
        if (std::find(allPackages.begin(), allPackages.end(), pkg) == allPackages.end())
            allPackages.push_back(pkg);
    std::string d = dirOf(path);
    for (auto& imp : p.imports) {
        std::string resolved = imp;
        if (imp.find('/') != std::string::npos || imp.find('.') != std::string::npos)
            resolved = (imp[0] == '/' || imp.find(':') != std::string::npos) ? imp : d + "/" + imp;
        else
            resolved = d + "/" + imp;   // 裸模块名按同目录文件处理
        if (resolved.size() < 3 || resolved.substr(resolved.size() - 3) != ".ae")
            resolved += ".ae";
        queue.push_back(resolved);
    }
}

// 以入口文件为根，递归收集所有模块，拼成一个完整的 Program
static Program assemble(const std::string& entryPath) {
    std::vector<std::string> queue{entryPath};
    std::unordered_set<std::string> seen;
    std::vector<std::shared_ptr<Stmt>> allFns;
    std::vector<std::string> allPackages;
    for (size_t i = 0; i < queue.size(); ++i)
        loadModule(queue[i], queue, seen, allFns, allPackages);
    Program merged;
    merged.fns = allFns;
    merged.packages = allPackages;
    return merged;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <file.ae>" << std::endl;
        return 1;
    }

    try {
        initBuiltinPackages();

        // 1. 词法/语法/模块收集：以入口文件为根组装完整 Program
        Program prog = assemble(argv[1]);

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
        checker.check(prog, prog.packages);

        // 5. Compile to bytecode
        Compiler compiler;
        auto funcs = compiler.compile(prog, prog.packages);

        // 6. Run
        Store store;
        AuditLog audit;
        HostContext hostCtx(store, audit);
        VM vm(funcs, hostCtx, 1'000'000);
        Value result = vm.run();

        (void)result;

    } catch (const CompileError& e) {
        std::cerr << "[错误] " << e.what() << std::endl;
        return 1;
    } catch (const RuntimeError& e) {
        std::cerr << "[运行时错误] " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[异常] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
