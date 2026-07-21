# AGENTS.md

本文件供 AI 编程助手（opencode 等）在本项目中工作时参考。

> **语言约定：与本仓库交互、回复用户时一律使用中文。**

---

## 1. 项目简介

- **Leash**：一门面向 AI Agent 的安全脚本语言（能力门控、最小特权、确定性）。
- **leash-studio**：配套 Vim 风格终端编辑器（自带语法高亮、运行、多文件支持、图形文件选择框）。
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
studio/src/minivim.cpp  终端编辑器（leash-studio）
examples/             示例程序（hello / prime_report / multifile / stdlib / langchain 等）
```

## 3. 构建

```bash
make              # 同时构建 leash 与 leash-studio
make run          # 运行 ./leash examples/hello.ae
make run-ide      # 运行 ./leash-studio
make clean        # 清除产物
```

也可单独编译 studio（无依赖，单文件）：

```bash
g++ -std=c++17 -O2 -o leash-studio studio/src/minivim.cpp
```

编译器产物为 `./leash`；studio 产物为 `./leash-studio`。

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
| 保留字 | `out`/`in`/`match`/`cap` 不能作变量或参数名 | 用作变量名报「let 后需要变量名」 |
| 宿主 IO | 仅 `fn main` 内能调用 `out` / `in` | 库函数内调用 `out` 报「函数 … 缺少能力: io」；正确做法：库函数返回值，由 `main` 打印 |
| 映射赋值左值 | `m[k]` 的键 `k` 必须是简单变量或字面量 | `m[a[i]] = v` 报「无法解析的表达式，遇到: =」 |
| 字符串插值 | 源字面量里的 `{` 须写成 `\{` | `"a{b}"` 被当作插值，遇到未定义变量报错 |
| 正则量词 | `re` 包当前 `{n}` 量词支持不完善 | `ismatch("^a{3}$","aaa")` 返回 `false`；IPv4 等改用纯 Leash 手动校验 |
| 空 `if` 体 | `if` 体不能仅由注释构成 | 报「需要缩进块 (INDENT)」 |

已支持的特性：

- 能力与安全（§10/§14，已实现）：`@fuel(N)` 步数上限、`@timeout(Nms|Ns)` 超时熔断、`@deterministic` 禁止非确定源（`now`/`random`/`sleep` 在开启时抛错）；`requires cap` 声明函数所需能力，`cap.method(args)` 经能力句柄调用（无 `requires` 编译期拒绝），宿主经 `provideCap` 注入 `io`/`file`/`net`/`model`/`time` 等能力句柄，全部能力调用记入审计日志。
- 变量：`let x = 1` / `let mut x = 1`（仅 `mut` 可重赋值）。
- 控制流：`if` / `else` / `while` / `break` / `continue`（均为缩进块）；`for k, v in m` 遍历映射、`for i, e in xs` 遍历列表/字符串。
- 函数：`fn 名称(参数) -> 返回类型`，`return` 返回值；**支持递归**（如 `fib_n`、目录树遍历）。
- 运算符：`+ - * / %`，比较 `== != < > <= >=`，逻辑 `and or not`，字符串拼接 `+`。
  - **字符串不支持关系比较**（`c >= "0"` 报错），需用 `ord()` 转整数后比较。
  - **字符串拼接 `+` 不支持跨行续行**：`let s = "a" +` 后换行 `"b"` 会被当作新语句而报「无法解析的表达式」。长字符串请写在同一行，或拆成多条 `s = s + ...` 赋值。
- 字符串插值：`"你 {x + 1} 岁"`（`{expr}` 内为任意表达式）；切片 `s[1:3]` / `s[i:j]` 支持，边界可为算术表达式或 `len()`。
- 模块：多文件 `import "xxx.ae"`，递归收集 + 去重 + 环检测；跨文件函数按名字解析。导入搜索路径含入口目录 `lib/`、`LEASH_LIB` 环境变量、可执行文件目录 `lib/` 及新增的 `cwd/lib`（仓库根目录 `lib/`），因此放在仓库根的示例可直接 `./leash x.ae` 解析 `lib/` 包；`examples/` 下示例可用 `LEASH_LIB=lib ./leash examples/x.ae`。
- 内置包（通过 `package X` 引入，提供**裸全局名**函数）：
  - `core`：自动注入；`sqrt` / `pow` / `floor` / `ceil` / `round` / `abs`（数值），以及 `parse`/`stringify` 形式的 JSON 辅助等。
  - `file`：`read` / `write` / `exists` / `delete` / `append`（1 参数；`read(path)` 而非 `read(path, default)`）。
  - `os`：`env` / `exit` / `cwd` / `is_tty`（`env` 仅 1 参数：`env(name)`；`is_tty()` 无参，返回 stdin 是否为交互终端，用于区分交互/非交互运行）。
  - `ai`：`chat`（无 API key 时返回 mock 结果）。
  - `time`：`now` / `sleep`。
  - `random`：`int(lo,hi)` / `float()` / `choice(xs)`。
  - `crypto`：`sha256`。
  - `http`：`get(url)` / `post(url, body)`（裸名 `get`/`post`，**不是** `http_get`）。
  - `re`：`ismatch` / `find`（裸名；`{n}` 量词支持不完善，见上表）。
  - `json`：自动注入；`parse` / `stringify` 为裸名（**不是** `json_parse`）。
- 标准库：`lib/` 下现有 **116 个纯 Leash 包 + 原生包**（如 `math`/`string`/`list`/`dictx`/`slug`/`statsx`/`csvx`/`toml`/`yaml`/`httpx`/`promptx`/`embedx` 等），详见 `教程/标准库详解.md`。
- **LangChain 风格框架包**（对标 LangChain 四大支柱，纯 Leash 实现，未配 api_key 时走 mock 仍可跑通）：
  - `promptx`：`promptx_fill(tpl, data)` / `promptx_vars(tpl)` / `promptx_render(mem)`。**模板占位符用 `<<name>>`**（不用 `{}`，以避开 Leash 字符串插值与映射字面量冲突）。
  - `memory`：`memory_new` / `memory_add` / `memory_get` / `memory_last` / `memory_window`（滑动窗口记忆，消息格式 `{"role","content"}`）。
  - `outputx`：`outputx_strip_fence` / `outputx_json` / `outputx_after` / `outputx_between`（剥离 ``` 围栏、抽取首个 JSON、取分隔符之后的文本/区间）。
  - `llmx`（`package ai,time`）：`llmx_new` / `llmx_invoke`（多轮拼进 user）/ `llmx_chat` / `llmx_preset(openai|claude|ollama|deepseek)` / `llmx_use`（切全局变量）。
  - `chainx`：`chainx_llm`（单链：填模板→调模型）/ `chainx_sequential`（顺序链，上一链输出作下一链输入变量）。
  - `toolx`（`package file,time`）：`toolx_new` / `toolx_param` / `toolx_find` / `toolx_describe` / `toolx_dispatch`（内置 `calculator`/`get_time`/`read_file`，自定义区在 `toolx_dispatch` 内扩展）。
  - `agent`（`package ai`）：`agent_parse`（识别 Final Answer / Action）/ `agent_run`（ReAct 主循环）/ `agent_ask`（便捷入口，空记忆、最多 5 步）。
  - `textx`：`textx_wrap` / `textx_truncate` / `textx_center` / `textx_split`（按词数切分，用于 RAG 建索引）。
  - `embedx`：`embedx_vector` / `embedx_cosine`（词袋嵌入 + 余弦相似度，离线可用，无需模型服务）。
  - `vector`：`vector_new` / `vector_add` / `vector_search(store, query, k)`（轻量向量库，top-k 检索）。
  - `retriever`：`retriever_build(docs)` / `retriever_search(store, query, k)`（构建于 vector 之上的检索器）。
  - `ragx`（`package ai`）：`rag_index(docs)` / `rag_query(cfg, query, store, k)`（检索增强生成：检索→拼提示→chat）。
  - 综合示例：`examples/langchain_framework.ae`（四大支柱一键演示，mock 可跑）；配合 `examples/leash.json` 注入 `model`/`api_key`/`base_url`。
- 入口：`fn main`（无参数；`out`/`in` 由宿主注入）。

> 更多高级语法（cap 能力类型、`match`、泛型、`agent`/`tool`/`chain` 原语、`@fuel` 等）见 `语法.txt`，但**当前编译器大多未实现**，使用前请先用 `./leash` 跑通验证。

## 5. 测试 / 验证方式

- **编译器**：直接跑示例。例如 `./leash examples/hello.ae`、`./leash examples/multifile/main.ae`（多文件）、`./leash examples/agent/main.ae`（多文件 Agent：TUI / 模型配置 / 多轮对话，未配置 api_key 时 chat 走 mock）。
- **多文件示例约定**：`import "xxx.ae"` 按入口文件目录解析兄弟文件（函数全局按名共享）；原生包用 `package file/os/ai/time` 在入口声明后全局可用；`leash.json` 经 `import "leash.json"` 注入 `model`/`api_key`/`base_url` 供 `chat()` 使用；**这些注入的全局变量可在运行时重新赋值（如 `model = "claude-3-opus"`），`chat()` 即时生效，无需重启**。
- **交互 vs 非交互**：`os.is_tty()` 可判断 stdin 是否为交互终端。示例 `examples/agent/main.ae` 在**非交互**（如 leash-studio 的 `Ctrl+R`、管道空输入、重定向）时自动进入【演示模式】跑一段示例对话后退出；在**真实终端**里才是完整交互式 TUI。studio 的「运行」不分配 TTY，故从 `Ctrl+R` 会看到演示而非可键入的界面——这是预期行为。
- **studio 端到端**：用 Python `pyte` 伪终端模拟验证编辑器行为（搜索、跳转、横向滚动、运行输出等）。示例脚本思路：
  ```python
  import pty, pyte, fcntl, termios, struct, os, select, time
  # fork 出 ./leash-studio <file>，发送按键字节（如 b'\x12'=Ctrl+R），
  # 用 pyte.Screen 还原屏幕，断言输出内容。
  ```
  注意：`:q` / 任意键返回等会清空屏幕，断言应在“退出前”或“未退出”的帧上做。

## 6. Studio 编辑器（leash-studio）速查

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
- **运行方式**：`Ctrl+R` / `:run` 会把当前缓冲区写到“当前文件所在目录”的临时 `.leash_run_<pid>.ae` 再执行 `./leash`（这样相对路径 `import` 才能解析）。临时文件运行后删除。
- 当前行用**青色行号**标记（不在整行铺灰底，以保证可读性）；光标为细条（`ESC[6 q`）。
- 长行通过 `colOff` 横向滚动（光标移出可见区时自动滚动）。
- 语法高亮在 `paintLine` 中统一处理 token 配色 + 搜索高亮 + 当前行底色。

## 7. 修改约定

- 编辑器改动集中在 `studio/src/minivim.cpp`（单文件，改动后 `make` 或单独 `g++` 重新编译即可）。
- 编译器改动后务必 `make`，并跑 `examples/hello.ae` 与 `examples/multifile/main.ae` 回归。
- 涉及 `import`/多文件时，注意 `assemble()` 在 `src/main.cpp`，循环依赖与重复导入已在其中处理。
- 回复、提交信息、注释中与用户沟通的部分使用中文。
