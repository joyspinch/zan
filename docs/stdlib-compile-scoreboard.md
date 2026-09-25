# stdlib 编译通过率记分牌（架构转向后的源驱动工作清单）

日期：2026-09-23 · v15 · 定点见下方 v15 节

## v15 socket om/em 8 例 reactor 真挂起批（2026-09-23，om 6→3、em 11→6，sweep 307→315，检查点 run.7mjxpH）

v12 基线遗留的 4 个 om 超时 + 4 个 em 超时全部转绿。上一批落好的 C
reactor（zt_io_ws 观察表 + zan_io_poll + DNS worker + close 唤醒）这次补
上四处诚实化缺口后真正跑通：

1. **就绪门控**（zt_fd_ready，零超时 pollfd 探测）：recv_co/recv_to_co/
   accept_co 在发起系统调用前先查就绪。接手的 socket 常被调用方留在阻塞
   模式，socket_close_wakes 的阻塞 recv 会冻住整个单线程引擎（sample：
   __recvfrom，Main 永远轮不到 Close）；未就绪一律挂起，不硬试。
2. **死 fd 快失败**（对齐 oracle io_reject_dead_fd）：fd 不存活时
   wait_co 立即交付 0、recv 系交付 0、accept_co 交付 -1，绝不入表。
   关键在 macOS：stdlib 的 CreateTcp6/CreateUdp6 用 Linux 的
   `socket(10,…)`（Darwin AF_INET6=30）必然失败返回 -1，而 poll() 对负
   fd 静默忽略——挂起的 W 观察永不触发（lldb：fd=-1 kind=W，无限
   poll）。ipv6 全程跑在死 fd 上，交付语义逐值对齐后输出与 oracle 完全
   一致：`FAIL reply= rc=-1 got= peer= salen=16`（oracle 自身 v6 连接
   缺陷，但其可观察行为就是规格——rc=0、salen=16、四个空串）。
3. **DNS 自管道 O_NONBLOCK**：poll 侧排水循环"读到不能再读"，阻塞管道
   在最后一个字节后下一次 read 永久冻结（1 次成功 6 次挂起的竞态假象）；
   zt_dns_pipe_ensure 给读端加 O_NONBLOCK，EAGAIN 终止排水。
4. **ResolveAsync 编译器 intrinsic（kind 7 → _zan_resolve_ipv4_co）**：
   stdlib Socket.ResolveAsync 的方法体是 `return await
   Socket.ResolveAsync(hostname);` 自递归占位——oracle 把它降为编译器
   intrinsic（zan_io_resolve_co：空名立即 *out=0，否则 worker 算
   zan_io_resolve_ipv4 交付）。不识别它 async_dns/async_mt 就在调度器里
   无限递归。DNS job 结构加 v4 标志共用同一 worker/管道/互斥锁机制。

发射侧配套：_zan_co_sched_run_until 在 timers_due 与定时器睡眠之间插
BLExtern("_zan_io_poll") 钩子；_zan_co_ready 前奏用函数指针
zan_co_ready_hook 自注册（弱符号在 macOS ld 静态链接下不可用，见 v12
注记；ngen_macho ExternSyms 相应收集未定义非 GOT 数据 strRefs）。

**验证**：39/39；crossboot ELF 5/5 + PE-COFF 5/5；正式自举 run.7mjxpH
stage2.o==stage3.o；qa15（TcpClient 全回显，v10 偏差保持修复）、qa16
（延迟门控 recv 双形态字节一致）探针复验；定向 8/8（async_asocket_echo/
async_socket_async_echo/ipv6/accept_after_close/async_concurrent_echo/
async_dns/async_mt/socket_close_wakes）；chart 剖面零 delta（2138，v15
全量重跑后确认）；全量 sweep 零回归（pass 315 / ncf 170 / nlf 0 / om 3 /
em 6 / mne 11 /
rcf 119，total 624）。

**下一批**（按既定顺序）：委托捕获/7 参形状（6 chart 错）→ Html
Dictionary<Action>（3）→ FindNotAnyOf（2）→ Task.Delay-arg/ToStr-arity
级联（含 v14 记录的 WriteLine 直参 bool 标签族）→ Interop Com。

## v14 泛型类构造器逐实例化 spec 批（2026-09-23，hashset_basic em→pass，sweep 306→307，检查点 run.gHqSU9）

v13 收尾时定位的 hashset_basic 段错误根因：**泛型类构造器从不特化**。
SpecRegisterClass 只认 methStat∈{0,1}（ctor=2 永不命中），开放 ctor 以擦除
形式一次性编译——构造器体内任何类型敏感的降级都退到引用形状：
`new Dict<T, int>()` 的 key-kind 位清零（string 模式），int 键实例化把键当
指针 strcmp（lldb：_platform_strcmp，地址 0x1；qa22 Bag<int> 最小复现，
HashSet<string> 恰好不炸 = 擦除模式与引用键一致）。

1. **GenNew 路由**：泛型类 new（TypeFullName 带 "<" 且类有类型参）→
   InstCtorSpecNg——克隆开放 ctor、剔除 TypeParam kids、SubstTypeNode
   代入实例化实参、AddMethNg 挂在**开放类名**下（stat 2，sval2=实例化名
   使 `this` 字段访问走代入后的类型）、BLInternal 到逐实例化符号。
2. **符号一致性**：EmitMethod 用 CtorSpecSymOf 镜像调用点符号（多 ctor
   类按 CtorSymNg 同款追加代入后参数 tag——首版漏 tag，Transfer<string>
   双 ctor 链接失败，**chart 剖面在出货前抓到**）。CtorCountNg 与
   FitCtorIdx 忽略 sval2 ctor，克隆注册永不翻转开放类的单/多 ctor 符号
   形状、也永不劫持开放 ctor 解析。
3. **字段初始化器**：ctor 前奏对 spec'd ctor 代入克隆节点
   （`Dict<T,int> f = new Dict<T,int>()` 字段初始化器同样特化），字段类型
   用 SubstTy 代入后再做 double/int 判定。
4. 已知边界（套件未触发，记录在案）：spec'd ctor 内 `: this(...)` 链落到
   开放兄弟 ctor；泛型基类 ctor 链仍跑开放基 ctor。另记预存限制：
   `Console.WriteLine(<直接 spec 方法调用>)` 重载挑选把实参当 int
   （true/false 打成 1/0，值正确，本批之前就在，套件未覆盖）。

**验证**：39/39；crossboot ELF 5/5 + PE-COFF 5/5；正式自举 run.gHqSU9
stage2.o==stage3.o；qa22（Bag<int>/<string>）、qa17/qa18/qa19 探针字节
一致；hashset_basic/fuzzy_bm25/dict_remove_churn/generic_constraint_
dispatch/generic_statics 定向全 pass；chart 剖面与 v13 逐字节相同
（2138，零 delta）；全量 sweep 零回归（pass 307 / ncf 170 / nlf 0 /
om 6 / em 11 / mne 11 / rcf 119）。

**下一批**（按既定顺序）：socket om/em 8 例 reactor 真挂起（RecvOv/
RecvToOv/AcceptOv 诚实化 + close 唤醒）→ 委托捕获/7 参形状（6 chart
错）、Html Dictionary<Action>（3）、FindNotAnyOf（2）、Task.Delay-arg/
ToStr-arity 级联（含上面 WriteLine 直参 bool 标签族）、Interop Com。

## v13 Get$T 显式泛型 spec 批（2026-09-23，chart 错 2866→2138，sweep 305→306，检查点 run.00JWPU）

`CollectSpecsInEnvNg` 的两条 SpecRegister 路径（显式类型实参、首参推断）用
**全局裸名** `FindGenericDecl(name)`（首个匹配即中）找泛型声明，同类名方法
互相劫持：`Control.Get<T>(string name)`（Gui/Control.zan:3696）劫持
`Com.Get(iface,index)`（Interop.zan:588，chart 371×"too many arguments
calling 'Get$nint'"），`JsonValue.Get(key)`（System/Json/JsonValue.zan:150
被 ：520/:527 调用）报 "unknown static method 'Get$string'"——合起来正是
Get$T 批的全部 chart 闸门。

1. **receiver 限定重载集**（`src/selfhost/ngen.zan`）：SpecRecvClsNg 解出
   receiver 的类链（StaticTyOf / ExtRecvTy / 类名 Ident 静态限定），
   OwnedGenericDeclNg 沿链找"该链自己声明的单类型参泛型"——链上找不到才回
   退全局裸名规则（扩展方法 db.Query<T> 的宿主在别类，必须保留）。SpecRegister
   改收调用方定界的 decl 下标，内部不再自行裸名查找。qa17
   （Kennel.Get<T> 泛型 + Store.Get 非泛型 + Other.Get 静态双参 + Echo<T>）
   与 oracle 字节一致。
2. **关键回归**：旧推断分支条件 `FindGenericDecl(name)>=0` 身兼
   **fall-through 过滤器**——非泛型名要落到下面的实例化类分支
   （SpecRegisterClass）。重写后分支 2 只在 TryPlanInferenceSpecNg
   （receiver 链有单类型参泛型 + 首参具体类型可推断）真正注册时才认领，
   否则照旧下落；修回 Box<Square> 实例调用特化（generic_constraint_dispatch
   曾一度 16→0 output_mismatch，现回到 pass；7 个泛型 spec 定向例全部
   与 v12 基线逐例相同或更好）。
3. **sweep 305→306**：+pass fuzzy_bm25（`idx.Remove(2)` 曾被 'Remove$int'
   劫持）；hashset_basic ncf→em **新暴露**：泛型类**构造器从不特化**
   （SpecRegisterClass 过滤 methStat∈{0,1}，ctor=2），HashSet<T> 构造器里的
   `new Dict<T,int>()` 以擦除模式发射（key-kind 位清零 = string 模式），
   int 键被 strcmp（lldb 定位 _platform_strcmp，地址 0x1）。HashSet<string>
   探针 qa18/qa19 字节一致（擦除模式恰好匹配引用键），qa22 Bag<int> 最小
   复现。修复 = 逐实例化构造器 spec，列为下一批；零既有 pass 回归。
4. chart 剖面：Get 族错误归零，总错 2866→2138，唯一 delta 是一个用例正确
   走到已知委托限制（"requires concrete word-value signature" 52→104），
   无新错误种类。

**验证**：39/39；crossboot ELF 5/5 + PE-COFF 5/5；正式自举 run.00JWPU
stage2.o==stage3.o；定向 7 泛型例 + qa17/qa18/qa19/qa20/qa21/qa22 探针；
全量 624 sweep 零回归（pass 306 / ncf 170 / nlf 0 / om 6 / em 12 /
mne 11 / rcf 119）。

**下一批**：泛型类逐实例化构造器 spec（hashset_basic em，顺带解
Dict<T,...> 构造路径家族）→ socket om/em 8 例 reactor 真挂起
（RecvOv/RecvToOv/AcceptOv 诚实化 + close 唤醒）→ 委托捕获/7 参形状
（6 chart 错）、Html Dictionary<Action>（3）、FindNotAnyOf（2）、
Task.Delay-arg/ToStr-arity 级联、Interop Com。

## v12 awaited DllImport 阻塞外存批（2026-09-23，socket 族首绿，sweep 301→305，检查点 run.LrJ84X）

`await Socket.NativeResolveAllAsync/NativeConnectSockAddr`（irgen_expr.c:7969
emit_await_blocking_extern）此前是 web 栈的编译闸门：**52 个 chart 用例各吃
2 个错**（Socket.zan 一进编译闭包就报）。本批解锁后 12 个 ncf 用例进入
链接/运行，分布：pass 305 / ncf 172 / nlf 0 / om 6 / em 11 / mne 11 /
rcf 119——**旧 pass 零回归**，新增 om/em 全部是首次能编译的 socket 用例。

1. **降级发射**（`src/selfhost/ngen_async.zan`）：BlockingExternMiNg
   （ResolveStaticNg + MOD.Extern，只跑在 await 路径，不碰 Hook B）→
   EmitBlockingExternNg 完全复用 co-intrinsic 形状：oracle 同款校验
   （≤4 参、integer/nint/bool/enum 参、int/long/nint/void 返回；诊断照发
   但发射走同一条路径，状态链的 label 不脱节）、参数求值压栈 →
   SaveSlotsNg → 逆序弹入 x0..x3 → BLExtern(EntryPoint) **内联执行**
   （oracle 传 fnptr 给阻塞 worker；本车道没有 worker 池，与 recv/accept
   同款已记录偏差）→ int 返回 sxtw（EOF==-1）→ 结果进 frame+32 →
   state=k → `_zan_co_ready(frame,$resume)` 立即再就绪 → 解臂、Epilogue、
   resume-k 重载重臂交付。TryGenAwaitExprNg 在非 async 上下文显式拒绝
   （oracle：没有帧可挂起）。
2. **探针抓到的寄存器 bug**：首版调 `_zan_co_ready` 时 x0 还揣着状态号
   k、x1 是帧——调度器把帧地址当代码"调用"，qa14 SIGBUS（PC=
   0x157608400）。修复 = `MovRR(0,1)/MovRR(1,2)`（`_zan_io_wait_co` 的
   x0=frame、x1=step 形状）。修后 qa14（ResolveAllAsync "localhost"）
   与 oracle 字节一致（n=2，v6+v4）。
3. **zanstubs.c 网络族**（语义逐条对照 oracle rt_io.c/rt_sync.c）：新
   编译的 12 例要过链接，zan_io_* 一个都不能缺——socket_send
   （EAGAIN→-1/致命→-2 分类 + 惰性 SIGPIPE 忽略，oracle zan_io_init 同
   款）、recv/ready/alive/connect_status/cleanup、sockaddr_ip_str
   （__thread + inet_ntop）与两个 *_into 拷出变体、resolve_sa（v4 优先
   + AF_UNSPEC 兜底）、resolve_ipv4、sockaddr_family/is_safe（含
   v4-mapped/Teredo/6to4/NAT64 内嵌地址分类）、resolve_all（stride-32
   稳定去重、永不半集）、resolve_all_async、connect_sa（非阻塞 connect
   + select 截止）、close_notify（无 reactor → no-op）、monotonic_us、
   socket_cleanup。nlf 保持 0。zan_plat_net_interfaces 原本就在
   zanhost.c（第一次打桩重复了）。
4. **探针**（oracle `zanc src.zan -o exe`）：qa14 解析字节一致；
   qa15 TcpClient.ConnectAsync 回环连接+发送一致；qa16 活对端
   RecvAsync 无超时/5000ms 超时两形态字节一致。qa15 首版 recv 空串
   = v10 记录在案的 RecvOv/RecvToOv 单发不真挂起偏差（数据已在途；
   qa16 证明数据排队后交付正确），本批未动。
5. **sweep 迁移**（workdir 逐例 diff）：+pass {async_echo,
   socket_errors, socket_handle_nint, socket_ready}；ncf→om
   {async_asocket_echo, async_socket_async_echo, ipv6}；ncf→em
   {accept_after_close, async_concurrent_echo, async_dns, async_mt,
   socket_close_wakes}——全是 reactor 时序语义（真挂起、close 唤醒、
   并发回显），是 socket 下一前线。

**验证**：39/39；crossboot ELF 5/5 + PE-COFF 5/5；正式自举
run.LrJ84X stage2.o==stage3.o；chart 剖面 awaited-DllImport 错
104→0；全量 sweep 如上。

**下一批**（按既定顺序）：Get$T 显式泛型 spec 调用（8 个 chart
错，JsonValue.zan Get<string> ×5 等）→ 委托捕获/7 参形状（6）、Html
Dictionary<Action>（3）、FindNotAnyOf（2）、Task.Delay-arg/
ToStr-arity 级联、Interop Com。socket om/em 8 例挂在 reactor 真挂起
（RecvOv/RecvToOv/AcceptOv 诚实化 + close 唤醒），排在 Get$T 之后。

## v11 线程运行时批（2026-09-23，nlf 15→0，sweep 286→301，检查点 run.dfN3PV）

三部分：crt-transition 补齐 Threading 的 45 个 DllImport 符号、EH 状态
每线程化、`lock` 真互斥降级。sweep 分布：pass 301 / ncf 184 / nlf 0 /
om 3（仍是三例 oracle 陈旧归因）/ em 6 / mne 11 / rcf 119，零回归。

1. **45 符号 runtime**（`crt-transition/zanstubs.c`，语义逐条对照
   oracle `src/runtime/rt_sync.c`）：
   - `zan_thread_start`（pthread_create 分离；我们车道的 ThreadStart
     委托对象是 {fn@0, env@8}、调用 fn(env)，nm 核实车道不引用
     _zan_delegate_*，trampoline 不做引用计数）+ `zan_thread_current_id`
     （pthread_threadid_np，同 oracle macOS 分支）。
   - 7 个 `zan_atomic_int_*`：C11 `__atomic` SEQ_CST，add 返回新值、
     compare_exchange 返回旧值（oracle 语义）。
   - 36 个 `zan_shared_table_*`：单进程诚实移植——进程内名字注册表 +
     堆列式行存储；schema `i:n;`/`f:n;`/`s:size:n;`（字符串列 size+1
     含 NUL，同 zan_parse_schema；重名列拒绝；int/float 8 字节对齐）；
     FNV-1a 64 强制非零哈希（hash() 与 _at 族共用）；wall-clock 过期 +
     oracle 清扫语义；rate_allow/lock_acquire/lock_release/extreme_at/
     match_at 逐行移植（INT 列 "count"/"window_start"/"owner"、仅
     absent 建行加锁、min 时 0 视为未设）；7/8 载荷因子拒建行；
     OsHandle/Attach 诚实失败（过渡运行时无跨进程承载物）；表有意
     不 free（句柄不悬垂）。
2. **EH 每线程化**（exception_threads 抓的正面竞态）：_zan_rt_eh_state
   原本把状态块指针缓存在进程级全局——线程会把 longjmp 打进别的线程
   armed 的槽。RtEmitEhState（ngen_obj.zan）改为尾调 zanstubs.c 新增的
   `zan_eh_tls_state`（`static __thread` 存储，块布局不变；
   crossboot/stub.c 用静态块顶单线程 qemu 客场）。**教训**：Mach-O 的
   BLExtern 名必须带前导下划线（ELF 写入器恰好剥一个，两边通吃）。
3. **lock 真互斥**（monitor_striped 抓的降级缺口）：parser 原把
   `lock (e) body` 降成普通块（写注释时还没有线程）。现在 parser 产
   AK.LockStmt，ngen GenLockStmt 在检查后脱糖为
   `{ var $lockN = obj; monitor_enter($lockN); try body finally
   monitor_exit($lockN) }`，底层是 rt_sync.c 同款 64 条纹递归互斥
   （指针折叠哈希）。骑真 try 机制白拿 oracle 的释放纪律
   （irgen_stmt.c AST_LOCK_STMT）：throw 在 throw 点释放、
   return/break/continue 走 pending-fin、嵌套 unwind 落进 lock 自己
   armed 的 buf。

**探针抓到的两个 ngen bug（都关乎"合成节点必须对编译器全程可见"）**：
(a) $lock 临时槽在发射期才 AddDeclNg——晚于 `frame = 16 * nloc` 定型，
写进帧外（qa4/qa11/qa13 SEGV；二分定位：内联循环没事、方法内循环 +
后随 try 必崩）。修复：ReserveLocals 的 LockStmt 臂里分配槽、把声明
节点存进 s.c。(b) HasTry 不认识 LockStmt → curHasTry=false →
#ehtop/#ehbase/#ehbrk 槽根本不保留、state-top 镜像不初始化。HasTry
现在把 LockStmt 算作 try。

**探针族**：qa4（单线程 lock/重入/throw 释放）、qa5（纯嵌套
try/finally 基线）、qa6（lock 内真 try/finally）、qa8（无 throw +
throw 形状）、qa10–qa13（300/1000 轮、内联 vs 跨方法）全部字节精确；
monitor_striped 40000/40000/reentrant ×3 稳定；线程族 7/7 parity。

**验证**：39/39；crossboot ELF 5/5 + PE-COFF 5/5；正式自举
run.dfN3PV stage2.o==stage3.o；全量 sweep 如上。

**下一批**（按既定顺序）：awaited DllImport 阻塞外存降级
（NativeResolveAllAsync/NativeConnectSockAddr，irgen_expr.c:7969：
≤4 标量参数、同步调用、结果进 frame+32、立即再就绪；需要
zan_io_resolve_all_async/zan_io_connect_sa 桩）→ Get$T 显式泛型
spec 调用（8 个 chart 错）。之后：委托捕获/7 参形状（6）、Html
Dictionary<Action>（3）、FindNotAnyOf（2）、Task.Delay-arg/
ToStr-arity 级联、Interop Com。

## v10 async/门/套接字批（2026-09-23，await 家族解锁，sweep 284→286，检查点 run.8ymewr）

全部改动在 `src/selfhost/ngen_async.zan`。这是 chart_* 用例卡住的主闸门
（每编译 ~38 个 await 错误）的根修批：

1. **委托值 await**（Worker.onDown 族，`await h(x)` / `await this.cb(x)`）：
   IsDelegateCalleeNg 在 **await 路径**把解析失败的 -1 提升为 -2（thunk
   派发到 method-group ramp，返回帧句柄；一般 await 从帧读 SELF resume，
   irgen_expr.c:8540）。** placements 是本批最重要的教训**：起初把检查放进
   AsyncResolveCallNg 内部，而 PrepareAsyncStmtNg（Hook B 分离调用识别）
   对**每条调用语句**都跑它 —— 普通委托字段调用 `sink(3);` 被当成"分离
   异步调用"发射，void thunk 返回值被当帧句柄二次唤醒 → SIGSEGV
   （delegate_field_call 崩溃、8 em + 2 om 假回归、无 async 模块被塞进
   co runtime）。sweep 抓住，挪到 await 上下文后全部还原。
2. **自降级挂起内建**（CoIntrinsicKindNg/EmitCoIntrinsicNg，以 IR lane
   EmitSocketIntrinsic 为规格）：Gate.Park、Socket.ReadReady/WriteReady
   （native lane 立即可读，interest 1/2 是降级常量）、RecvOv/RecvToOv
   （一次 libc recv，负数 errno 式返回原样传播，超时不强制=已记录偏差）、
   AcceptOv（libc accept，失败闭合）、ResolveSockAddr（IPv4 字面量经
   inet_pton 填 16 字节 darwin sockaddr_in；DNS 名 *outn=0 已记录缺口）。
   无值类（Park/ready）resUsed=true 报错；有值类从 frame+32 结果槽交付。
3. **gate/io runtime 自发射**（EnsureAsyncRuntimeNg 内，asyncRtDone 守卫）：
   DllImport EntryPoint（zan_gate_new/signal/free）经 BLExtern 本地符号
   优先解析到自发射实现。Gate={head,tail,surplus} 24B，waiter=
   {next,frame,step} 24B，盈余暂存语义照 rt_io.c 5060-5160；gate_free
   排干等待者（native lane 无 free）。

**探针抓到的两个发射 bug**：park 入队把 gate 留在 x0 跨过 _zan_rt_alloc
调用（返回值覆盖 → waiter 自环，Signal 永远唤醒不了；修复=gate 一并压
栈）；rev16 编码写成 REV（0x5AC00C21 应为 0x5AC00421）→
_zan_resolve_sa_co 内 SIGILL。

**chart 真实剖面**（必须从 zan-selfhost 跑让 stdlib 兜底生效；此前从
tests/conformance 跑出 34 错是**无 stdlib 的伪剖面**）：81 → 44。剩余
族：Get$T 显式泛型 spec（8）、委托捕获/7 参形状（6）、Html
Dictionary<Action>（3）、awaited DllImport（2，irgen_expr.c:7969
blocking-extern 规格）、FindNotAnyOf（2）、Task.Delay 实参/ToStr 元数
级联、Interop Com。

**复验**：qa1（委托 await 局部+字段）/qa2（Gate park/signal/盈余/关门
往返）/qa3（recv/accept -1 传播 + 127.0.0.1:8080 的 sockaddr 字节
16,2,31,144,127 + DNS→0）全对；delegate_field_call 复归探针 ✓；39/39 ✓；
crossboot ELF 5/5 + PE-COFF 5/5 ✓；正式自举 **run.8ymewr**
stage2.o==stage3.o；624 sweep **286 pass**（+2），em 6 / om 3 / mne 11
案例清单与 run.OniEz2 基线完全一致；17 例脱离 ncf，其中 15 例编译已解锁
但链接需要线程 runtime 外链面（_zan_thread_*/monitor/semaphore 族：
atomic_shared_table、thread_* 等）—— crt-transition stub 扩面是下批
前置，不是回归。

## v9 语言修复批（2026-09-23，static const 位叠加 + 四个解析缺口，chart 524→80 错）

**真 bug（本批锚点）**：parser 修饰符扫描把 `const` 记作
`m = m + MOD.Static()`，而 `MOD.Static()=16`——**`static const` 叠加成
32 == MOD.Virtual()**，const 字段被当成 virtual **实例字段**（占布局槽、
不可 `ClsName.字段` 限定读）。一行修复（`|` 代替 `+`）消灭 chart 主家族
374 个 `unknown field` 错中的 312 个。stdlib 里 Log.TRACE、
QueryCapabilities.Filter 等全部归位。

**同批四个解析缺口**（chart 错误 212→99→80）：

1. **静态字段链类型传播**：`Store.items.Count`——StaticTyOf/ExprCls 的
   MemberAccess 分支只认实例字段；加 `ClsName.field` 分支
   （FindStaticTy 经 QualifiedCls），内层读的类型喂给外层成员。
   `unsupported index target` 24 错因此全消。
2. **基元/Math 常量折叠**：`int/long.MaxValue|MinValue`、
   `double.MaxValue|MinValue|Epsilon`、`Math.PI|E` 折叠为字面量
   （PrimitiveConstLit + StaticTyOf 类型标注 + IsDblExpr 兜底走
   StaticTyOf）；用户自己声明的 Math 成员不劫持。
3. **base.Method(args) 调用**：GenCall 识别接收者 Ident("base")，沿基类
   链找最近实现（跳过 curCls 自身 override、abstract 不终止），`this` 作
   接收者直接 BL（非虚）——ResolveBaseImplNg。
4. **CallRetTy 接收者链解析**：成员调用回退类型原来是 MethRetTy
   **全局同名第一匹配**（`Get` 在 SignalString→string / SignalInt→int
   间凭注册顺序二选一）；改为先沿接收者静态类链 InstanceCands +
   FitPickIdx，全局表兜底。`model.Get().Length` 一族归位。
5. **Contains 的 WriteLine 渲染**（sweep 揭示的潜伏分歧，非本批回归）：
   static const 修复让 log_rolling_file 首次编译通过，暴露 gen0 把
   string/List Contains 降级为 0/1 int、golden 打 `0/1` 而我们打
   `true/false`——值全对、渲染分派错。IsBoolExpr 对 Contains 调用（含
   `$` spec 后缀剥离）按 gen0 模型返回数值渲染。修后与 golden 逐字节
   一致（`true true 0 1 1`），om→pass。

**剖面**：chart_kinds_complete 524 → 80 错（-85%）；剩余约半是
**instance async**（Worker.onDown 等，~38 错/编译，每个 chart 用例拖同一
stdlib 所以全体卡此）+ 其级联（too many arguments 等多为方法注册失败的
错位解析）。**下批 = instance async + 泛型 spec 调用解析（Get$T）**，
是 53 个 chart_* 用例翻绿的闸门（golden 都在，oracle 因缺 packages 全部
reference_compile_failed，修好即直接对 golden）。

**复验**：五个探针（限定 const 读/基元常量/静态字段链/base 调用/同名
方法解析）逐字节正确；39/39 回归 ✓；crossboot ELF 5/5 + PE-COFF 5/5 ✓；
定向 parity 9 pass（property_accessors/orm_table_accessor 存量 ncf，批前
checkpoint 同判）；正式自举新检查点 **run.OniEz2：stage1==stage2==stage3
全收敛**（972767e4e564467f，连续第五个）；624 sweep 分布见 json。

## v8 编译器性能批（2026-09-23，大输入编译 36×/17× 提速，零行为变化）

**动机**：chart_kinds_complete.zan（错误恢复路径，524 错）编译 ~259s；
自举 stage1（29 个 selfhost 源 ~500KB，成功路径）~31s。`sample` 采样
叶子自时间定位出五波 O(N²)：

1. **三容器写器的 reloc 有序插入**（62% 样本）：InsertReloc /
   InsertCoffReloc / ElfAddRelo 为保地址序线性找位 + List.Insert = O(R²)。
   改为追加 + 发射前一次堆排序（SortReloc2/3/4，稳定升序，语义同旧）。
2. **methNodes 平表线性扫**：自研开放定址 MethIndex（**builtin Dictionary
   内部是线性游走——ngen_obj.zan `_zan_rt_dict_index` 注释明言，不能拿来
   做索引**）；`(cls|name)`、`name`、`cls` 三个桶，~20 个查找函数换桶查询
   （FindStatic/InstanceCands/ResolveExtNg/AccessorInChain/FitCtorIdx 等）。
   键分隔符用 `"|"`（**Zan 源串不解码 \x 转义**，DecodeStr 只认 \n\t\r\0）。
3. **FindClass / 静态字段**：ngen_obj 加 clsByName 与
   sfByClsName/sfByName 索引，RegCls/AddSf 单点收口。
4. **Patch/容器构建期**：SymPos 懒建 symPosIdx；macho/coff 的
   BuildRefSymIdx + ELF 的 BuildElfRefIdx（保留各自编号序），每 fixup /
   strRef 一次的 RefSymNum/ElfRefSym 全表扫 → 桶查询。
5. **diag LineOf 每条诊断全量扫源**（剩余热点 44%+25% runtime 串助手）：
   524 错 × MB 级 TU ≈ GB 级逐字节扫。改懒建行偏移表，越界行号语义
   （<1 → 文本首、超尾 → 末换行后）逐分支保留。

**数字**：chart_kinds_complete 259s →（reloc 排序）158s →（全部索引）
21.8s →（diag 行表）**7.1s（36×）**；selfhost 全源成功路径 30.9s →
**1.8s（17×）**。

**零行为变化的证据**：成功路径新 checkpoint stage1.o 与批前编译器输出
**逐字节一致**（cmp 通过）；chart 错误输出 1575 行日志在每步后 diff 为空；
39/39 回归 ✓；crossboot ELF 5/5 + PE-COFF 5/5（ELF/COFF 写器都动了）✓；
定向 parity resolution 敏感族 delegate_dispatch/extension_methods/
indexer_overload/interface_dispatch/cs_b09_indexer 5/5 ✓
（property_accessors/orm_table_accessor 为存量 ncf，批前 checkpoint 复核
同判）；全量 sweep 分布与 run.8GMa65 基线持平（见 json）。正式自举新
检查点 **run.aAlTLD：stage1.o == stage2.o == stage3.o 全收敛**
（sha256 前 16 位 f6e23b45af0200db，连续第三个全收敛检查点）。

## v7 A310 活绑定 owned-temp 降级（2026-09-23，binding_temp_source 归位）

**真缺口（已修）**：`comp.prop = f().field;`（接收者产出 owned 临时）我们
合成的是活绑定（IsLive()=1），golden 要求降级 const 快照。更早的对拍里
oracle 输出 `1 (null) ... 1 0`——`(null)` 正是悬空 target 的
use-after-free、`33` 读成 `0`：**Sep-13 旧 oracle 二进制早于其仓库的 A310
修复**。修复后我们与 golden 逐字节一致（`0 snapshot 1 alice carol dave
0 33`），是三方（oracle/native/golden）中唯一正确的；待用户重建
zan-lang 后该例在 parity harness 里翻绿。

**修法**（ngen_binding.zan，镜像 C host emit_binding_value 的
owned-receiver 分流）：`TryGenBindingValue` 在字段左值成立后检查接收者
`ExprCls(接收者) != null && YieldsOwnedTempNg(接收者)`，成立则 `ff=null`
落 const 路径。`YieldsOwnedTempNg` 覆盖我们代码生成里"产出即死"的形态：
Call/New/Await 结果、其链式成员、自定义 getter 属性（get_Prop 调用）、
op_index 元素、owned 分支的条件表达式；裸局部/this/真字段/静态读是
borrowed，保持活绑定。**注意顺序**：先递归接收者链再查 FieldOf（否则
`F().inner.name` 会误判为字段槽）。

**复验**：binding_temp_source 直跑 rc=0 逐字节 == golden；定向 parity
binding 族 + record_with_expr 5/6 pass（唯一 mismatch 即上述 oracle 陈旧）、
临时接收者族 arc_temp_receiver_methodgroup/async_receiver_temp/
arc_chained_temp/index_temp_release 4/4 pass；39/39 回归 ✓；正式自举
（SEED=run.IkNm6s/stage2）新检查点见 json。

**对 stale oracle 的 sweep 已饱和**：binding_temp_source/arr_lit_rc/
enum_257_members 在 harness 口径下会一直 output_mismatch（reference 侧
输出陈旧），但三者 native 输出均与 golden 逐字节一致——zan-lang 重建前
全量 sweep 数字不再有信息量，跳过。

## v6 验证与真 bug（2026-09-22，\u/\x 双重编码 + 全量复验）

**环境变化（重要）**：用户 2026-09-22 拉取 zan-lang（只读目录）：conformance
549→624 例（+113，含 15 个新 zandb_*）；stdlib 收缩为 {Gui,System}，
Game/Sdk/Commercial 移入 packages/（feffa89f/fd1dd4c2，09-18）。而
zan-lang/build/zanc 二进制是 09-13 构建（HEAD 83996719），**早于 packages
功能**——DYLD opendir 插桩证实它从不扫描包存储，因此 119 例
reference_compile_failed 全部是 oracle 侧陈旧（ZANPKG_MISSING），
**需要用户重建 zan-lang**（我方保持只读）。新旧 sweep 数字不可直接比。

**真 bug（已修）**：`\u`/`\x` 转义在 native 字面量池**双重编码**。ngen.zan
`RawByteStr(b)` 用 `Convert.ToString((char)b)`，而 b38ad09 的 chrstr 重写后
char→string 是诚实 UTF-8：b≥0x80 渲染成多字节 → `\u4e2d` 进池变成
C3 A4 C2 B8 C2 AD（len 6，应为 E4 B8 AD len 3）。最小复现 + string_escapes
golden 均证。修复：`Substring(0,1)` 切出全新 1 字节串再 `s[0]=b` 原始覆写
（字符串索引写按 native_string_ops 语义截断为 u8）。

**修后复验**：最小复现（E4 B8 AD/3）✓；string_escapes golden "all pass" ✓；
39/39 回归 ✓；定向 parity 8/8（string_escapes、char_and_ulong_text、
urldecode_nul、int_format_boundaries、known_folders、interp_nested、
cs_b10_interp_format、string_index_char_text）✓；正式自举
`SEED=Sk0XRV/stage2` 重跑得 **run.IkNm6s：stage1.o == stage2.o == stage3.o
全收敛**（强于脚本的 stage2==stage3 验收），新检查点 39/39 ✓。

**全量 sweep（624 例，run.IkNm6s/stage2，对照组 Sk0XRV pre-fix 277）**：
pass **278**（+1 = string_escapes 归位）、output_mismatch 6→3、
exit_mismatch 6、matching_nonzero_exit 9→11、native_compile_failed 207、
reference_compile_failed 119。**8 个异常全归因**：

| 例 | 判定 |
|---|---|
| arr_lit_rc、enum_257_members | 我们与 golden 逐字节一致，oracle 漂移 |
| async_shadow_same_name_across_await、struct_arc_lifetime | oracle 段错误(-11)，我们输出与 golden 一致 |
| fileinfoex_mmap、mmap_owner | 本轮移入 matching_nonzero（oracle 侧漂移） |
| binding_temp_source | **我方真缺口**：A310 活绑定对接收者为 owned 临时时应降级 const 快照，golden 0 vs 我们 1（下批修） |
| reference_compile_failed ×119 | oracle 二进制缺 packages 功能，重建即清 |

其余 207 例 native_compile_failed 为真实能力缺口（HTTP 客户端/socket 族、
DB 驱动含 15 个新 zandb_*、JSON bind、ORM、LINQ、反射、线程/锁）。

## v5 自举（2026-09-20，run.H78JR0 → run.dsdoIT，正式链）

`SEED=run.H78JR0/stage2 RT_OBJS=crt-transition 重建对象 scripts/native_bootstrap.sh`
一次通过：stage1 → stage2 → stage3，**stage2.o == stage3.o 逐字节一致**
（compare.log 留档），新检查点 build/native-bootstrap/run.dsdoIT（stdlib
冻结拷贝 + 源/种子/runtime sha256 齐全）。以 run.dsdoIT/stage2 复验：
39/39 回归、known_folders/urldecode_nul/int_format_boundaries/
char_and_ulong_text/xlsx_write 定向 parity 全 pass。
注意：/tmp 于 2026-09-20 再次被清空，crt-transition 的 zanstubs.o/
zanhost.o 按 README 从源码重建（哈希与 run.H78JR0 记录的不同——clang
版本差异，以回归+定点+sweep 行为为准）。

## v5 交叉目标（2026-09-20，执行级验收重建 + 脚本化）

基线 json 里"qemu boot byte-exact"的验证脚手架随 /tmp 被清而丢失，本轮
重建为**可重复脚本** `scripts/crossboot/cross_boot_check.sh`（每批照跑）：

1. **ELF64 执行车道**：stdlib-free 编译器拷贝（fixture 只用编译器内建的
   Dictionary/List/Console，pull-in 记录 total 1 files）以
   `ZAN_TARGET=aarch64-linux` 编译 5 个 conformance fixture →
   elfcheck → `ld.lld -m aarch64linux -static -nostdlib -T stub.ld` 链接
   freestanding 半托管 stub → `qemu-system-aarch64 -M virt -cpu max`
   裸机引导 → 与 tests/selfhost golden **逐字节对拍**。5/5 PASS：
   dict_minimal、dict_growth、native_string_ops（含软越界段）、
   list_string_search、host_args_bounds。
2. **PE-COFF 结构车道**：同一批源以 `.obj` 输出名（无需 env）走
   ngen_coff → coffcheck 5/5 PASS（同一脚本第二车道）。
3. **stub.c 要点**（scripts/crossboot/）：半托管 WRITE0/EXIT_EXTENDED；
   bump 堆 + 整数/字符串 mini-printf；_start 里 `msr cpacr_el1` 打开
   FP/SIMD（bare metal 默认陷阱，runtime 的 `stp q0,q1` 会打到 0x200
   异常向量）；boot 以 main(1,{NULL}) 交参数（否则 host_argc 读垃圾，
   Environment.ArgCount()>0 误入 probe 分支静默退出）；runtime 引用的
   macOS 专属宿主面（glob/CC_SHA256/mach 等）用 UNREACHED abort 占位——
   引导成功即证明执行路径未触达；软越界守卫（ngen_guard）按其设计喂
   文件/时间失败值（fopen→NULL、localtime_r→NULL 等）优雅降级，stdout
   不受影响；os_unfair_lock 单线程 no-op（interning 路径真调用）。
4. 同批复验：39/39 回归。基线 json targets 记录已更新指向本脚本。

## v5 结果（2026-09-17，run.H78JR0 定点 + 本批二进制，549 例 sweep）

549 例全量 sweep（顺序执行）：pass 257→279、output_mismatch 11→0、
exit_mismatch 9→4（仅剩 v3 遗留 async-EH 4 例）、native_link_failed 5→0、
native_compile_failed 244→243、matching_nonzero_exit 11（双侧一致失败桶）、
reference_compile_failed 12。

本批内容（定点 run.H78JR0 → /tmp/zanc-new，工作树 5 文件 +386/−62）：

1. **chrstr 纯 UTF-8 重写**（ngen_obj.zan）：snprintf("%lc") 受 locale 支配
   （C locale 下 >0xFF 全部丢弃/单字节落盘），改为镜像 oracle
   emit_char_to_cstr 的纯算术编码——lead/continuum 字节打包进一个小端 i64，
   一次 memcpy 覆盖 1–4 字节四种长度；char 0 编码为单个 NUL（stamp 读 0，
   与 oracle 一致）。char_and_ulong_text 的 €/ñ 渲染转绿。
2. **IsCharExpr 补 Call 分支**（ngen.zan）：返回类型为 char 的调用表达式
   现在识别为 char 表达式（CallRetTy 判定），`ch[0] & 255` 一类链路不再
   误走 int 渲染。
3. **int 宽度语义重构**（ngen.zan）：无符号 64 位 add/sub/mul 保留全宽
   i64 结果（声明的槽宽在 store 处截断，同 gen0 的 64 位 uint 读）；移位
   只有 count 按左操作数声明宽度掩码（ShiftMask32，裸字面量按 int）；
   取负只在 32 位有符号域包裹；uint 目标 store 用 ubfm 零扩展低 32 位。
   float 族：打包 float[] 槽位（ArrElemSize=4）store 走 NarrowF32Bits 取
   单精度位型，8 字节 float 槽（局部/List/字段/转换入参）走 NarrowF32
   在写入时舍入（ngen.zan/ngen_conversion.zan/ngen_delegate.zan）。
   float_widths/unsigned_widths/datetime_civil/float_list_slot 族转绿。
4. **sb.Append 长度 ABI 重做**（ngen_host.zan）：新增运行时助手
   `_zan_host_sblen`——读 ptr-8 stamp，校验高 32 位 magic 0x5A414E53 且
   低 32 位 ≠ str_alloc 哨兵低字 0x54524D47（哨兵与 stamp 同高字，单纯
   magic 会误判），失败即 strlen 回退（extern 渲染、getenv、经局部/字段/
   返回值流转的 raw 数字缓冲——调用点形状窥探 AppendArgIsRawRender 因此
   删除）；null 返 0；strlen 是 libc 调用会踩 caller-saved x1，助手内部
   存/取 x1（arm64 x0-x17 全部 caller-saved，20 位长度的向量化 strlen
   实测踩 x1、短串不踩——曾致 int_format_boundaries 只打尾巴）。sb 跨
   HostStringArg/sblen 压栈。此前 astr 直接信任 stamp 的路径对
   Interop.getenv 的 raw libc 指针读出垃圾长度 → known_folders 在 stdlib
   模式 SIGSEGV（memmove 源=栈顶环境区、长度=垃圾），现转 pass；
   urldecode_nul（%00 内嵌 NUL 靠 stamp 保长）保持 pass。
5. 同批验证：定向 14 例全 pass（known_folders、xlsx_write、xlsx_stream、
   int_format_boundaries、urldecode_nul、float_widths、unsigned_widths、
   datetime_civil、float_list_slot、cs_b10_interp_format、
   cs_b16_keyvaluepair、interp_nested、lang_fixes、char_and_ulong_text）；
   39/39 回归；bootstrap 定点 gen1==gen2（新一代二进制两次自举输出逐
   字节一致；与 H78JR0 stage2 的差异是合法的字符串 intern 位置）。
   v3 时代遗留 exit_mismatch 4 例（string_throw_dispatch、null_conditional、
   generic_class_async_generic_method、exception_rethrow——async EH 的
   rethrow/帧描述符问题，在旧定点二进制上同样失败，非本轮引入）。

## v4 结果（2026-09-17，run.H78JR0 定点，顺序执行，alarm 120）

| 桶 | 数量 | 定性 |
|---|---|---|
| 干净（no static Main） | 1218 | 96.1%，与 v3 持平：本批对 stdlib 零回归 |
| Json.Deserialize<T> 类不在本文件 | 47 | 44 直接报类未知 + 3 Json mapper 字段不支持，与 v3 的 47 同一集合 |
| 语法：嵌套集合初始化器 | 2 | CefBootstrap.zan、DownloadDialog.zan（v3 已知） |

超时 0、崩溃 0。

本轮批次内容（定点 run.whfZQX → run.H78JR0）：

1. **NativeMemory.Crc32 零扩展**：stdlib 声明 int 但 oracle intrinsic 返回全宽
   i64（`zext i32`），CallStaticNg 对 Crc32 按 long 收窄；zanstubs 的
   `(int)` 截断一并去掉。native_memory/crypto_digests/stream_io 转绿。
2. **file_info/file_lock 语义**：zan_file_time 缺失路径返 0（非 -1）、ctime 作
   创建时间；file_length 不再排除目录；readonly 位改 access(W_OK)；
   File.TryLock/Unlock 换 oracle 的 gen/slot 句柄表（二次 unlock 返 0）。
3. **未捕获类异常报告**：oracle 把 "Unhandled exception: <类名>\n" 打到 stdout
   （irgen_stmt.c reh.\*）。ngen 尾部改走 strpool 堆串 + strcat2/println（与
   Console.WriteLine 同路），类名经 _zan_eh_cls_name 纯代码查表返回堆串；
   此前 printf+raw cstring 路径打印空名。fileinfoex_mmap 转 matching。
4. **ToBytes/slot_copy 密集写**：byte[] 是打包布局（ArrElemSize=1），但
   _zan_ext_to_bytes/_zan_ext_slot_copy 按 8 字节 slot 落盘，产出 NUL 交错
   字节——xlsx_write/xlsx_stream 的 zip 全部损坏。改 strb 步长 1。
5. **新增 Tilde token（'!' 与 '~' 分离）**：lexer 曾把 `~` 折进 `!`，ngen 按
   IsBoolExpr 分发逻辑/按位取反——`!d.ContainsKey(k)`（int 形 0/1 谓词）走
   mvn 后 cbz 恒判真，dict_growth/list_string_search 全 miss。现 `!` 恒为
   `cmp #0/cset eq`（与 irgen icmp-eq-0 一致），`~` 独立 token 走 mvn
   （oracle 探针钉死两种语义）。

同批 conformance sweep（549 例，/tmp/zanc-new）：pass 235→257、
native_compile_failed 287→244、新 matching_nonzero_exit 桶 11（双侧一致失败，
如 fileinfoex_mmap）；fileinfoex_mmap/file_info/file_lock/native_memory/
xlsx_write/xlsx_stream 定向全绿；39/39 回归；bootstrap 定点逐字节一致
（run.H78JR0）。v3 时代遗留（在 whfZQX 上同样失败、非本轮引入，立待办）：
float/宽度/格式 output_mismatch 族（float_widths、unsigned_widths、
int_format_boundaries、datetime_civil、float_list_slot、cs_b10_interp_format、
cs_b16_keyvaluepair、interp_nested、lang_fixes、urldecode_nul）、链接失败
extension_methods/native_memory_sha256/respack_roundtrip/win_ping_network、
exit mismatch binding_\*3/objinit_ctor_field_overwrite/win_taskscheduler_smoke。

## v2 → v3 之间修掉了什么（挂死根因链）

v2 的 194 个"挂死"（alarm 120 截断）不是单一 bug，是三层叠加：

1. **CallRetTy 逐层重走链前缀**（指数主体）：fluent 链每一层调用 CallRetTy 时，
   `recvTy = StaticTyOf(cc.a)` 走一遍前缀，函数尾部 `StaticTyOf(e.a.a)` 再走一遍；
   加上 StringCallRetTy 的 `IsStrExpr(e.a.a)` 守卫、Dict Remove 的 `ExprCls(e.a.a)`，
   每层 4-5 次前缀重走 → 12 调用链 = 数百万次 StaticTyOf（实测 4.5×/层）。
   修复：recvTy 类型一次并复用（只有 .Invoke 剥壳才重算）+ 全部名字预闸门。
2. **ngen_host 的"类缺省即打类型"分支**：DateTime/Stopwatch/Timer/FileInfo/
   AtomicInt/StringBuilder 的实例成员臂在 `FindClass(X)==null` 时对每个调用点
   无条件 `StaticTyOf(e.a.a)`（或经 HostIsBuilder/IsStrExpr 间接），std pull
   不完整时再乘一层。修复：全部改为先比成员名（IsDateTimeMemberName 等名字闸门），
   命中候选才打类型。
3. **实验性位置键 memo（未合入）**：曾用 (line,col,kind) 缓存 StaticTyOf 掩盖指数，
   但子表达式共享起点导致键冲突——native_generic_overload_fit 错解析 + dbgen:679
   "unknown field 'sval'" 自举失败 + DownloadStage.Download 类静态解析错乱。
   已废弃；正确性靠上面的结构修复。

实测（无 pull 探针，/tmp/zn）：链长 8/10/12/14 → 0.46/0.01/0.01/0.01s（修复前
12 链 >40s 且不收敛）；DownloadJob.zan 独立编译 0.09s；39/39 回归全绿；bootstrap
定点逐字节一致（run.6Zroza）。

## v3 协议（待机器恢复后跑）

本轮测量被宿主机 swap 打满（27.6GB/23.5GB，多个编译进程卡在内核不可杀的 UE
退出态）污染，v3 未执行。复测步骤：

```bash
# 建议先重启宿主机（清理 UE 僵尸与 swap）；然后：
cp /tmp/zo /tmp/scoreboard2/stage2   # 或重建：见"复现/复测"
bash /tmp/sb3_run.sh                 # 顺序跑 all.txt → results3.tsv（含 swap 守护）
python3 scripts/native_parity.py --all --seed /tmp/zo \
  --reference ../zan-lang/build/zanc --runtime '/tmp/zan-t1/zanstubs.o /tmp/zanhost占位.o'
```

已知的遗留差异（非挂死）：ch1 式 `using System;` 大拉取单元会把 oracle 不拉的
文件（AsyncGate 的 await 等）也编进来，暴露 234 个拉取对齐类错误（oracle 同单元
只拉 4 文件）；DownloadJob.zan 按 oracle 的拉取集独立编译是干净的。大单元的
O(unit²) methNodes 扫描（ResolveStaticNg/FitPickIdx 线性查全表）另立待办。

## v2 基线（2026-09-16，00b5589+ThreadStart extern delegate，alarm 120）

| 桶 | 数量 | 备注 |
|---|---|---|
| 干净（no static Main） | 1026 | |
| oracle-parity 失败 | 47 | Jd Json 类解析、Wechat List mapper 等，oracle 同样失败 |
| 挂死/超时 | 194 | 本轮根因链（见上）——预期 v3 大部分转干净 |

## v3 结果（2026-09-17，4b62eb6，run.whfZQX 定点，顺序执行，alarm 120）

| 桶 | 数量 | 定性 |
|---|---|---|
| 干净（no static Main） | 1218 | 96.1%，单文件前端零失败 |
| Json.Deserialize<T> 类不在本文件 | 47 | 探针假象：目标类在同空间其他文件（Jd/Wechat Sdk 等），fail-closed 正确，oracle 同样失败 |
| 语法：嵌套集合初始化器 | 2 | CefBootstrap.zan、DownloadDialog.zan `Panel.Row() { Children = { … } }` |

超时 0、崩溃 0、extern ABI 闸门 0。v2 的 194 个挂死/超时全部转干净；v1 的
extern ABI 闸门（35）与 DownloadJob 挂死已在此前的 F 代理/拉取修复中根除。

促成 v3 的四个修复（4b62eb6 + 811cbc4）：

1. **按需拉取过滤器**（811cbc4，oracle pi_\* 移植）：单文件探针不再拖入整库，
   挂死与跨文件错误噪声随之消失。
2. **MACOS 预定义宏**：此前 `#elif MACOS` 的 Darwin 分支（dirent d_type@20/
   d_name@21 等）静默编译了 LINUX 布局，目录列举把条目分错类。
3. **opaque-string 数据流**：`string ent = readdir(d)` 这类 extern 初始化的
   string 局部与全部 string 形参按 oracle 规则标记无长度头；其两参 Substring
   走新的 `_zan_rt_substr_raw`（按字节裸切+实盖章），有界接收者仍走
   `_zan_rt_substr3` 的窗口钳制。此前无头缓冲上的切片一律答空。
4. **zan_file_read_path 语义**：ct 过渡层误写成 realpath；oracle 对工作目录
   存在的路径返回 ""（保留调用方拼写），仅对缺失的相对路径做
   `$ZAN_PKG_DIR`/应用目录兜底（directory_paths 测试钉住此契约）。

同批 parity sweep（549 例）：239→241 pass；directory_paths exit_mismatch→pass；
win_printing_raw_backend/smoke compile_failed→pass；clipboard_roundtrip
compile_failed→output_mismatch（oracle 自身在 macOS 抛 PlatformNotSupportedException，
环境性）。chart_\* 40 例 60s 编译超时为已知 O(unit²) methNodes 扫描（未变）。

---

日期：2026-09-15 · 编译器：HEAD+jsongen 改名（self-build，含 ZedMapperCls 修复）
方法：1267 个原版 stdlib .zan 文件逐个单文件编译（`zanc out.o <file>`），取第一条错误分桶。
单文件探针无 Main、无跨文件类型，故 "no static Main" = 前端（parse/binder/checker/声明级 ngen）全部通过。

## 结果

| 桶 | 数量 | 定性 |
|---|---|---|
| no static Main（前端干净） | 1080 | 85%；加上 149 个跨文件假象后有效通过率 ~97% |
| Json.Serialize<T> 类不在本文件 | 149 | 探针假象：目标类在同名空间其他文件（如 Sdk/Wechat/Models），fail-closed 行为正确 |
| extern ABI 闸门 | 35 | nint×145/double/ushort/short/ref/out/params——F 代理在修（bootstrap-blocking） |
| extern >8 寄存器参数 | 1 | Gui/Render.zan:83 `zan_gui_surface_rounded_rect`——需要 extern 栈传参支持 |
| 语法：嵌套集合初始化器 | 1 | Gui/Component/Downloader/DownloadDialog.zan:206 `Panel.Row() { Children = { a, b } }` |
| 编译器挂死（15s 超时） | 1 | System/Net/Http/Download/DownloadJob.zan——需查根因 |

## 已根除的簇（本文件的历史教训）

**103 个 "syntax error: unexpected '}'"** —— 根因不是语法：jsongen.zan 的
`GenClass` 与 irgen.zan:2022 的 `GenClass` 同名。静态调用解析器按裸方法名在
全局方法表匹配，`src + GenClass(gi)` 的类型模型解析到 irgen 的 void 版本 →
ngen 对返回的字符串指针发射 `_zan_rt_int2str`（十进制化堆指针）→ 生成的
`__JsonBind` mapper 源被截断成 `"<数字>}"` → 内层 Parser 报 unexpected '}'。
调用代码生成本身绑定正确（`bl _Jsongen_GenClass`），只有类型模型分歧——
同一调用两个解析器不一致。修复：jsongen 改名 ZedMapperCls（d3e921a）。
教训：**裸名调用 + 全局名字表 = 跨类同名方法必然踩雷**；ResolveStaticNg 需要
curCls 优先的作用域化（待办，与 F 的 ABI 改动一起落在 ngen.zan）。

## 运行时符号契约（链接期，编译通过后做）

`using System;` 拉取集（17 文件）声明的非 libc extern：zan_file_*（16）、
zan_mmap_*（7）、zan_audio_*（21）、zan_embed_*（4）、Plat*/Posix*（dlopen 族）、
ZanMonotonicNs、zan_pkg_fopen 等。原版实现参考 zan-lang/src/runtime/rt_file.c +
zan_embed_api.c（只读参考）。未调用到的 extern 不产生链接引用。

## 复现/复测

```bash
# 重建探针编译器（秒级，不走 bootstrap）
FILES=$(sed 's|^|src/selfhost/|; s|$|.zan|' scripts/selfhost_sources.txt | tr '\n' ' ')
build/native-bootstrap/run.XXX/stage2 /tmp/z.o ${=FILES} && cc -o /tmp/z7 /tmp/z7.o -lSystem
# 记分牌
find stdlib -name '*.zan' | xargs -P 8 -n 1 /tmp/scoreboard/score7.sh > results.tsv
```

注意：bootstrap run 目录冻结创建时的 stdlib/ 拷贝，exe_dir/stdlib 优先于 cwd——
用旧 run 目录验证 stdlib 行为会静默拉到旧库。
