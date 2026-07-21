#!/bin/bash
# Leash 安全测试套 — 运行全部安全验证
cd "$(dirname "$0")/.."
LEASH="./leash"
PASS=0; FAIL=0
pass() { PASS=$((PASS+1)); echo "  [PASS] $1"; }
fail() { FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }

mk() { mktemp /tmp/leash_test_XXXX.ae; }

echo "============================================"
echo " Leash 安全测试套"
echo "============================================"

# ===== 1. 集成回归 =====
echo ""; echo "=== 1. 集成/回归测试 ==="
for ex in examples/hello.ae examples/stdlib_demo.ae examples/langchain_framework.ae \
          examples/agent/main.ae examples/general.ae examples/prime_report.ae \
          examples/stdlib.ae examples/multifile/main.ae; do
  $LEASH "$ex" &>/dev/null && pass "$ex" || fail "$ex"
done

# ===== 2. 编译期能力门控 =====
echo ""; echo "=== 2. 编译期能力门控 ==="
reject() {
  local f=$(mk)
  echo -e "$1" > "$f"
  $LEASH "$f" &>/dev/null && fail "$2 (本应拒绝)" || pass "$2"
  rm -f "$f"
}

reject 'package ai
fn leaky()
  return chat("s","u")
fn main
  out(leaky())' \
  "非 main 调 chat 无 requires model → 编译拒绝"

reject 'package file
fn leaky(p: str)
  return read(p)
fn main
  out(leaky("/x"))' \
  "非 main 调 read 无 requires file → 编译拒绝"

reject 'package file
fn leaky(p: str)
  return write(p,"x")
fn main
  out(leaky("/x"))' \
  "非 main 调 write 无 requires file → 编译拒绝"

reject 'package time
fn leaky()
  return now()
fn main
  out(leaky())' \
  "非 main 调 now 无 requires time → 编译拒绝"

reject 'package os
fn leaky()
  return env("HOME")
fn main
  out(leaky())' \
  "非 main 调 env 无 requires os → 编译拒绝"

reject 'package http
fn leaky()
  return get("http://x")
fn main
  out(leaky())' \
  "非 main 调 get 无 requires net → 编译拒绝"

reject 'package crypto
fn leaky()
  return sha256("x")
fn main
  out(leaky())' \
  "非 main 调 sha256 无 requires crypto → 编译拒绝"

reject 'package random
fn leaky()
  return int(0,1)
fn main
  out(leaky())' \
  "非 main 调 int 无 requires random → 编译拒绝"

# 正向：有 requires 时编译通过
accept() {
  local f=$(mk)
  echo -e "$1" > "$f"
  $LEASH "$f" &>/dev/null && pass "$2" || fail "$2: $($LEASH "$f" 2>&1 | tail -1)"
  rm -f "$f"
}
echo "--- 正向：有 requires 时编译通过 ---"
accept 'package ai
fn ok() requires model
  return chat("s","u")
fn main requires model
  out(ok())' \
  "有 requires model 时 chat 正常编译"

# 先创建临时文件
echo "test" > /tmp/leash_read_test.txt
accept 'package file
fn ok(p: str) requires file
  return read(p)
fn main requires file
  out(ok("/tmp/leash_read_test.txt"))' \
  "有 requires file 时 read 正常编译"
rm -f /tmp/leash_read_test.txt

# ===== 3. agent/chain/rag =====
echo ""; echo "=== 3. agent/chain/rag 关键字真实解析 ==="
run() {
  local f=$(mk)
  echo -e "$1" > "$f"
  $LEASH "$f" &>/dev/null && pass "$2" || fail "$2: $($LEASH "$f" 2>&1 | tail -1)"
  rm -f "$f"
}

run 'agent hello(n: str)
  return "hi " + n
fn main
  out(hello("test"))' \
  "agent 关键字作为真实函数"

run 'chain hello(n: str)
  return "hello " + n
fn main
  out(hello("test"))' \
  "chain 关键字作为真实函数"

run 'rag search(q: str)
  return "搜寻: " + q
fn main
  out(search("x"))' \
  "rag 关键字作为真实函数"

run '@max_steps(5)
agent bounded()
  return "ok"
fn main
  out(bounded())' \
  "@max_steps 注解可解析"

# ===== 4. 运行时安全 =====
echo ""; echo "=== 4. 运行时安全 ==="

# 路径穿越
run 'import "toolx"
package file
fn check_path(p: str) -> str requires file time
  return toolx_dispatch("read_file", p)
fn main requires file time
  let r = check_path("/etc/passwd")
  if str_contains(r, "[安全拒绝]")
    out("PASS")
  else
    out("FAIL: " + r)' \
  "路径穿越拒绝 /etc/passwd"

run 'import "toolx"
package file
fn check_path(p: str) -> str requires file time
  return toolx_dispatch("read_file", p)
fn main requires file time
  let r = check_path("../../secret")
  if str_contains(r, "[安全拒绝]")
    out("PASS")
  else
    out("FAIL: " + r)' \
  "路径穿越拒绝 ../../secret"

# 递归深度
echo "--- 递归深度 ---"
check_recurse() {
  local d=$1 f=$(mk)
  echo -e "fn r(n: int)\n  if n > 0\n    r(n-1)\nfn main\n  r($d)\n  out(\"done\")" > "$f"
  if $LEASH "$f" &>/dev/null; then
    [ $d -le 200 ] && pass "递归深度 $d 正常通过" || fail "递归深度 $d 本应拒绝"
  else
    [ $d -gt 200 ] && pass "递归深度 $d 被拒绝 ✓" || fail "递归深度 $d 异常拒绝: $($LEASH "$f" 2>&1 | tail -1)"
  fi
  rm -f "$f"
}
check_recurse 50
check_recurse 250

# ===== 5. @fuel / @deterministic =====
echo ""; echo "=== 5. @fuel / @deterministic ==="

f=$(mk)
echo -e 'package time\n@deterministic\nfn main\n  out(now())' > "$f"
$LEASH "$f" 2>&1 | grep -q "禁止非确定源" && pass "@deterministic 拒绝 now ✓" || fail "@deterministic 未生效"
rm -f "$f"

f=$(mk)
echo -e '@fuel(3)\nfn main\n  let i=0\n  while i<10\n    i=i+1\n  out("done")' > "$f"
$LEASH "$f" 2>&1 | grep -q "燃料耗尽" && pass "@fuel(3) 燃料耗尽 ✓" || fail "@fuel(3) 未触发"
rm -f "$f"

# ===== 汇总 =====
echo ""; echo "===================================="
echo " 结果: 通过 $PASS / $((PASS+FAIL))"
echo "===================================="
[ $FAIL -gt 0 ] && exit 1 || exit 0
