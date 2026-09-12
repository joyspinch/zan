#!/usr/bin/env bash
# Native-backend end-to-end test: compile a .zan program to an arm64 object
# with the self-hosted compiler's ngen backend, link it against the platform
# runtime objects, run it, and diff stdout against a golden file.
#
#   ZANC1=<gen1 compiler> RT_OBJS="<runtime .o files>" \
#     scripts/native_test.sh <prog.zan> <prog.out>
#
# Mirrors bootstrap.sh's RT_OBJS contract; CLANG is used for linking.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ZANC1="${ZANC1:?Set ZANC1=/path/to/gen1 (the self-hosted compiler)}"
RT_OBJS="${RT_OBJS:?Set RT_OBJS=<space-separated runtime .o files>}"
CLANG="${CLANG:-clang}"
SRC="${1:?usage: native_test.sh <prog.zan> <prog.out>}"
GOLDEN="${2:?usage: native_test.sh <prog.zan> <prog.out>}"

OBJ="${TMPDIR:-/tmp}/$(basename "${SRC%.zan}").$$.o"   # .o suffix selects the ngen backend
EXE="${OBJ%.o}"

"$ZANC1" "$OBJ" "$SRC"      # output name first, like bootstrap's .ll convention
"$CLANG" "$OBJ" $RT_OBJS -o "$EXE"
"$EXE" > "$OBJ.stdout"

# goldens are compared with trailing whitespace normalized, like run_selfhost.cmake
if ! diff <(sed -e 's/[[:space:]]*$//' "$GOLDEN") <(sed -e 's/[[:space:]]*$//' "$OBJ.stdout") > /dev/null; then
  echo "FAIL: native output mismatch for $SRC" >&2
  echo "--- expected ---" >&2; cat "$GOLDEN" >&2
  echo "--- actual ---" >&2; cat "$OBJ.stdout" >&2
  exit 1
fi
rm -f "$OBJ" "$OBJ.stdout" "$EXE"
echo "OK: native backend output matched for $SRC"
