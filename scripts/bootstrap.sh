#!/usr/bin/env bash
# Self-hosting closure for the zan-selfhost lane.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD="${BUILD:-$ROOT/build}"
ZANC="${ZANC:?Set ZANC=/path/to/zanc (the pinned bootstrap compiler)}"
CLANG="${CLANG:-clang}"
RT_OBJS="${RT_OBJS:-}"
LDFLAGS="${LDFLAGS:--L$ROOT/stdlib-link -lSystem}"
mkdir -p "$BUILD"

SRCS=()
while IFS= read -r name; do
  [[ -n "$name" ]] && SRCS+=("$ROOT/src/selfhost/$name.zan")
done < "$ROOT/scripts/selfhost_sources.txt"

src_hash() {
  for f in "${SRCS[@]}"; do shasum -a 256 "$f"; done
}
hash_text() { printf '%s\n' "$@" | shasum -a 256 | cut -d' ' -f1; }
ZANC_HASH="$(shasum -a 256 "$ZANC" | cut -d' ' -f1)"
CLANG_HASH="$(command -v "$CLANG" | xargs shasum -a 256 | cut -d' ' -f1)"
SRC_HASH="$(src_hash | shasum -a 256 | cut -d' ' -f1)"
GEN1_FP="$(hash_text "$ZANC_HASH" "$SRC_HASH")"
G2_FP="$(hash_text "$GEN1_FP")"
GEN2_FP="$(hash_text "$G2_FP" "$CLANG_HASH" "$RT_OBJS" "$LDFLAGS")"
G3_FP="$(hash_text "$GEN2_FP" "$SRC_HASH")"
need() { [[ ! -e "$1" || ! -f "$2" || "$(<"$2")" != "$3" ]]; }
mark() { printf '%s\n' "$2" > "$1.tmp" && mv -f "$1.tmp" "$1"; }

if need "$BUILD/zanc1" "$BUILD/.gen1.stamp" "$GEN1_FP"; then
  echo "[1/5] gen0 -> gen1"
  tmp="$BUILD/zanc1.tmp.$$"; rm -f "$tmp"
  "$ZANC" "${SRCS[@]}" -o "$tmp" && mv -f "$tmp" "$BUILD/zanc1"
  mark "$BUILD/.gen1.stamp" "$GEN1_FP"
else echo "[1/5] gen0 -> gen1 (cached)"; fi

if need "$BUILD/g2.ll" "$BUILD/.g2.stamp" "$G2_FP"; then
  echo "[2/5] gen1 -> g2.ll"
  tmp="$BUILD/g2.ll.tmp.$$"; rm -f "$tmp"
  "$BUILD/zanc1" "$tmp" "${SRCS[@]}" && mv -f "$tmp" "$BUILD/g2.ll"
  mark "$BUILD/.g2.stamp" "$G2_FP"
else echo "[2/5] gen1 -> g2.ll (cached)"; fi

if need "$BUILD/zanc2" "$BUILD/.gen2.stamp" "$GEN2_FP"; then
  echo "[3/5] clang g2.ll -> gen2"
  tmp="$BUILD/zanc2.tmp.$$"; rm -f "$tmp"
  "$CLANG" "$BUILD/g2.ll" $RT_OBJS -o "$tmp" $LDFLAGS && mv -f "$tmp" "$BUILD/zanc2"
  mark "$BUILD/.gen2.stamp" "$GEN2_FP"
else echo "[3/5] clang g2.ll -> gen2 (cached)"; fi

if need "$BUILD/g3.ll" "$BUILD/.g3.stamp" "$G3_FP"; then
  echo "[4/5] gen2 -> g3.ll"
  tmp="$BUILD/g3.ll.tmp.$$"; rm -f "$tmp"
  "$BUILD/zanc2" "$tmp" "${SRCS[@]}" && mv -f "$tmp" "$BUILD/g3.ll"
  mark "$BUILD/.g3.stamp" "$G3_FP"
else echo "[4/5] gen2 -> g3.ll (cached)"; fi

echo "[5/5] compare g2.ll and g3.ll"
if cmp -s "$BUILD/g2.ll" "$BUILD/g3.ll"; then
  echo "SUCCESS: gen2 == gen3 (byte-identical, $(wc -c < "$BUILD/g2.ll") bytes, $(shasum -a 256 "$BUILD/g2.ll" | cut -d' ' -f1))"
else
  echo "FAILURE: gen2 != g3" >&2
  exit 1
fi
