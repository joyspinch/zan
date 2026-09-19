#!/usr/bin/env bash
# Cross-target execution + structural verification for the self-host compiler.
#
# Per fixture, two lanes:
#   ELF64 (execution): compile with ZAN_TARGET=aarch64-linux using a
#     stdlib-free copy of the compiler (Dictionary/List/Console are compiler
#     built-ins, so the fixtures need no stdlib and the ELF objects stay
#     small), validate the relocatable with scripts/elfcheck.py, link it
#     against the freestanding semihosting stub (scripts/crossboot/stub.c +
#     stub.ld) into a static kernel image, boot it under qemu-system-aarch64
#     -M virt, and compare output byte-for-byte with tests/selfhost/<n>.out.
#   PE-COFF (structural): compile the same source with an .obj output name
#     (Windows ARM64 writer, no env needed) and validate with coffcheck.py.
#
# Usage:
#   scripts/crossboot/cross_boot_check.sh [fixture-name ...]
# With no arguments the five verified fixtures run.
#
# Environment:
#   SEED      compiler binary with the native CLI (default: newest
#             build/native-bootstrap/run.*/stage2)
#   CLANG     aarch64-capable clang (default: clang from PATH, then the
#             Homebrew llvm keg)
#   LLD       ld.lld binary (default: lld dir sibling of CLANG or PATH)
#   QEMU      qemu-system-aarch64 (default: PATH)
#
# Exit status is nonzero if any fixture fails either lane.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TESTS="$ROOT/tests/selfhost"
DEFAULT_FIXTURES="dict_minimal dict_growth native_string_ops list_string_search host_args_bounds"

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] ||
  fail 'cross_boot_check requires macOS arm64 (qemu runs aarch64 guests).'

if (( $# > 0 )); then FIXTURES=("$@"); else FIXTURES=($DEFAULT_FIXTURES); fi

# ---- toolchain ----
CLANG="${CLANG:-$(command -v clang || true)}"
if [[ -z "$CLANG" ]]; then
  for c in /Users/qq/.homebrew/opt/llvm/bin/clang /opt/homebrew/opt/llvm/bin/clang; do
    [[ -x "$c" ]] && CLANG="$c" && break
  done
fi
[[ -x "${CLANG:-}" ]] || fail 'clang not found (set CLANG=/path/to/clang).'
LLD_BIN_DIR="$(dirname "$(dirname "$CLANG")")/bin"
LLD="${LLD:-$(command -v ld.lld || true)}"
if [[ -z "$LLD" && -x "$LLD_BIN_DIR/ld.lld" ]]; then LLD="$LLD_BIN_DIR/ld.lld"; fi
[[ -x "${LLD:-}" ]] || fail 'ld.lld not found (set LLD=/path/to/ld.lld).'
QEMU="${QEMU:-$(command -v qemu-system-aarch64 || true)}"
[[ -x "${QEMU:-}" ]] || fail 'qemu-system-aarch64 not found (set QEMU=...).'
[[ -x /usr/bin/python3 ]] || fail 'python3 unavailable (needed by elfcheck).'

# ---- seed compiler ----
if [[ -z "${SEED:-}" ]]; then
  for cand in $(ls -t "$ROOT"/build/native-bootstrap/run.*/stage2 2>/dev/null); do
    if [[ -x "$cand" ]]; then SEED="$cand"; break; fi
  done
fi
[[ -n "${SEED:-}" && -x "$SEED" ]] ||
  fail 'no SEED compiler; set SEED=<stage2-or-equivalent> or run native_bootstrap.sh first.'
SEED="$(cd "$(dirname "$SEED")" && pwd)/$(basename "$SEED")"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/zan-crossboot.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# A compiler copy with NO stdlib beside it: fixtures use only built-ins.
mkdir "$WORK/bin"
cp "$SEED" "$WORK/bin/zanc"
cp "$ROOT/scripts/crossboot/stub.c" "$ROOT/scripts/crossboot/stub.ld" "$WORK/"

printf 'SEED: %s\nclang: %s\nld.lld: %s\nqemu: %s\nwork: %s\n' \
  "$SEED" "$CLANG" "$LLD" "$QEMU" "$WORK"

"$CLANG" -target aarch64-none-linux-gnu -ffreestanding -fno-builtin \
  -fno-stack-protector -O1 -std=c11 -c "$WORK/stub.c" -o "$WORK/stub.o"

boot_one() {
  local name="$1" rc=0
  printf '%s: ' "$name"

  (cd "$WORK" && ZAN_TARGET=aarch64-linux ./bin/zanc "$name.elf.o" \
     "$TESTS/$name.zan") || { echo 'FAIL (compile)'; return 1; }

  python3 "$ROOT/scripts/elfcheck.py" "$WORK/$name.elf.o" >/dev/null ||
    { echo 'FAIL (elfcheck)'; return 1; }

  "$LLD" -m aarch64linux -static -nostdlib -T "$WORK/stub.ld" \
    "$WORK/$name.elf.o" "$WORK/stub.o" -o "$WORK/$name.elf" ||
    { echo 'FAIL (link)'; return 1; }

  # Watchdog: a healthy boot exits well under a second; kill at 15s.
  ( "$QEMU" -M virt -cpu max -nographic \
      -semihosting-config enable=on,target=native \
      -kernel "$WORK/$name.elf" </dev/null >"$WORK/$name.boot" 2>&1 & \
    qpid=$!; for _ in $(seq 1 15); do sleep 1; kill -0 $qpid 2>/dev/null || break; done; \
    kill -9 $qpid 2>/dev/null; wait $qpid 2>/dev/null ) || rc=1

  if diff -q "$TESTS/$name.out" "$WORK/$name.boot" >/dev/null 2>&1; then
    echo 'PASS'
  else
    echo 'FAIL (output diff)'
    diff "$TESTS/$name.out" "$WORK/$name.boot" | head -10 >&2 || true
    rc=1
  fi
  return $rc
}

coff_one() {
  local name="$1"
  printf '%s: ' "$name"
  (cd "$WORK" && ./bin/zanc "$name.obj" "$TESTS/$name.zan") ||
    { echo 'FAIL (compile)'; return 1; }
  python3 "$ROOT/scripts/coffcheck.py" "$WORK/$name.obj" >/dev/null 2>&1 ||
    { echo 'FAIL (coffcheck)'; return 1; }
  echo 'PASS'
}

printf '%s\n' 'ELF64 lane (compile -> elfcheck -> link -> qemu boot -> diff):'
FAILED=0
for f in "${FIXTURES[@]}"; do
  [[ -f "$TESTS/$f.zan" && -f "$TESTS/$f.out" ]] ||
    fail "fixture or golden missing: $TESTS/$f.{zan,out}"
  boot_one "$f" || FAILED=1
done

printf '%s\n' 'PE-COFF lane (compile .obj -> coffcheck):'
for f in "${FIXTURES[@]}"; do
  coff_one "$f" || FAILED=1
done
exit $FAILED
