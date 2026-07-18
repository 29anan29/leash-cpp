# AGENTS.md

本文件供 AI 编程助手（opencode 等）在本项目中工作时参考。

> **语言约定：与本仓库交互、回复用户时一律使用中文。**

---

## 1. 项目简介

- **Aegis**：一门面向 AI Agent 的安全脚本语言（能力门控、最小特权、确定性）。
- **aegis-studio**：配套 Vim 风格终端编辑器（自带语法高亮、运行、多文件支持、图形文件选择框）。
- 设计目标与完整语法见 `语法.txt`（**设计稿**，部分特性尚未实现，以“当前实现”为准）。

## 2. 目录结构

```
Makefile              构建脚本（编译器 + studio）
语法.txt              语法设计稿（参考，非实现清单）
src/                  编译器源码
  frontend/           lexer / parser
  checker/            typecheck
  codegen/            compiler（字节码）
  vm/                 虚拟机
  host/               宿主能力（file/os/ai 等）
  main.cpp            入口 + 多文件 assemble()
studio/src/minivim.cpp  终端编辑器（aegis-studio）
examples/             示例程序（hello / prime_report / multifile / stdlib / langchain 等）
```

## 3. 构建

```bash
make              # 同时构建 aegis 与 aegis-studio
make run          # 运行 ./aegis examples/hello.ae
make run-ide      # 运行 ./aegis-studio
make clean        # 清除产物
```

也可单独编译 studio（无依赖，单文件）：

```bash
g++ -std=c++17 -O2 -o aegis-studio studio/src/minivim.cpp
```

编译器产物为 `./aegis`；studio 产物为 `./aegis-studio`。

## 4. 当前可实现的语言子集（重要，避免踩坑）

**下面这些“看起来像 C/Python”的写法在该语言里是无效的**，已被实测验证：

| 易错点 | 正确写法 | 错误写法 |
|--------|----------|----------|
| 代码块 | **缩进**（同 Python，无 `{}`） | `fn f() { ... }` ❌ |
| 打印 | `out("字符串")` | `out -> "..."` ❌ / `out "..."` ❌ |
| 注释 | `// 行注释` | `# 注释` ❌（`#` 不是注释） |
| 多文件引用 | `import "math.ae"`（带引号的文件路径） | `import math`（设计稿写法，未实现）❌ |
| `package` 暴露的函数 | **裸全局名**，如 `append(...)` / `env(...)` / `chat(...)` | `file.append(...)` ❌ |
| 函数返回类型 | `fn add(a: int, b: int) -> int` | — |

已支持的特性：

- 变量：`let x = 1` / `let mut x = 1`（仅 `mut` 可重赋值）。
- 控制流：`if` / `else` / `while` / `break` / `continue`（均为缩进块）。
- 函数：`fn 名称(参数) -> 返回类型`，`return` 返回值。
- 运算符：`+ - * / %`，比较 `== != < > <= >=`，逻辑 `and or not`，字符串拼接 `+`。
- 字符串插值：`"你 {x + 1} 岁"`（`{expr}` 内为任意表达式）。
- 模块：多文件 `import "xxx.ae"`，递归收集 + 去重 + 环检测；跨文件函数按名字解析。
- 内置包（通过 `package file` / `package os` / `package ai` 引入，提供裸函数）：
  - `file`：`read` / `write` / `exists` / `delete` / `append`
  - `os`：`env` / `exit` / `cwd`
  - `ai`：`chat`（无 API key 时返回 mock 结果）
- 入口：`fn main`（无参数；`out`/`in` 由宿主注入）。

> 更多高级语法（cap 能力类型、`match`、泛型、`agent`/`tool`/`chain` 原语、`@fuel` 等）见 `语法.txt`，但**当前编译器大多未实现**，使用前请先用 `./aegis` 跑通验证。

## 5. 测试 / 验证方式

- **编译器**：直接跑示例。例如 `./aegis examples/hello.ae`、`./aegis examples/multifile/main.ae`（多文件）。
- **studio 端到端**：用 Python `pyte` 伪终端模拟验证编辑器行为（搜索、跳转、横向滚动、运行输出等）。示例脚本思路：
  ```python
  import pty, pyte, fcntl, termios, struct, os, select, time
  # fork 出 ./aegis-studio <file>，发送按键字节（如 b'\x12'=Ctrl+R），
  # 用 pyte.Screen 还原屏幕，断言输出内容。
  ```
  注意：`:q` / 任意键返回等会清空屏幕，断言应在“退出前”或“未退出”的帧上做。

## 6. Studio 编辑器（aegis-studio）速查

源码：`studio/src/minivim.cpp`。主要特性：

- 增量渲染、`OPOST|ONLCR` 终端设置、运行/帮助用 alt-screen（不擦除编辑器）。
- 快捷键：
  - 移动：`h j k l` / 方向键、`w` 词后、`b` 词前、`0` 行首、`$` 行尾、`gg` 文件头、`G` 文件尾。
  - 插入：`i a I A o O` + `Esc`；`o`/`O` 为向下/向上插入新行。
  - 编辑：`x` 删字符、`dd` 删行、`yy` 复制行、`p/P` 粘贴、`u` 撤销、`Ctrl+Y` 重做。
  - 搜索：`/` 向下、`?` 向上、`n` 下一处、`N` 上一处（命中处黄色高亮）。
  - 跳转：命令模式 `:数字` 跳到第 N 行。
  - 文件：`Ctrl+S` 保存、`Ctrl+O` 打开（有桌面环境用 zenity/kdialog 弹窗，否则回退文本输入）、`Ctrl+N` 新建、`Ctrl+R` 运行、`Ctrl+Q` 退出、`Ctrl+H` 帮助。
  - 命令模式：`:w` `:q` `:wq` `:e <文件>` `:e`（重载当前）`:run`。
- **运行方式**：`Ctrl+R` / `:run` 会把当前缓冲区写到“当前文件所在目录”的临时 `.aegis_run_<pid>.ae` 再执行 `./aegis`（这样相对路径 `import` 才能解析）。临时文件运行后删除。
- 当前行高亮用 `BG_DARK`；长行通过 `colOff` 横向滚动（光标移出可见区时自动滚动）。
- 语法高亮在 `paintLine` 中统一处理 token 配色 + 搜索高亮 + 当前行底色。

## 7. 修改约定

- 编辑器改动集中在 `studio/src/minivim.cpp`（单文件，改动后 `make` 或单独 `g++` 重新编译即可）。
- 编译器改动后务必 `make`，并跑 `examples/hello.ae` 与 `examples/multifile/main.ae` 回归。
- 涉及 `import`/多文件时，注意 `assemble()` 在 `src/main.cpp`，循环依赖与重复导入已在其中处理。
- 回复、提交信息、注释中与用户沟通的部分使用中文。
