#!/usr/bin/env bash
# Build the Zan-implemented runtime object (runtime_core.zan, batches 1-3)
# with the native bootstrap compiler and localize its non-API symbols.
#
#   SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh [out.o]
#
# Produces an object exporting exactly the 39 allowlisted zan_* symbols
# (monotonic/sha256/sha512/alloc/free/crc32 + pkg_fopen/file_*/embed_*/
# mmap_*). Link it with the -DZAN_RT_CORE_ZAN builds of zanstubs.c and
# zanhost.c wherever those symbols are needed:
#
#   python3 scripts/native_regression.py --seed "$SEED" \
#     --runtime "crt-transition/runtime_core.o crt-transition/zanstubs_rest.o crt-transition/zanhost_rest.o" \
#     tests/selfhost/native_rt_core.zan
#
# The pure-C baseline for A/B regression is built alongside:
#   zanstubs_full.o + zanhost_full.o (both without the define).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${SEED:?Set SEED=/path/to/native-compiler-or-gen1}"
OUT="${1:-$ROOT/crt-transition/runtime_core.o}"

cd "$ROOT"
"$SEED" "$OUT" crt-transition/runtime_core.zan
python3 crt-transition/localize_syms.py "$OUT" \
  _zan_monotonic_ns _zan_monotonic_us _zan_sha256 _zan_sha512 \
  _zan_alloc _zan_free _zan_crc32 \
  _zan_pkg_fopen _zan_file_fopen _zan_file_remove _zan_file_rename \
  _zan_file_read_path \
  _zan_file_open _zan_file_read _zan_file_write _zan_file_seek \
  _zan_file_tell _zan_file_flush _zan_file_close _zan_file_eof \
  _zan_file_length _zan_file_attributes _zan_file_time \
  _zan_file_set_readonly _zan_file_set_time \
  _zan_file_try_lock _zan_file_unlock \
  _zan_mmap_create _zan_mmap_open _zan_mmap_from_file _zan_mmap_map \
  _zan_mmap_unmap _zan_mmap_flush _zan_mmap_close _zan_mmap_unlink \
  _zan_plat_net_interfaces _zan_plat_icmp_ping \
  --weaken \
  _zan_embed_has _zan_embed_read _zan_embed_bytes _zan_embed_list
cc -DZAN_RT_CORE_ZAN -c -o "$ROOT/crt-transition/zanstubs_rest.o" \
  "$ROOT/crt-transition/zanstubs.c"
cc -DZAN_RT_CORE_ZAN -c -o "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanhost.c"
cc -c -o "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanstubs.c"
cc -c -o "$ROOT/crt-transition/zanhost_full.o" "$ROOT/crt-transition/zanhost.c"
printf 'Zan runtime object: %s\nC remainder:        %s %s\nC baseline:         %s %s\n' \
  "$OUT" \
  "$ROOT/crt-transition/zanstubs_rest.o" "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanhost_full.o"
