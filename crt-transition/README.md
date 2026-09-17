# crt-transition — 过渡期链接用 C 运行时（非自举源码）

这两个 `.c` 文件不是自举编译器的一部分；它们只是把回归/记分牌产出的目标码
链接成可执行文件时所需的过渡期外部符号实现（`native_regression.py
--runtime`、`native_bootstrap.sh RT_OBJS`）：

    cc -c -o zanstubs.o zanstubs.c
    cc -c -o zanhost.o  zanhost.c
    cc -o zanc zanc.o zanstubs.o zanhost.o -lSystem

- `zanstubs.c`：Zan 运行时原语（Alloc/Free/Copy/…/Crc32）、
  `zan_monotonic_ns`、Win 代码页 API、`zan_audio_*` 桩。语义无歧义的用
  libc 实现；语义未移植的路径 abort()（宁可炸也别静默错）。
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
