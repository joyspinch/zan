#!/usr/bin/env bash
# Self-hosting closure for the zan-selfhost lane.
#
#   gen0  = a released/pinned zanc (the C host compiler of zan-lang), located
#           via $ZANC or auto-discovered. This repo does NOT build gen0.
#   gen1  = gen0 compiling src/selfhost/*.zan into a native executable
#   g2.ll = gen1 compiling its own source to LLVM IR text
#   gen2  = clang linking g2.ll into a native executable
#   g3.ll = gen2 compiling the same source to g3.ll
#
# Success criterion (fixed point): g2.ll and g3.ll are byte-identical.
#
# Runtime objects for linking gen2: gen1 links the runtime automatically (zanc
# self-contained linking), but gen2 is linked by clang and needs the platform
# runtime objects. Set RT_OBJS (space-separated) and LDFLAGS if the defaults
# don't match your platform; on macOS the toolchain's libSystem.tbd stub is
# used. Run from the repo root so the `stdlib/` snapshot is discovered.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${BUILD:-$ROOT/build}"
ZANC="${ZANC:?Set ZANC=/path/to/zanc (the pinned bootstrap compiler)}"
CLANG="${CLANG:-clang}"
RT_OBJS="${RT_OBJS:-}"
LDFLAGS="${LDFLAGS:--L$ROOT/stdlib-link -lSystem}"
mkdir -p "$BUILD"

SRCS=(
  "$ROOT/src/selfhost/main.zan"
  "$ROOT/src/selfhost/irgen.zan"
  "$ROOT/src/selfhost/irgen_async.zan"
  "$ROOT/src/selfhost/irgen_stmt.zan"
  "$ROOT/src/selfhost/irgen_expr.zan"
  "$ROOT/src/selfhost/checker.zan"
  "$ROOT/src/selfhost/binder.zan"
  "$ROOT/src/selfhost/diag.zan"
  "$ROOT/src/selfhost/parser.zan"
  "$ROOT/src/selfhost/jsongen.zan"
  "$ROOT/src/selfhost/dbgen.zan"
  "$ROOT/src/selfhost/lexer.zan"
  "$ROOT/src/selfhost/ngen.zan"
  "$ROOT/src/selfhost/ngen_macho.zan"
  "$ROOT/src/selfhost/nio.zan"
  "$ROOT/src/selfhost/ast.zan"
  "$ROOT/src/selfhost/token.zan"
)

echo "[1/5] gen0 -> gen1 (building the self-hosted compiler)"
"$ZANC" "${SRCS[@]}" -o "$BUILD/zanc1"

echo "[2/5] gen1 -> g2.ll (self-compile)"
"$BUILD/zanc1" "$BUILD/g2.ll" "${SRCS[@]}"

echo "[3/5] clang g2.ll -> gen2"
"$CLANG" "$BUILD/g2.ll" $RT_OBJS -o "$BUILD/zanc2" $LDFLAGS

echo "[4/5] gen2 -> g3.ll (self-compile)"
"$BUILD/zanc2" "$BUILD/g3.ll" "${SRCS[@]}"

echo "[5/5] compare g2.ll and g3.ll"
if cmp -s "$BUILD/g2.ll" "$BUILD/g3.ll"; then
  echo "SUCCESS: gen2 == gen3 (byte-identical, $(wc -c < "$BUILD/g2.ll") bytes)"
else
  echo "FAILURE: gen2 != g3" >&2
  exit 1
fi
