# crt-transition — 过渡期链接用 C 运行时（非自举源码）

这两个 `.c` 文件不是自举编译器的一部分；它们只是把回归/记分牌产出的目标码
链接成可执行文件时所需的过渡期外部符号实现（`native_regression.py
--runtime`、`native_bootstrap.sh RT_OBJS`）：

    cc -c -o zanstubs.o zanstubs.c
    cc -c -o zanhost.o  zanhost.c
    cc -o zanc zanc.o zanstubs.o zanhost.o -lSystem

## Zan 运行时第一批（runtime_core.zan）

`runtime_core.zan` 用 Zan 重新实现了 zanstubs.c 的第一批符号：
`zan_monotonic_ns` / `zan_monotonic_us` / `zan_sha256` / `zan_sha512`。
它由原生自举编译器（ngen）编译成对象，再用 `localize_syms.py` 把 API 面
之外的符号本地化——ngen 会把整套运行时帮助函数以全局符号发射进每个
对象，不本地化则与程序对象撞符号。产出恰好导出这 4 个符号：

    SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh

链接面（`-DZAN_RT_CORE_ZAN` 把这批符号从 C 侧编出，其余照旧）：

    python3 scripts/native_regression.py --seed "$SEED" \
      --runtime "crt-transition/runtime_core.o crt-transition/zanstubs_rest.o" \
      tests/selfhost/native_rt_core.zan          # 第一批端到端（FIPS 180-4 已知答案）

39 项 kernel 回归同样接受 `--runtime "crt-transition/runtime_core.o
crt-transition/zanstubs_rest.o"`（Zan 对象供 sha/monotonic 符号）。

约束与已知代价（详见 `runtime_core.zan` 头注）：不 using stdlib（Span 是
binder 内建，libc 直调）；数值一律 long + `>>>` + 掩码；每次调用为 Span
视图泄漏 2×16 字节（视图按设计短命不回收）。`Crc32` 仍在 C 侧：导出名
`_Crc32` 无法由 `_类名_方法名` mangle 表达，需 ngen 侧换名（如
`_zan_crc32`）后才能收编。

- `zanstubs.c`：Zan 运行时原语（Alloc/Free/Copy/…/Crc32）、
  `zan_monotonic_ns/us`、Win 代码页 API、`zan_audio_*` 桩、
  `zan_thread_*`/`zan_atomic_int_*`/`zan_shared_table_*` 同步族
  （语义对照 oracle `src/runtime/rt_sync.c`；shared_table 为单进程诚实
  移植，跨进程面 OsHandle/Attach 诚实失败）、`zan_eh_tls_state`（每线程
  EH 状态块）、`zan_io_*` 套接字/解析器族 + `zan_monitor_*`（语义对照
  oracle `src/runtime/rt_io.c`/`rt_sync.c`：send 的 -1/-2 分类与 SIGPIPE
  惰性忽略、resolve_sa/resolve_all 的 v4 优先与 stride-32 去重、
  sockaddr_is_safe 的内嵌 v4 分类、connect_sa 非阻塞+select 截止；
  close_notify 因车道无 reactor 为 no-op）。语义无歧义的用 libc 实现；
  语义未移植的路径 abort()（宁可炸也别静默错）。
- `zanhost.c`：被拉取 stdlib 声明的 `zan_file_*`/`zan_embed_*`/
  `zan_mmap_*`/`zan_pkg_fopen` 族。编译器本体只用 `zan_pkg_fopen` +
  libc（ReadAllText/WriteAllText/ListNames）；其余成员的存在只是为了
  让拉进来的 stdlib 方法体通过链接。embed/包系统/mmap 在自举场景无调用，
  一律安全桩。注意：本文件是 2026-09-17 /tmp 清空后按符号需求重建的，
  与更早的失传版本不保证逐行一致；功能以 39/39 回归 + bootstrap 定点
  + parity sweep 为准。

由本编译器产出的程序另引用 `_zan_ext_*`/`_zan_dt_*`/`_zan_dir_*` 帮助
函数族（见 src/selfhost/ngen_host.zan）——那些由 ngen_host 直接生成
机器码到目标文件里，不在这里。
