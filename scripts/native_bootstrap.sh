#!/usr/bin/env bash
# Native object fixed point, using an externally supplied native compiler/gen1:
#   SEED=/path/to/gen1 RT_OBJS="/path/to/runtime.o ..." scripts/native_bootstrap.sh
# SEED must accept: compiler output.o input1.zan ... (not the gen0 -o CLI).
# No gen0 build, LLVM IR, clang invocation, runtime compilation, or cache here.
# RT_OBJS is an optional whitespace-separated list of prebuilt arm64 runtime
# objects (paths containing whitespace are not supported, as in bootstrap.sh).
# C runtime objects are a TRANSITION: success does NOT mean C has been removed.
# Each invocation keeps its outputs/logs in a fresh BUILD/run.XXXXXX directory;
# existing build directories and artifacts are never removed or reused.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD:-$ROOT/build/native-bootstrap}"
SEED="${SEED:?Set SEED=/path/to/native-compiler-or-existing-gen1}"
RT_OBJS="${RT_OBJS:-}"

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] ||
  fail 'This bootstrap requires macOS arm64.'
[[ -f "$SEED" && -x "$SEED" ]] || fail "SEED is not an executable file: $SEED"
# Resolve caller-relative paths before changing to ROOT for stdlib discovery.
SEED="$(cd "$(dirname "$SEED")" && pwd)/$(basename "$SEED")"
RT_ARGS=()
# Intentional whitespace splitting, without glob expansion or eval.
set -f
for obj in $RT_OBJS; do
  [[ -f "$obj" && -r "$obj" ]] || fail "Runtime object is not readable: $obj"
  [[ "$obj" == *.o ]] || fail "RT_OBJS must contain prebuilt .o files: $obj"
  RT_ARGS+=("$(cd "$(dirname "$obj")" && pwd)/$(basename "$obj")")
done
set +f

[[ -x /usr/bin/ld ]] || fail 'System linker /usr/bin/ld is unavailable.'
SDKROOT="${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path)}"
SDK_VERSION="$(xcrun --sdk "$SDKROOT" --show-sdk-version)"
MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
[[ -f "$SDKROOT/usr/lib/libSystem.tbd" ]] ||
  fail "macOS SDK libSystem not found under $SDKROOT"
SDKROOT="$(cd "$SDKROOT" && pwd)"

mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
RUN="$(mktemp -d "$BUILD/run.XXXXXX")"
printf 'Native bootstrap artifacts and logs: %s\n' "$RUN"
trap 'status=$?; if (( status != 0 )); then printf "FAILED (exit %s); artifacts/logs retained in %s\n" "$status" "$RUN" >&2; fi' EXIT
# main.zan falls back to ./stdlib when none exists beside the compiler.
cd "$ROOT"

SRCS=()
while IFS= read -r name; do
  [[ -n "$name" ]] && SRCS+=("$ROOT/src/selfhost/$name.zan")
done < "$ROOT/scripts/selfhost_sources.txt"
for src in "${SRCS[@]}"; do
  [[ -r "$src" ]] || fail "Selfhost source is not readable: $src"
done
shasum -a 256 "$SEED" > "$RUN/seed.sha256"
shasum -a 256 "${SRCS[@]}" > "$RUN/sources.sha256"
mkdir -p "$RUN/src/selfhost"
SNAP_SRCS=()
for src in "${SRCS[@]}"; do
  snapshot="$RUN/src/selfhost/$(basename "$src")"
  cp "$src" "$snapshot"
  SNAP_SRCS+=("$snapshot")
done
shasum -a 256 "${SRCS[@]}" > "$RUN/sources.after-copy.sha256"
cmp -s "$RUN/sources.sha256" "$RUN/sources.after-copy.sha256" ||
  fail 'Compiler sources changed while taking the snapshot; rerun after edits finish.'
SRCS=("${SNAP_SRCS[@]}")
cp -R "$ROOT/stdlib" "$RUN/stdlib"
if (( ${#RT_ARGS[@]} )); then
  shasum -a 256 "${RT_ARGS[@]}" > "$RUN/runtime.sha256"
fi
cd "$RUN"

printf 'SEED: %s\nSDK: %s (version %s, minimum macOS %s)\n' \
  "$SEED" "$SDKROOT" "$SDK_VERSION" "$MACOSX_DEPLOYMENT_TARGET"
printf '%s\n' 'Scope: native object fixed point without invoking clang; NOT C-removal completion.'
if (( ${#RT_ARGS[@]} )); then
  printf 'Transitional external runtime object: %s\n' "${RT_ARGS[@]}"
else
  printf '%s\n' 'RT_OBJS is empty; unresolved Zan runtime externs will fail linking.'
fi

compile() {
  local compiler="$1" stage="$2"
  # Keep the literal .o suffix: main.zan uses it to select ngen/Nio.WriteBytes.
  "$compiler" "$RUN/$stage.o" "${SRCS[@]}" 2>&1 | tee "$RUN/$stage.compile.log"
  [[ -s "$RUN/$stage.o" ]] || fail "$stage did not produce a nonempty object"
}

link_stage() {
  local stage="$1"
  # The native compiler emits its own runtime and needs only libSystem.
  # RT_OBJS is retained for older seeds; do not supply definitions already
  # emitted by the current backend. Undefined symbols fail the link.
  # The + expansion also supports empty arrays under macOS Bash 3.2 + nounset.
  if /usr/bin/ld -arch arm64 -e _main \
      -platform_version macos "$MACOSX_DEPLOYMENT_TARGET" "$SDK_VERSION" \
      -syslibroot "$SDKROOT" -L"$SDKROOT/usr/lib" \
      -o "$RUN/$stage" "$RUN/$stage.o" \
      ${RT_ARGS[@]+"${RT_ARGS[@]}"} -lSystem 2>&1 | tee "$RUN/$stage.link.log"; then
    [[ -x "$RUN/$stage" ]] || fail "$stage link produced no executable"
  else
    printf 'Link failed: inspect %s/%s.link.log; supply ABI-compatible arm64 RT_OBJS for unresolved Zan externs.\n' "$RUN" "$stage" >&2
    return 1
  fi
}

printf '[1/6] SEED -> stage1.o (%s selfhost sources)\n' "${#SRCS[@]}"
compile "$SEED" stage1
printf '%s\n' '[2/6] system ld -> stage1'
link_stage stage1
printf '%s\n' '[3/6] stage1 -> stage2.o'
compile "$RUN/stage1" stage2
printf '%s\n' '[4/6] system ld -> stage2'
link_stage stage2
printf '%s\n' '[5/6] stage2 -> stage3.o'
compile "$RUN/stage2" stage3
printf '%s\n' '[6/6] compare stage2.o and stage3.o'
if cmp "$RUN/stage2.o" "$RUN/stage3.o" 2>&1 | tee "$RUN/compare.log"; then
  printf 'SUCCESS: stage2.o == stage3.o (byte-identical). Artifacts: %s\n' "$RUN"
  printf '%s\n' 'This proves the native object fixed point only, NOT completion of C removal.'
else
  fail 'stage2.o != stage3.o; native bootstrap fixed point not reached'
fi
