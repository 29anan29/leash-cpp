# Leash

<p align="center">
  <img src="https://img.shields.io/github/languages/count/29anan29/leash-cpp" alt="languages">
  <img src="https://img.shields.io/github/languages/top/29anan29/leash-cpp" alt="top-lang">
  <img src="https://img.shields.io/github/last-commit/29anan29/leash-cpp" alt="last-commit">
  <img src="https://img.shields.io/github/repo-size/29anan29/leash-cpp" alt="repo-size">
  <img src="https://img.shields.io/github/license/29anan29/leash-cpp" alt="license">
</p>

> 一门面向 **AI Agent** 的安全脚本语言：能力门控、最小特权、确定性执行。

Leash（埃癸斯，希腊神话中的"神盾"）把**安全**和**可控**作为核心设计目标。程序默认没有任何权限——读文件、访问环境、调用 AI 模型等所有副作用都必须通过宿主显式注入的"能力（capability）"完成，并且每一步都被审计。这让 AI Agent 可以在严格受限的沙箱中自主运行，同时仍拥有完成任务所需的工具。

配套提供一个 Vim 风格的终端编辑器 **leash-studio**，自带语法高亮、搜索、跳转、横向滚动，并能一键编译运行。

---

## 目录

- [特性](#特性)
- [快速开始](#快速开始)
- [构建](#构建)
- [30 秒上手](#30-秒上手)
- [语言速查（当前已实现子集）](#语言速查当前已实现子集)
- [内置包](#内置包)
- [多文件模块](#多文件模块)
- [leash-studio 使用](#leash-studio-使用)
- [目录结构](#目录结构)
- [深入学习](#深入学习)

---

## 特性

- **能力安全模型**：默认零权限；副作用全部走宿主注入的能力，未授权即被拒绝。
- **最小特权 / 审计**：每一次能力调用都被记录，作用域可静态追踪。
- **确定性**：无隐式并发、无未授权时钟；运行时按 fuel 限制步数、限制内存与递归深度。
- **缩进式语法**：类 Python 的缩进代码块，易读、无括号噪音。
- **内置包**：`file` / `os` / `ai` 三类常用能力，开箱即用。
- **多文件模块**：`import "xxx.ae"` 递归收集、去重、环检测。
- **零依赖编辑器**：`leash-studio` 是单文件 C++，自带高亮与运行。

---

## 快速开始

```bash
make                 # 同时构建 leash 与 leash-studio
./leash examples/hello.ae        # 运行示例
./leash-studio                   # 启动编辑器（或 make run-ide）
```

预期输出：

```
Leash 语言 — 安全 Agent 脚本
hello world!
x + y = 50
```

---

## 构建

| 命令 | 作用 |
|------|------|
| `make` | 构建 `leash`（编译器）与 `leash-studio`（编辑器） |
| `make run` | 运行 `./leash examples/hello.ae` |
| `make run-ide` | 启动 `./leash-studio` |
| `make clean` | 清除 `obj/` 与两个二进制 |

> 仅想编译编辑器（无依赖）：`g++ -std=c++17 -O2 -o leash-studio studio/src/minivim.cpp`

构建要求：支持 C++17 的 `g++` / `clang++`。

---

## 30 秒上手

`hello.ae`：

```leash
fn main
  let name = "world"
  out("hello " + name)
  let x = 42
  let y = 8
  out("x + y = {x + y}")
```

运行：

```bash
./leash hello.ae
```

要点：

- 代码块用**缩进**表示（同 Python），不要写 `{ }`。
- 打印用 `out("...")`，括号内是字符串表达式；`{expr}` 做字符串插值。
- `//` 是行注释（`#` 不是注释）。

---

## 语言速查（当前已实现子集）

下面这些"看起来像 C/Python"的写法在本语言里是**无效**的，已被实测验证：

| 易错点 | 正确写法 | 错误写法 |
|--------|----------|----------|
| 代码块 | **缩进**（无 `{ }`） | `fn f() { ... }` ❌ |
| 打印 | `out("字符串")` | `out -> "..."` ❌ / `out "..."` ❌ |
| 注释 | `// 行注释` | `# 注释` ❌ |
| 多文件引用 | `import "math.ae"`（带引号路径） | `import math` ❌ |
| `package` 暴露的函数 | **裸全局名** `append(...)` / `env(...)` / `chat(...)` | `file.append(...)` ❌ |
| 函数返回类型 | `fn add(a: int, b: int) -> int` | — |

已支持：

- 变量：`let x = 1` / `let mut x = 1`（仅 `mut` 可重赋值）。
- 控制流：`if` / `else` / `while` / `break` / `continue`（均为缩进块）。
- 函数：`fn 名称(参数) -> 返回类型`，`return` 返回值。
- 运算符：`+ - * / %`；比较 `== != < > <= >=`；逻辑 `and or not`；字符串拼接 `+`。
- 字符串插值：`"你 {x + 1} 岁"`（`{expr}` 内为任意表达式）。
- 模块：多文件 `import "xxx.ae"`，跨文件函数按名字解析。
- 入口：`fn main`（无参数；`out` 由宿主注入）。

> 设计稿里更多高级语法（能力类型 `cap`、`match`、泛型、`agent`/`tool`/`chain` 原语、`@fuel` 等）见 `语法.txt`，但**当前编译器大多未实现**，使用前请先用 `./leash` 跑通验证。

---

## 内置包

通过 `package file` / `package os` / `package ai` 引入，提供**裸全局函数**（直接用名字调用，不要加包名前缀）：

- **file**：`read` / `write` / `exists` / `delete` / `append`
- **os**：`env` / `exit` / `cwd`
- **ai**：`chat`（无 API key 时返回 mock 结果）

示例（`examples/prime_report.ae` 片段）：

```leash
package file
package os
package ai

fn main
  out(env("HOME"))          // 读取环境变量
  let f = append("/tmp/log.txt", "run\n")   // 追加写入
  out(chat("你好"))          // 调用 AI（无 key 返回 mock）
```

---

## 多文件模块

`import "相对路径.ae"` 会递归收集依赖并去重、检测环。跨文件函数按名字直接调用。参见 `examples/multifile/`：

```leash
// main.ae
import "math.ae"
import "text.ae"
package file

fn main
  out(section("多文件协同报告"))
  let n = 100
  // 直接调用 math.ae / text.ae 中定义的函数
  let primes = primes_upto(n)
  out("1.." + n + " 之间共有 " + count(primes) + " 个质数")
```

`leash-studio` 里按 `Ctrl+R` 运行时会把缓冲区写到**当前文件所在目录**的临时文件再执行 `./leash`，因此相对路径 `import` 能正确解析。

---

## leash-studio 使用

Vim 风格终端编辑器，单文件实现。启动后：增量渲染、语法高亮、当前行青色行号标记、细条光标。

### 快捷键

| 类别 | 按键 |
|------|------|
| 移动 | `h j k l` / 方向键、`w` 词后、`b` 词前、`0` 行首、`$` 行尾、`gg` 文件头、`G` 文件尾 |
| 插入 | `i a I A o O` + `Esc`（`o`/`O` 为向下/向上插入新行） |
| 编辑 | `x` 删字符、`dd` 删行、`yy` 复制行、`p/P` 粘贴、`u` 撤销、`Ctrl+Y` 重做 |
| 搜索 | `/` 向下、`?` 向上、`n` 下一处、`N` 上一处（命中处黄色高亮） |
| 跳转 | 命令模式 `:数字` 跳到第 N 行 |
| 文件 | `Ctrl+S` 保存、`Ctrl+O` 打开（有桌面环境用 zenity/kdialog 弹窗，否则回退文本输入）、`Ctrl+N` 新建（自动补 `.ae` 后缀）、`Ctrl+R` 运行、`Ctrl+Q` 退出、`Ctrl+H` 帮助 |
| 命令模式 | `:w` `:q` `:wq` `:e <文件>` `:e`（重载当前）`:run` `:N`（跳转行） |

### 运行方式

`Ctrl+R` / `:run`：把当前缓冲区写入当前文件目录下的临时 `.leash_run_<pid>.ae`，再执行 `./leash`（这样相对 `import` 可解析），运行后删除临时文件。

### 输入文件名

- 打开 / 新建文件时若未带扩展名，**自动补上 `.ae`**（已有 `.ae` 或其它后缀则不改动）。
- 图形弹窗（zenity/kdialog）不可用或返回空时，自动回退到文本输入提示。

---

## 目录结构

```
Makefile              构建脚本（编译器 + studio）
语法.txt              语法设计稿（参考，非实现清单）
AGENTS.md             AI 编程助手工作约定
docs/tutorial-cn.md   完整中文教程（3287 行）
src/                  编译器源码
  frontend/           lexer / parser
  checker/            typecheck
  codegen/            compiler（字节码）
  vm/                 虚拟机
  host/               宿主能力（file/os/ai 等）
  main.cpp            入口 + 多文件 assemble()
studio/src/minivim.cpp  终端编辑器（leash-studio）
examples/             示例程序（hello / prime_report / multifile / stdlib / langchain 等）
```

---

## 深入学习

- **完整教程**：`docs/tutorial-cn.md`（从前言、快速开始到能力系统、内置包、AI 编程与综合示例）。
- **语法设计稿**：`语法.txt`（能力类型、`agent`/`tool`/`chain` 原语等未来特性）。
- **示例**：`examples/` 下可直接 `./leash examples/<名称>.ae` 运行。

---

> 约定：与本仓库交互、提交信息、文档一律使用中文。
