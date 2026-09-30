#!/usr/bin/env bash
# Landing-gate probes for seed rebuilds. The bootstrap fixed point cannot
# see float bugs (src/selfhost carries no float literals and no double-arg
# methods), which is how the v18o-17 iterations shipped a lr-less shim that
# hung the self-compile and a GenNulWrap d0 read that boxed stale garbage
# into every `double?` literal. This script runs the shapes the fixed point
# cannot: float-bearing compiles, a runnable nullable-literal program, and
# a two-object C-ABI probe through the export shims.
#
#   SEED=/path/to/zanc [RT_OBJS="a.o b.o c.o"] bash scripts/gate_probes.sh
#
# RT_OBJS defaults to the crt-transition trio. Exits nonzero on any failure;
# artifacts under a fresh mktemp dir, printed on exit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${SEED:?Set SEED=/path/to/native-compiler-seed}"
SEED="$(cd "$(dirname "$SEED")" && pwd)/$(basename "$SEED")"
RT="${RT_OBJS:-$ROOT/crt-transition/runtime_core.o $ROOT/crt-transition/zanstubs_rest.o $ROOT/crt-transition/zanhost_rest.o}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/zan-gate-probes.XXXXXX")"
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SDKVER="$(xcrun --show-sdk-version)"
fail() { printf 'FAIL: %s\n' "$*" >&2; printf 'artifacts: %s\n' "$WORK" >&2; exit 1; }
RTS=()
for obj in $RT; do RTS+=("$(cd "$(dirname "$obj")" && pwd)/$(basename "$obj")"); done

link() { # link <out> <obj>...
  /usr/bin/ld -arch arm64 -e _main -platform_version macos 11.0 "$SDKVER" \
    -syslibroot "$SDKROOT" -undefined dynamic_lookup -o "$1" "${@:2}" "${RTS[@]}" \
    -lSystem 2>/dev/null
}

cd "$WORK"

# ---- shape 1: float-bearing compiles must not crash the emitter ----
printf 'class B { public static double F(double x){return x;} static void Main(){} }\n' > shim.zan
printf 'class C { static void Main(){ double d = 0.25; } }\n' > dbl.zan
printf 'class C { static void Main(){ double d = 2.0; } }\n' > dbl2.zan
printf 'class C { static void Main(){ float f = 1.5f; } }\n' > flt.zan
for p in shim dbl dbl2 flt; do
  "$SEED" "$p.o" "$p.zan" >/dev/null 2>&1 || fail "$p.zan compile exit $?"
done
echo "compile probes: 4/4"

# ---- shape 2: nullable double literal must survive the wrap ----
cat > nul.zan <<'ZAN'
using System;
class N {
    static void Main() {
        double? e = 1.5;
        Console.WriteLine(e ?? 0.0);
    }
}
ZAN
"$SEED" nul.o nul.zan >/dev/null 2>&1 || fail "nul.zan compile exit $?"
link nul nul.o
OUT="$(./nul)"
[ "$OUT" = "1.5" ] || fail "nullable literal: expected '1.5', got '$OUT'"
echo "nullable probe: 1.5 ok"

# ---- shape 3: two objects, C-ABI shim entry + literals in the ----
# ---- non-Main object (the class of bug shims and strd blobs fix) ----
cat > lib.zan <<'ZAN'
class Lib {
    public static double F(double x) { return x * 2.0 + 0.5; }
    static void Main() { }
}
ZAN
cat > app.zan <<'ZAN'
using System;
class App {
    [DllImport("crt", EntryPoint="Lib_F")] public static extern double LibF(double x);
    static void Main() {
        Console.WriteLine(LibF(1.5));
        Console.WriteLine(0.25);
    }
}
ZAN
"$SEED" lib.o lib.zan >/dev/null 2>&1 || fail "lib.zan compile exit $?"
python3 "$ROOT/crt-transition/localize_syms.py" lib.o _Lib_F >/dev/null
"$SEED" app.o app.zan >/dev/null 2>&1 || fail "app.zan compile exit $?"
link twoobj app.o lib.o
OUT="$(./twoobj)"
[ "$OUT" = "3.5
0.25" ] || fail "two-object C-ABI probe: expected '3.5/0.25', got '$(echo "$OUT" | tr '\n' '/')'"
echo "two-object C-ABI probe: 3.5/0.25 ok"

printf 'gate probes: all green; artifacts: %s\n' "$WORK"
