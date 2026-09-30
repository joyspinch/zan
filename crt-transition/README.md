# crt-transition — 过渡期链接用 C 运行时（非自举源码）

这两个 `.c` 文件不是自举编译器的一部分；它们只是把回归/记分牌产出的目标码
链接成可执行文件时所需的过渡期外部符号实现（`native_regression.py
--runtime`、`native_bootstrap.sh RT_OBJS`）：

    cc -c -o zanstubs.o zanstubs.c
    cc -c -o zanhost.o  zanhost.c
    cc -o zanc zanc.o zanstubs.o zanhost.o -lSystem

## Zan 运行时第一批~第四批(runtime_core.zan)

`runtime_core.zan` 用 Zan 重新实现了过渡 C 运行时的四批符号:
`zan_monotonic_ns` / `zan_monotonic_us` / `zan_sha256` / `zan_sha512`(第一批)、
`zan_alloc` / `zan_free` / `zan_crc32`(第二批)、
`zan_pkg_fopen` / `zan_file_fopen` / `remove` / `rename` / `read_path`、
FileStream 八件套(open/read/write/seek/tell/flush/close/eof)、FileInfo 五件套
(length/attributes/time/set_readonly/set_time)、`try_lock` / `unlock`、
`zan_embed_*`(诚实空集)、`zan_mmap_*`(诚实桩)——第三批;
`zan_plat_net_interfaces`(getifaddrs 走查/地址折叠/MAC)与
`zan_plat_icmp_ping`(DGRAM→RAW 回退、poll 状态机)——第四批,
**至此 zanhost.c 在 `-DZAN_RT_CORE_ZAN` 下整体清零**(链接进
`zanhost_rest.o` 的只剩空翻译单元)。
它由原生自举编译器(ngen)编译成对象,再用 `localize_syms.py` 把 API 面
之外的符号本地化——ngen 会把整套运行时帮助函数以全局符号发射进每个
对象,不本地化则与程序对象撞符号。产出恰好导出 37 个强符号 + 4 个弱
符号(embed 族经 `--weaken` 置 N_WEAK_DEF,带内嵌资源的程序对象自带的
强定义在链接时获胜):

    SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh

该脚本同时产出五个对象:Zan 配置用 `runtime_core.o + zanstubs_rest.o +
zanhost_rest.o`(后两者是 `-DZAN_RT_CORE_ZAN` 编出的 C 余量),C 基线用
`zanstubs_full.o + zanhost_full.o`(无定义符,供 A/B 对照)。
Zan 配置链接面:

    python3 scripts/native_regression.py --seed "$SEED" \
      --runtime "crt-transition/runtime_core.o crt-transition/zanstubs_rest.o crt-transition/zanhost_rest.o" \
      tests/selfhost/native_rt_core.zan          # 四批端到端(46 行金标)

39 项 kernel 回归同样接受上述 `--runtime`。两个配置(A/B)都已全量验证:
fixture(46 行金标)、默认 kernel 套件 39/39、自举固定点(含 stage1.o
引用的全部 24 个文件族符号由 Zan 对象供给;第四批后另含 net/ping 两
符号)。第四批的 net 文本还与 C oracle 做过逐字节 A/B(同一台机器
probe 程序直接 dump zan_plat_net_interfaces 返回缓冲,两路 diff 为空)。

第二批的改名机制：stdlib 的 `NativeMemory.Alloc/Free/Crc32` 声明加
`EntryPoint = "zan_alloc" / "zan_free" / "zan_crc32"`——DllImport 的
EntryPoint 就是符号改名机制，无需动编译器。C host（LLVM 车道）对这些
调用直降 libc（`irgen_expr.c`），不受影响。`NativeMemory.Crc32` 的双车道
oracle 语义是**零扩展成 i64 返回**（"123456789" → 3421780262，
见 `irgen_expr.c` 的 `nm_crc32_fn` 末尾 ZExt 与 `ngen.zan` 的 Crc32→long
特例）；fixture 里自带的 `int` 返回 DllImport 变体才打印有符号读数
（-873187034）——两条都钉在 golden 里。

过渡期遗留别名：旧代 seed 是对着改名前的 stdlib 快照编译的，其 stage1
产物仍引用 `_Alloc/_Free/_Crc32`。`zanstubs.c` 里这三个转发别名**不加
守卫**（两个配置都参与链接），等所有钉住的 seed 都晚于本次改名后即可
删除。注意：native 车道的 stdlib
查找是**编译器同目录优先**（`main.zan` 回退 `./stdlib`）——seed 旁边的
历史快照会遮蔽仓库 stdlib，改 stdlib 后必须用新 bootstrap 产出的 seed
重跑回归，否则改动静默不生效。

约束与已知代价(详见 `runtime_core.zan` 头注):不 using stdlib(Span 是
binder 内建,libc 直调);数值一律 long + `>>>` + 掩码;每次调用为 Span
视图泄漏 1~2×16 字节(视图按设计短命不回收);公开 ABI 方法名必须与
libc 导入别名错开(同名会触发重载 mangling 破坏符号契约,如内部的
`cfree` 别名)。第三批追加:stdlib 声明的 string/byte[] 形参在 Zan 侧
一律收 nint(同槽位,隐式 string→nint 已探针验证);struct stat 按
macOS arm64 硬编码(144 字节,mode@4 time@32/48/64 size@96,经
offsetof 验证);embed 族为强符号(ngen 无弱符号发射;native 车道无
embed 烘焙器,gen0 的 embedres.c 不进 native 链接,无人与之争);
try_lock 代际表是进程内 calloc 静态、单线程假设与 C 版一致,锁本体
BSD flock;read_path/embed_list 返回 malloc 的 C 串(C strdup 契约),
该 string 返回 ABI 在托管侧目前是休眠路径(fixture 按字节校验)。
`utimensat` 在部分受限环境拒绝相对路径——金标钉"返回值与回读一致"
而非具体值,任何环境下正确实现均为 true。

第四批追加:`sa_family_t` 是 u8(Darwin),只能读 offset 1——offset 2
起 `sockaddr_in` 是端口、`sockaddr_dl` 是 sdl_index,并读会分别把
AF_LINK 判成假、MAC 列判空。`freeifaddrs` 必须收链表头(`*listp`)而
非持有者指针,否则持有者随后 `free` 即 double free。

**已发现编译器缺口**(ngen,均有探针/反汇编证据,详见
`runtime_core.zan` 头注,本文件全部绕行):
1. 变参调用降级不可用——8 参 DllImport→snprintf 入口寄存器逐个验证
   正确、被调方仍读到压栈槽地址(4/6 参非变参 mmap 实证正常);第二受害
   者是三参 open(open 本是变参函数,mode 由 va_arg 读出):x2 落寄存器
   残留值,创建文件拿到随布局漂移的垃圾模式(file_lock 翻车根因,v18o-13
   根治)——绕行:zanstubs.c 的二参包装 zan_open_creat,变参 libc 函数
   一律不得从 ngen 代码 DllImport;
2. 静态字段持有的指针上 Span 索引写不可靠(probe_va2 写后原缓冲
   仍为零)——模式一律"静态存、用点取新局部";
3. 字符串字面量经数据槽间接寻址、槽由本对象静态初始化填充,第二个
   ngen 对象的 init 不被调用→槽恒 0——runtime 内禁写字面量。


- `zanstubs.c`:gen0 主机原语(Alloc/Free/Copy/…/Crc32 legacy 别名)、
  Win 代码页 API、`zan_audio_*` 桩、`zan_thread_*`/`zan_atomic_int_*`/
  `zan_shared_table_*` 同步族(语义对照 oracle `src/runtime/rt_sync.c`;
  shared_table 为单进程诚实移植,跨进程面 OsHandle/Attach 诚实失败)、
  `zan_eh_tls_state`(每线程 EH 状态块)、`zan_io_*` 套接字/解析器族 +
  `zan_monitor_*`(语义对照 oracle `src/runtime/rt_io.c`/`rt_sync.c`:
  send 的 -1/-2 分类与 SIGPIPE 惰性忽略、resolve_sa/resolve_all 的
  v4 优先与 stride-32 去重、sockaddr_is_safe 的内嵌 v4 分类、
  connect_sa 非阻塞+select 截止;close_notify 因车道无 reactor 为
  no-op)。第一批~第二批符号(zan_monotonic_*/sha256/sha512/alloc/
  free/crc32)已移入 runtime_core.zan,`-DZAN_RT_CORE_ZAN` 时编出;
  语义无歧义的用 libc 实现;语义未移植的路径 abort()(宁可炸也别
  静默错)。
- `zanhost.c`:在 `-DZAN_RT_CORE_ZAN` 下整体清零(文件半边第三批移入
  runtime_core.zan,network 半边第四批移入);无定义符的
  `zanhost_full.o` 仍是 A/B 对照的 C oracle。注意:本文件
  是 2026-09-17 /tmp 清空后按符号需求重建的,与更早的失传版本不保证
  逐行一致;功能以 39/39 回归 + bootstrap 定点 + parity sweep 为准。

由本编译器产出的程序另引用 `_zan_ext_*`/`_zan_dt_*`/`_zan_dir_*` 帮助
函数族（见 src/selfhost/ngen_host.zan）——那些由 ngen_host 直接生成
机器码到目标文件里，不在这里。
