#!/usr/bin/env bash
# Linux userspace lane: run Zan fixtures in a real Linux guest.
#
#   SEED=/path/to/stage2 scripts/linux_vehicle.sh [fixture...]
#
# Default fixtures: kernel1 native_extern native_string_ops
#                   native_float_shapes native_varargs_elf
#                   native_digests native_memwide
#                   native_rt_core native_sync
#
# Pipeline per fixture: zanc (ZAN_TARGET=aarch64-linux) -> ELF object ->
# ld.lld static link with the localized Zan runtime (ELF) and musl
# libgcc — ZERO project C since 第十四批 retired zanlinuxshims.c (the
# -D ZAN_RT_CORE_ZAN remainder objects are empty and no longer linked) ->
# a custom Alpine initramfs -> qemu-system-aarch64 -M virt boot -> serial
# output compared byte-for-byte with tests/selfhost/<fixture>.out.
#
# Downloads (Alpine kernel, minirootfs, musl, gcc for libgcc.a) are
# cached under build/linux-vehicle/dl on first use and need network.
# The guest uses stty -onlcr so the serial line carries the program's
# bytes exactly; init prints "prog exit=N" and powers off.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/linux-vehicle"
DL="$BUILD/dl"
SYSROOT="$BUILD/sysroot"
WORK="$BUILD/work"
TESTS="$ROOT/tests/selfhost"
ALPINE="https://dl-cdn.alpinelinux.org/alpine/v3.20"
REL="$ALPINE/releases/aarch64"
MUSL_VER="1.2.5-r3"
GCC_VER="13.2.1_git20240309-r1"
FIXTURES=("$@")
if (( ${#FIXTURES[@]} == 0 )); then
  FIXTURES=(kernel1 kernel2 kernel3 kernel4 kernel5 kernel6 kernel7 kernel8
            kernel9 kernel10 kernel11 kernel12 kernel13 kernel14 kernel15
            kernel16 kernel17 kernel18 kernel19 kernel20 kernel21 kernel22
            kernel23 kernel24 kernel25 kernel26 kernel27
            native_extern native_string_ops native_float_shapes
            native_varargs_elf native_digests native_memwide
            native_rt_core native_sync native_varargs fileinfoex_mmap
            dict_growth dict_minimal host_args_bounds json_ignore_ok
            list_string_search native_async_basic native_async_detach
            native_async_locals native_async_throw native_dict_out_address
            native_dict_packed_out native_lexical native_local_frame
            native_spans native_int_fieldinit native_int_narrow
            native_int_narrow2 native_numeric_runtime native_fnptr
            native_generic_overload_fit native_float_return_arg
            native_cstring_read)
fi

# 第十五批起 SKIP 清单为空:native_varargs 的 st_mode 偏移与 open 标志
# 已改为按目标的 #if 双臂,单一 golden 同时覆盖 darwin 与 guest。

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] ||
  fail 'linux_vehicle requires macOS arm64 (qemu runs aarch64 guests).'

# ---- seed compiler ----
if [[ -z "${SEED:-}" ]]; then
  for cand in $(ls -t "$ROOT"/build/native-bootstrap/run.*/stage2 2>/dev/null); do
    if [[ -x "$cand" ]]; then SEED="$cand"; break; fi
  done
fi
[[ -n "${SEED:-}" && -x "$SEED" ]] ||
  fail 'no SEED compiler; set SEED=<stage2-or-equivalent> or run native_bootstrap.sh first.'
SEED="$(cd "$(dirname "$SEED")" && pwd)/$(basename "$SEED")"

CLANG="${CLANG:-$(command -v clang || true)}"
if [[ -z "$CLANG" ]]; then
  for c in /Users/qq/.homebrew/opt/llvm/bin/clang /opt/homebrew/opt/llvm/bin/clang; do
    [[ -x "$c" ]] && CLANG="$c" && break
  done
fi
[[ -x "${CLANG:-}" ]] || fail 'clang not found (set CLANG=/path/to/clang).'
LLD="${LLD:-$(command -v ld.lld || true)}"
if [[ -z "$LLD" && -x "$(dirname "$(dirname "$CLANG")")/bin/ld.lld" ]]; then
  LLD="$(dirname "$(dirname "$CLANG")")/bin/ld.lld"
fi
[[ -x "${LLD:-}" ]] || fail 'ld.lld not found (set LLD=/path/to/ld.lld).'
QEMU="${QEMU:-$(command -v qemu-system-aarch64 || true)}"
[[ -x "${QEMU:-}" ]] || fail 'qemu-system-aarch64 not found (set QEMU=...).'
[[ -x /usr/bin/python3 ]] || fail 'python3 unavailable (needed by elf_localize).'

mkdir -p "$DL" "$SYSROOT" "$WORK"

# ---- artifacts (cached) ----
fetch() { # fetch <url> <dest>
  [[ -s "$2" ]] && return 0
  curl -s --max-time 500 -o "$2" "$1" || fail "download failed: $1"
}
fetch "$REL/netboot/vmlinuz-virt" "$DL/vmlinuz-virt"
fetch "$REL/alpine-minirootfs-3.20.9-aarch64.tar.gz" "$DL/minirootfs.tar.gz"
fetch "$ALPINE/main/aarch64/musl-$MUSL_VER.apk" "$DL/musl.apk"
fetch "$ALPINE/main/aarch64/musl-dev-$MUSL_VER.apk" "$DL/musl-dev.apk"
fetch "$ALPINE/main/aarch64/gcc-$GCC_VER.apk" "$DL/gcc.apk"
[[ -f "$SYSROOT/usr/lib/libc.a" ]] || tar -xzf "$DL/musl.apk" -C "$SYSROOT"
[[ -f "$SYSROOT/usr/include/stdio.h" ]] || tar -xzf "$DL/musl-dev.apk" -C "$SYSROOT"
[[ -f "$SYSROOT/usr/lib/gcc/aarch64-alpine-linux-musl/13.2.1/libgcc.a" ]] ||
  tar -xzf "$DL/gcc.apk" -C "$SYSROOT" ./usr/lib/gcc 2>/dev/null || true
GCCDIR="$SYSROOT/usr/lib/gcc/aarch64-alpine-linux-musl/13.2.1"
[[ -f "$GCCDIR/libgcc.a" ]] || fail 'libgcc.a missing after extraction'
[[ -f "$DL/rootfs-template.tar.gz" ]] || {
  rm -rf "$WORK/rootfs-template"
  mkdir -p "$WORK/rootfs-template"
  tar -xzf "$DL/minirootfs.tar.gz" -C "$WORK/rootfs-template"
  cat > "$WORK/rootfs-template/init" <<'EOF'
#!/bin/sh
/bin/busybox mount -t proc proc /proc 2>/dev/null
/bin/busybox mount -t devtmpfs devtmpfs /dev 2>/dev/null
# POSIX shm_open 落在 /dev/shm/<name>:minirootfs 无此目录,mmap 族全部
# ENOENT(第十六批探针 errno=2 实证),须自挂 tmpfs。
/bin/busybox mkdir -p /dev/shm 2>/dev/null
/bin/busybox mount -t tmpfs -o mode=777 shm /dev/shm 2>/dev/null
/bin/busybox ip link set lo up 2>/dev/null || /bin/busybox ifconfig lo up 2>/dev/null
/bin/busybox stty -onlcr 2>/dev/null
# getenv("PATH") 等环境面与 darwin 车道对齐:内核给 init 的环境近乎空,
# 不导出则 native_cstring_read 的 p.Length>0 在 guest 里翻 false。
export PATH=/bin:/usr/bin
/prog </dev/null 2>/dev/null
echo "prog exit=$?"
/bin/busybox poweroff -f
EOF
  chmod +x "$WORK/rootfs-template/init"
  (cd "$WORK/rootfs-template" && find . -print0 |
    cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$DL/rootfs-template.tar.gz")
}

# ---- runtime objects (rebuilt when the seed changes) ----
SEEDHASH="$(python3 -c "import hashlib,sys;print(hashlib.sha256(open('$SEED','rb').read()).hexdigest()[:12])")"
RTTAG="rt-$SEEDHASH"
if [[ ! -f "$WORK/$RTTAG.stamp" ]]; then
  echo "building ELF runtime objects (seed $SEEDHASH)..."
  rm -rf "$WORK/$RTTAG"
  mkdir -p "$WORK/$RTTAG"
  O="$WORK/$RTTAG"
  ZAN_TARGET=aarch64-linux "$SEED" "$O/runtime_core.elf.o" \
    "$ROOT/crt-transition/runtime_core.zan"
  KEEPS="$(python3 - "$ROOT/crt-transition/build_zan_core.sh" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
seg = s[s.index('localize_syms.py'):s.index('--weaken')]
print(' '.join(sorted(set(re.findall(r'_zan_[a-z0-9_]+', seg)))))
PY
)"
  python3 "$ROOT/scripts/elf_localize.py" "$O/runtime_core.elf.o" $KEEPS \
    --weaken zan_embed_has zan_embed_read zan_embed_bytes zan_embed_list
  touch "$WORK/$RTTAG.stamp"
else
  O="$WORK/$RTTAG"
fi

printf 'SEED: %s\nclang: %s\nld.lld: %s\nqemu: %s\nwork: %s\n' \
  "$SEED" "$CLANG" "$LLD" "$QEMU" "$WORK"

# ---- per fixture ----
failures=0
run_one() {
  local name="$1" rc=0
  printf '%s: ' "$name"

  ZAN_TARGET=aarch64-linux "$SEED" "$WORK/$name.elf.o" "$TESTS/$name.zan" ||
    { echo 'FAIL (compile)'; return 1; }
  "$LLD" -m aarch64linux -static "$SYSROOT/usr/lib/crt1.o" \
    "$WORK/$name.elf.o" "$O/runtime_core.elf.o" \
    -L"$SYSROOT/usr/lib" -L"$GCCDIR" -lc -lgcc \
    -o "$WORK/$name.elf" || { echo 'FAIL (link)'; return 1; }

  rm -rf "$WORK/guest"
  mkdir -p "$WORK/guest"
  tar -xzf "$DL/rootfs-template.tar.gz" -C "$WORK/guest"
  cp "$WORK/$name.elf" "$WORK/guest/prog"
  (cd "$WORK/guest" && find . -print0 |
    cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$WORK/$name.cpio.gz")

  ( "$QEMU" -M virt -cpu max -m 256M -nographic \
      -kernel "$DL/vmlinuz-virt" -initrd "$WORK/$name.cpio.gz" \
      -append "console=ttyAMA0 panic=-1 rdinit=/init quiet" \
      </dev/null >"$WORK/$name.boot" 2>&1 & \
    qpid=$!; for _ in $(seq 1 120); do sleep 1; kill -0 $qpid 2>/dev/null || break; done; \
    kill -9 $qpid 2>/dev/null; wait $qpid 2>/dev/null ) || rc=1

  grep -a -v 'prog exit=\|reboot:' "$WORK/$name.boot" > "$WORK/$name.got"
  grep -aq 'prog exit=0' "$WORK/$name.boot" ||
    { echo 'FAIL (guest exit nonzero)'; return 1; }
  if diff -q "$TESTS/$name.out" "$WORK/$name.got" >/dev/null 2>&1; then
    echo 'PASS'
  else
    echo 'FAIL (output diff)'
    diff "$TESTS/$name.out" "$WORK/$name.got" | head -10 >&2 || true
    rc=1
  fi
  return $rc
}

echo 'Linux userspace lane (compile -> localize/link -> guest boot -> diff):'
for name in "${FIXTURES[@]}"; do
  run_one "$name" || failures=$((failures + 1))
done
if (( failures > 0 )); then
  echo "$failures FAILURES"
  exit 1
fi
echo 'all green'
