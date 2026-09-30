#!/usr/bin/env bash
# Build the Zan-implemented batch-1 runtime object (runtime_core.zan) with the
# native bootstrap compiler and localize its non-API symbols.
#
#   SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh [out.o]
#
# Produces an object exporting exactly: _zan_monotonic_ns, _zan_monotonic_us,
# _zan_sha256, _zan_sha512, _zan_alloc, _zan_free, _zan_crc32. Link it with a
# -DZAN_RT_CORE_ZAN build of zanstubs.c (zanstubs_rest.o) wherever those
# symbols are needed:
#
#   python3 scripts/native_regression.py --seed "$SEED" \
#     --runtime "crt-transition/runtime_core.o crt-transition/zanstubs_rest.o" \
#     tests/selfhost/native_rt_core.zan
#
# The companion C side is rebuilt as:
#   cc -DZAN_RT_CORE_ZAN -c -o crt-transition/zanstubs_rest.o crt-transition/zanstubs.c
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${SEED:?Set SEED=/path/to/native-compiler-or-gen1}"
OUT="${1:-$ROOT/crt-transition/runtime_core.o}"

cd "$ROOT"
"$SEED" "$OUT" crt-transition/runtime_core.zan
python3 crt-transition/localize_syms.py "$OUT" \
  _zan_monotonic_ns _zan_monotonic_us _zan_sha256 _zan_sha512 \
  _zan_alloc _zan_free _zan_crc32
cc -DZAN_RT_CORE_ZAN -c -o "$ROOT/crt-transition/zanstubs_rest.o" \
  "$ROOT/crt-transition/zanstubs.c"
cc -c -o "$ROOT/crt-transition/zanhost.o" "$ROOT/crt-transition/zanhost.c"
printf 'Zan runtime object: %s\nC remainder object:  %s\nC host stubs:       %s\n' \
  "$OUT" "$ROOT/crt-transition/zanstubs_rest.o" "$ROOT/crt-transition/zanhost.o"
