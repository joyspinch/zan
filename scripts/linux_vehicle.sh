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
# ld.lld static link with the localized Zan runtime (ELF), the localized
# C remainder (zanstubs_elf.o, b41: timer/gate/CO/embed-decode/app_dir
# faces from the oracle C sources — darwin-lane mirror) and musl
# libgcc -> a custom Alpine initramfs -> qemu-system-aarch64 -M virt
# boot -> serial output compared byte-for-byte with
# tests/selfhost/<fixture>.out.
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
# ZAN_VEHICLE_TESTS 可把车道指向别的 .zan/.out 同名目录(批二十三的
# conformance 全集 sweep 即用它指到 zan-lang/tests/conformance);
# 缺省仍是 tests/selfhost,门禁行为不变。
TESTS="${ZAN_VEHICLE_TESTS:-$ROOT/tests/selfhost}"
ALPINE="https://dl-cdn.alpinelinux.org/alpine/v3.20"
REL="$ALPINE/releases/aarch64"
MUSL_VER="1.2.5-r3"
GCC_VER="13.2.1_git20240309-r1"
OSSL_VER="3.3.7-r0"
UODBC_VER="2.3.12-r0"
SQLITE_VER="3.45.3-r3"
LIBPQ_VER="16.14-r0"
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
            native_cstring_read
            # b45:socket 族收编(tests/selfhost 内已固化双车道绿档):
            # http/redis 客户端 + 代理链 + 并发压力服务器 + TLS。
            http_client_redirect http_client_binary http_client_timeout
            http_client_cookies redis_pool redis_tls
            http_forwarder_keepalive http_forwarder_stream
            http_forwarder_tunnel http_server_stress http_framing
            http_parser_hardening http_smuggling http_upload_bytes
            http_bytes_redirect http_chunk_len_overflow)
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
# openssl 静态库：stdlib 的 TLS 族在 ELF 车道链接时缺 EVP_*/BIO_*（第二十三
# 批 154 个 link-fail 的根因）。归档是惰性的——用不到的 fixture 不会因此
# 膨胀。镜像若升版导致 404，车道照旧（只是那批继续 link-fail），不 fail。
if [[ ! -s "$DL/openssl-libs-static.apk" ]]; then
  curl -s --max-time 500 -o "$DL/openssl-libs-static.apk" \
    "$ALPINE/main/aarch64/openssl-libs-static-$OSSL_VER.apk" || true
fi
if [[ -s "$DL/openssl-libs-static.apk" && ! -f "$SYSROOT/usr/lib/libcrypto.a" ]]; then
  tar -xzf "$DL/openssl-libs-static.apk" -C "$SYSROOT" 2>/dev/null || true
fi
# unixODBC 同款(批二十二在 darwin 车道加 -lodbc;DB 族 fixture 的
# SQLAllocHandle 类在 guest 静态车道同样缺):归档惰性,无用不膨胀。
if [[ ! -s "$DL/unixodbc-static.apk" ]]; then
  curl -s --max-time 500 -o "$DL/unixodbc-static.apk" \
    "$ALPINE/main/aarch64/unixodbc-static-$UODBC_VER.apk" || true
fi
if [[ -s "$DL/unixodbc-static.apk" && ! -f "$SYSROOT/usr/lib/libodbc.a" ]]; then
  tar -xzf "$DL/unixodbc-static.apk" -C "$SYSROOT" 2>/dev/null || true
fi
# SQLite 静态库同理(orm/sqlite 族直接引 sqlite3_* C API)。
if [[ ! -s "$DL/sqlite-static.apk" ]]; then
  curl -s --max-time 500 -o "$DL/sqlite-static.apk" \
    "$ALPINE/main/aarch64/sqlite-static-$SQLITE_VER.apk" || true
fi
if [[ -s "$DL/sqlite-static.apk" && ! -f "$SYSROOT/usr/lib/libsqlite3.a" ]]; then
  tar -xzf "$DL/sqlite-static.apk" -C "$SYSROOT" 2>/dev/null || true
fi
# libpq 静态库(pg 族直接引 PQ* C API;连带 pgcommon/pgport)。
if [[ ! -s "$DL/libpq-dev.apk" ]]; then
  curl -s --max-time 500 -o "$DL/libpq-dev.apk" \
    "$ALPINE/main/aarch64/libpq-dev-$LIBPQ_VER.apk" || true
fi
if [[ -s "$DL/libpq-dev.apk" && ! -f "$SYSROOT/usr/lib/libpq.a" ]]; then
  tar -xzf "$DL/libpq-dev.apk" -C "$SYSROOT" 2>/dev/null || true
fi
# zan_gui 桩:oracle 不发布 linux GUI 运行时(toolchain/linux-arm64 无
# zanrt_gui.o),而 chart/几何族 fixture 只是链接期携带 zan_gui_* 外呼、
# 纯几何路径从不执行。桩符号全零返回;真做渲染的 fixture 会因此分歧,
# 留在 GUI-driver 类。符号清单=第二十四批 82 个 GUI 类 fixture 的未定义
# 并集(121 个);stdlib 面扩大时用 nm -u 重新生成 scripts/zan_gui_stubs.c。
"$CLANG" --target=aarch64-linux-musl --sysroot="$SYSROOT" -c \
  "$ROOT/scripts/zan_gui_stubs.c" -o "$SYSROOT/usr/lib/zan_gui_stubs.o" 2>/dev/null || true
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
# 金档是带 argv 跑出来的(如 main_args = alpha "beta gamma"):程序名
# 不进 string[](D19 契约),args 文件一行一参,/bin/prog 逐行重建 "$@"。
while IFS= read -r zanarg; do
  set -- "$@" "$zanarg"
done < /args
/bin/prog "$@" </dev/null 2>/dev/null
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
  # b41:ELF C remainder(zanstubs.c 可移植子集:rt_timer.c + gate/CO 单
  # 线程驱动 + miniz/inflate + app_dir/exe_dir_into + rt 诊断面;audio 面
  # darwin-only 已在源内护栏)。keep 表 = Zan 车道已导出六面
  # (dir_list_into/thread_detach/io_set_nonblocking/io_connect_sa_start/
  # io_pump/io_pump_timeout)之外的全部 zan_/swoole_ 面,miniz 内部符号
  # 不在 keep 即自动收局部;五件 ngen 每程序烘焙物按 b38 契约弱化。
  ELFK="swoole_timer_after swoole_timer_clear swoole_timer_clear_all
swoole_timer_info swoole_timer_list_at swoole_timer_list_count
swoole_timer_stats swoole_timer_tick
zan_timer_after zan_timer_cancel_delay zan_timer_clear zan_timer_clear_all
zan_timer_delay zan_timer_dispatch_due zan_timer_info zan_timer_list_at
zan_timer_list_count zan_timer_next_timeout zan_timer_now_ms
zan_timer_pending zan_timer_runtime_reset zan_timer_saturating_due
zan_timer_set_ready_hook zan_timer_stats zan_timer_tick
zan_async_cfg_io_shards zan_async_cfg_sync_fast zan_async_cfg_workers
zan_async_set_io_shards zan_async_set_sync_fast zan_async_set_workers
zan_co_live_add zan_co_live_count zan_co_live_del zan_co_live_has
zan_co_live_reset
zan_gate_new zan_gate_park zan_gate_signal zan_gate_free
zan_co_sched_init zan_co_ready zan_co_delay zan_co_pending
zan_co_sched_run_until zan_co_sched_run __zan_co_frame_free
__zan_eh_release
zan_embed_decode zan_embed_rawlen
zan_exe_dir_into zan_file_app_dir
zan_rt_fatal zan_rt_set_fatal_handler
zan_rt_guard_fail2 zan_rt_soft_note zan_rt_soft_note2 zan_rt_soft_note3
zan_rt_soft_scratch zan_utf8_argv"
  "$CLANG" --target=aarch64-linux-musl --sysroot="$SYSROOT" \
    -DZAN_RT_CORE_ZAN \
    -I/Users/qq/Desktop/zanlang/zan-lang/src/common \
    -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
    -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
    -c -o "$O/zanstubs_elf.o" "$ROOT/crt-transition/zanstubs.c"
  python3 "$ROOT/scripts/elf_localize.py" "$O/zanstubs_elf.o" $ELFK \
    --weaken zan_rt_dbl_parse zan_rt_dbl_str zan_rt_guard_fail3 \
    zan_rt_set_strict zan_rt_soft_is_hard
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
  OSSL_LIBS=''
  [[ -f "$SYSROOT/usr/lib/libssl.a" ]] && OSSL_LIBS='-lssl -lcrypto'
  [[ -f "$SYSROOT/usr/lib/libodbc.a" ]] && OSSL_LIBS="$OSSL_LIBS -lodbc"
  [[ -f "$SYSROOT/usr/lib/libsqlite3.a" ]] && OSSL_LIBS="$OSSL_LIBS -lsqlite3"
  [[ -f "$SYSROOT/usr/lib/libpq.a" ]] &&
    OSSL_LIBS="$OSSL_LIBS -lpq -lpgcommon -lpgport"
  GUI_O=''
  [[ -f "$SYSROOT/usr/lib/zan_gui_stubs.o" ]] && GUI_O="$SYSROOT/usr/lib/zan_gui_stubs.o"
  # b41:C remainder 回归 ELF 车道(timer/gate/CO/embed-decode/app_dir 面,
  # 与 darwin 车道同构;此前第十四批"零项目 C"只对当时的面成立)。
  CREST=''
  [[ -f "$O/zanstubs_elf.o" ]] && CREST="$O/zanstubs_elf.o"
  "$LLD" -m aarch64linux -static "$SYSROOT/usr/lib/crt1.o" \
    "$WORK/$name.elf.o" "$O/runtime_core.elf.o" $CREST $GUI_O \
    -L"$SYSROOT/usr/lib" -L"$GCCDIR" -lc -lgcc $OSSL_LIBS \
    -o "$WORK/$name.elf" || { echo 'FAIL (link)'; return 1; }

  # 每次调用独立 guest 目录：并行分片 sweep 时互不踩(第二十四批起)。
  local gdir="$WORK/guest.$$"
  rm -rf "$gdir"
  mkdir -p "$gdir"
  tar -xzf "$DL/rootfs-template.tar.gz" -C "$gdir"
  # 放 /bin/prog 而非 /prog:Skin.ExeDir() 按最后一个 '/' 切,exe 在
  # 根目录时切出 ""(第二十五批 file_embed_subdir 首检查 0 实证);
  # darwin 车道的 exe 一直在深目录,这里对齐。
  mkdir -p "$gdir/bin"
  cp "$WORK/$name.elf" "$gdir/bin/prog"
  # 每 fixture 的 argv(golden 生成时的调用参数,逐一镜像):
  case "$name" in
    main_args) printf 'alpha\nbeta gamma\n' > "$gdir/args" ;;
    environment_unicode_args) printf '\xe4\xb8\xad\xe6\x96\x87\n\xe5\x8f\x82\xe6\x95\xb0\n' > "$gdir/args" ;;
    *) : > "$gdir/args" ;;
  esac
  # 数据文件镜像:fixture 以 zan-lang 仓库根为 cwd 用相对路径读数据
  # (examples/gui_charts/options/*.json、tests/conformance/data_*.json),
  # golden 即在该 cwd 下产出。仓库整树太大(gui_charts 28M),按 fixture
  # 源里的引用逐个拷进 guest 根(第二十五批起)。
  local zroot="$(cd "$TESTS/../.." && pwd)"
  grep -hoE '"[^"]+\.(json|txt|csv|bin)"' "$TESTS/$name.zan" 2>/dev/null |
    tr -d '"' | sort -u |
    while IFS= read -r rel; do
      [[ -f "$zroot/$rel" ]] || continue
      mkdir -p "$gdir/$(dirname "$rel")"
      cp "$zroot/$rel" "$gdir/$rel"
    done
  (cd "$gdir" && find . -print0 |
    cpio -0 -o -H newc 2>/dev/null | gzip -1 > "$WORK/$name.cpio.gz")
  rm -rf "$gdir"

  # 大 ELF 自动升内存:writer 侧 a64 数组膨胀可产出 25MB+ 静态二进制
  # (sdk_wechat 78MB),256M guest 解 initramfs 直接 write error(内核
  # panic "No working init");>30MB 的 fixture 用 1G。可用 QEMU_MEM 覆盖。
  local qmem="${QEMU_MEM:-256M}"
  local szf
  szf="$(wc -c < "$WORK/$name.elf")"
  (( szf > 30000000 )) && qmem=1G
  ( "$QEMU" -M virt -cpu max -m "$qmem" -nographic \
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
