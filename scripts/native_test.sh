#!/usr/bin/env bash
# Native-only compile/link/run test. Retains a fresh log/artifact directory.
# ZANC1=/path/to/native-compiler scripts/native_test.sh prog.zan prog.out
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZANC1="${ZANC1:?Set ZANC1=/path/to/native-compiler}"
SRC="${1:?usage: native_test.sh <prog.zan> <prog.out>}"
GOLDEN="${2:?usage: native_test.sh <prog.zan> <prog.out>}"
exec python3 "$ROOT/scripts/native_regression.py" --seed "$ZANC1" \
  --runtime "${RT_OBJS:-}" --expected "$GOLDEN" "$SRC"
