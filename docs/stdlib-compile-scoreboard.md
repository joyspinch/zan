# stdlib 编译通过率记分牌（架构转向后的源驱动工作清单）

日期：2026-09-30 · v18o-10 · 定点见下方 v18o-10 节

## v18o-10 阶段边界批（2026-09-30，sweep 520→564，ncf 43→1，检查点 /tmp/v18n/fix/bin/zanc = run.KqvY7o + 38171e5）

反射/枚举器/位运算三簇 + 环境修复，全量 sweep 624 例 **520(live)→564 pass**（另有 20 例
matching_nonzero_exit 双侧同码退出，合计 584 例对齐）。1. **反射子系统**（新
ngen_reflect.zan）：oracle 记录布局逐字节对齐（方法/ctor 56 字节、field 32 字节、targs
NUL 结尾），typeof 数组/nullable/泛型、CreateInstance/Invoke*/Get/SetField 含 object
接收者；三个硬老师：MUL 的编码是 MADD-with-XZR（Ra=31——Ra=0 是寄存器 x0，静默加错）、
每个会 blr 的叶子必须自己存 x30（RtPrologue 只存 x29/x30）、方法表扫描 stride 56 vs
字段表 32（ReflScanName2S 参数化）。2. **foreach 枚举器协议**：sync/async 两条 lane 都
走 GetEnumerator/MoveNext/Current（集合只求值一次、枚举器放帧槽；indexable 才走旧
List 布局）——foreach_protocol 双侧绿。3. **ulong 位运算**（& | ^）补进 ngen 的无符号
lane——win_compression_smoke 绿。4. **包仓库回退**（main.zan PiGlobInto）：stdlib 树已
提供的命名空间不再拉 installed-package 副本——双副本把 Sdk.* 每个类型注册两次，生成的
__JsonBind binder 的悬空引用全部死于 "ambiguous type"（sdk_wechat_mp/product_modules、
win_compression_smoke 因此 rcf→绿）。**已验证 oracle 同病**：把 Sdk 拷进 scratch ref
stdlib 复现一字不差的报错。环境收口：重复包移出全局 store（AppUpdate/Commercial/
Industrial 保留，app_update 需要 Zan.AppUpdate）、store 独有的 Game 源并入 stdlib/Game
（9 个文件）、reference exe-stdlib 补 Game/Platform/Chart 三个 symlink（Sdk 原本就有）
——两侧单副本同树。剩余清单（下一批起点）：em 10（dictionary_wide_values = ORACLE
缺陷：Dictionary.Remove/Clear 后 zan_rt_str_release UAF，reference .ips 证实；其余
closure_mutable_capture/exception_rethrow/firebird_wire/
generic_class_async_generic_method/generic_deep_close/mqtt_lwt_retain/
string_throw_dispatch/struct_arc_lifetime/ws_client_auth）、om 3（cast_string_object、
http_forwarder_stream、http_server_stress）、ncf 1（sqlserver_tls）、rcf 17 全部
oracle 侧（gui/cef/chart/designer 簇 reference 运行时缺 zan_gui_* 符号；
namespace_qualified_call oracle 解析器 bug；process_control_smoke oracle 自己解析不了
Thread.Sleep；server_mvc_timezone ZanWeb 缺仓）、rto 9（oracle 自身超时，按库内
golden 对齐）。

## v18 阶段边界批（2026-09-29，sweep 507→518，ncf 55→43，检查点 /tmp/v18n/fix/bin/zanc = run.KqvY7o）

cs_b* 语言核心阶段收口：v18n/v18o 两轮共五个批次把 conformance 的语言核心残留清完，全量
sweep 624 例 **507→518 pass**（sweep 后 qdot 又把 null_conditional +
null_conditional_value 翻绿，活状态 520/15 em）。1. **switch 表达式 + 类型/关系模式 +
when、元组、op_call**（v18n/v18o-1/2）。2. **cs_b15 Task 面**（19932a1）：Task.Run 内联
delegate、Task<T> Result/Wait/IsCompleted、quiesce 泵模式——oracle 自身 rto，按库内
golden 逐字节对齐。3. **cs_b08 矩形多维数组**（8737b3e）：逗号秩声明 + 多下标索引解析、
oracle 头布局（arr=raw+32、dims 在 arr+8d、count 在 -16、rank 在 -8）、寄存器化
GenMdArrNew（x19-x25，绝不跨 calloc 持调用者保存寄存器）、行主扁平索引、行主初始化器
（str imm12 按宽度**缩放**：w 用 off/4<<10、b 用 off<<10——这两处是 cs_b08 最后的
om 根因）、int[][,] 锯齿嵌矩形、返回多维数组的方法。硬老师：MUL 是 MADD Ra=xzr
（0x9B…），SDIV 基址 0x9AC00C00，LDUR 基址 0xF8400000（0xF8000000 是 STUR），BL
偏移从 bl 自身地址起算，lldb 断点在这批二进制上不可用（二进制补丁 + 带内探针定位）。
4. **nullable 值类型**（f10fbe5）：T?（基元/枚举/struct）= 8 字节堆单元（指针即值、
0 即 null、payload 在 [cell]），引用型 ? 在 TypeFullName 抹除；装箱收口在
GenConversionValueNg 单一边界（声明/赋值/返回/数组初始化器），null 字面量保持 null
单元；?? 对单元，提升 + - * / % == != < > <= >= **先查空再做算术**（b 为 null 的
b/0 永不除）；.HasValue/.Value/GetValueOrDefault + oracle 的 "Nullable object must
have a value" 守卫；WriteLine/Write/串接把 null 渲染为空串。5. **真 null 条件
?.**（f272f2d）：解析器把 `?.` 记为 MemberAccess ival==1，旧检查盯 op==TK.QDot
（永不成立）→ `a?.Get()` 在 null 接收者上 SIGSEGV。现按 oracle emit_null_cond：接收者
判空一次、null 短路为结果类型之 null、值型成员/调用结果在取用路径装箱；StaticTyOf
把两种形态都提升为 T?，声明边界不再二次装箱（m=1、h=指针 的双装箱类）；链式
b?.Self()?.name 可用。验证：cs_b19_nullable_arr、nullable_value_types、
nullable_safety_test、nullable_reference_types、null_conditional、
null_conditional_value 全 pass；battery 39/39；crossboot 支路未动。sweep 归因：
dispatch_queue_growth / dispatch_delegate_ownership / dispatch_first_use /
generic_deep_close 随批次从 ncf 变为"能编译、运行崩"（Dispatcher.Take/Post、new
UiEvent、泛型 ToString 的解析在批次中出现）——运行时缺口列入 open；oracle 漂移
flapper 照旧（arr_lit_rc/enum_257 本轮落 pass，http_forwarder_keepalive rto→em，
foreach_protocol mne→em，redis_client em→mne）。

**下一批**（按既定顺序）：泛型 delegate 簇（generic_delegate_ctor、
generic_delegate_ctor_methodgroup、generic_iface_convert、generic_nested_generic_field、
ternary_is_type、type_test_ops、closure_mutable_capture）→ Web 绑定族（web_* 6 例）→
JSON 簇（3）+ Game（4）→ 所有权/异常簇与 em 清账 → reflection intrinsics（3）→
misc 单例。

## v17 阶段边界批（2026-09-27，chart 1670→252，sweep 316→380，检查点 run.y2Gm7Q/run.iIz2Mx）

v17b 四项回归清零 + 两项阶段闸门修复。六个根因、六个修复：

1. **nsresolve 镜像按 nsresolve.c 补完**（hashset_basic）：冲突改名改回
   "按声明顺序单遍扫描"（逐对标记 + 单遍 MangleFull），name_taken 扫描
   全部 finals 且**不含自身豁免**——dotless 全名（全局 "Button"）与自己的
   initial simple 相撞时按 oracle 语义跳到 _2；qualified 引用带 arity 校验，
   静态/点式接收者镜像 resolve_static_receiver/resolve_qualified_receiver，
   未解析裸名保留 oracle 的歧义报错。
2. **Directory.ListNames 落到 glob(3)**（dir_watcher）：_zan_host_dir 返回
   65536 字节换行分隔 BASENAME、已排序、不含点文件，"dir/*" 同时返回文件
   与目录——watch-ok 场景与 oracle 逐字节一致。
3. **pull-in 同名空间子目录下沉**（pullin_shadow_same_name）：照 oracle
   pi_glob_into POSIX 支路——目标目录的一层子目录内 .zan 声明与目标同名
   空间时并入（Gui/Text/Text.zan、Gui/Text/RichText.zan 等）；配 nsresolve
   尾部 fallback（用户声明先于 stdlib，倒序扫描把 stdlib 文件里被遮蔽的
   裸名绑回它书写时对应的声明），以及 stdlib 两处 stray 清理
   （Gui/Text.zan 截断副本、Gui/QrEncoder.zan）。
4. **重载贴合补窄整型/数组维度**（crypto_digests rc=139 无限互递归）：
   Hash(byte[],int) 调用点被判给 Hash(string,int)——KnownParamTy 缺
   byte/sbyte/short/ushort/uint/float（数组递归判元素），字面量分支按 C#
   隐式常量转换放宽（IntLit→窄整型 5、→double/float 4；FloatLit→float 8；
   CharLit→int/ushort/uint 4）。九组已知答案向量与 oracle 输出+rc 全同。
5. **ELF .strtab 名称偏移与内容偏移解耦**（crossboot ELF 支路全红）：
   字符串符号拼写是 l_.str.N，st_value 才是字符串池内容偏移——新
   elfNoffs 前缀和（StrSym 拼写）走 st_name，elfSoffs（StrOffsets 内容）
   走 st_value，externs 用 extNameBase/extOffsets。恢复 10 PASS。
6. **stdlib 45 个扁平 Gui 镜像删除**（chart 剖面 1670→16998→252）：下沉
   让子目录正本成为 provider 候选后，用户树里那套扁平镜像
   （Gui/App.zan、Control、Event、Theme、StyleBox、Types…）被一并拉入，
   同全名双副本把 count_simple 翻倍 → 每个 chart 用例 315 个假
   "ambiguous type"。oracle 树 Gui 顶层**没有任何扁平 .zan**（子目录即正
   本，Gui/Core/App.zan 是 `partial class App`）→ 扁平镜像是 stray，照
   QrEncoder 先例删除；同全名重复族 104→10（全部是合法 partial 分片）。
   剖面 252 的残余（BrkElapsed/gapVal/ChartBreak 族）在**两棵树都不存在**，
   属 stdlib API 缺口而非编译器缺陷。

验证：39/39；crossboot 10 PASS；定点 run.y2Gm7Q 与 run.iIz2Mx
（stage2.o==stage3.o 逐字节）；定向 pullin/hashset/crypto/dir/namespace
8/8 全绿；全量 sweep 316→**380**（ncf 169→100，em 7，om 5，mne 13，
rcf 119；翻绿 gui_cef_profile，cef_runtime_index ncf→om；v17b 批内
hashset_basic/pullin_shadow_same_name/dir_watcher/crypto_digests/
mqtt_lwt_retain/ns_conflict_generic_arity 先后翻绿）。mqtt_lwt_retain
确认**用例自身竞态**（连跑三次 2×em + 1×pass，与工具链无关）。

**下一批**（按既定顺序）：ncf-100 簇（unknown method 23 / arity mismatch
13 / async 赋值目标 10 / List 操作 9 / unknown field 8 / index target 4 /
表达式 kind 49 ×3 / ...）；cef_runtime_index om 与 http_client_keepalive
并发 HTTP 池 HttpRequestException 归因。（澄清：QualifyTypes 五类
type-decl 已含 DelegateDecl——Gui.Action 与 Gui.Reactive.Action 的跨空间
实冲突已按 oracle 语义改名并重写引用；剖析报告里 4 个 "ambiguous
type 'Action'" 全部来自已删除的扁平 Event.zan 副本把 count_simple 翻倍，
删除后消失。）

## v16 委托捕获/方法组形状批（2026-09-23，chart 2138→1670，sweep 315→316，检查点 run.xwRh1F）

委托批的 6 个 chart 错误（App.GuardBody×2、DataGrid 委托字段调用、
Upload this 绑定、Realtime 捕获 List\<DataColumn\>×2）全绿，且修复像预
期那样把整条 stdlib 级联一起带绿。五个根因、五个修复：

1. **Func/Action 名被同名校劫持**（App.GuardBody "no concrete compatible
   method group" 的真根因）：stdlib Gui/Event.zan 声明了非泛型
   `delegate void Action()`；InferGroupTyNg 从 GuardBody 本体发明
   `Action<nint>` 后，DelegateDeclTyNg 查 FindDelegate("Action") 命中
   0 参 decl 且 `tps.Count==0` 提前返回，把 `Action<nint>` 读成 0 参签名
   → GenDelegateGroupNg 以 ParamCount 1≠0 拒绝一切候选。qa26d 之所以复
   现不出，是它的最小 pull-in 没拖进 Event.zan。修复：DelegateDeclTyNg
   一律校验"引用的类型实参数 == decl 的类型形参数"，不等即回落到内建
   字符串形状（`Action<nint>` → [nint]/void）。
2. **实例化引用类的词形**（DelegateWordNg）：`List<int>` 作为
   GridNums\<T\> 返回类型、`List<DataColumn>` 被闭包捕获，都被
   `c.tps.Count == 0` 拒绝——而 List/Dictionary/Dict/StringBuilder 是编
   译器内建、根本不在类表里。修复：剥掉 `<...>` 按开放名判——内建引用
   容器直接放行，普通类按 isStruct 放行（struct 值仍拒绝）。
3. **绑定组的虚门槛过粗**（Upload:844）：按 `cls.poly == 1` 拒绝一切多
   态类的绑定组，但 Upload.OnPicked 本身非虚，静态直呼就是精确语义。修
   复：改按目标方法自身的 IsVirtualMod（virtual/override）把关，非虚成
   员照发直呼 thunk，虚/override 仍拒绝（thunk 读不到 vtable）。
4. **静态方法组 → 整数 cast 发的是 pair 指针**（qa26c 反汇编
   `bl _zan_dlg_intern`）：oracle 原生面把静态方法组值降为裸函数指针
   （irgen_expr.c:4995，只有 wasm32 包 closure record），zan_gui_guard_
   call 也按代码指针调用。修复：CastExpr 数值分支先经
   StaticGroupCastIdxNg（复用 InferGroupTyNg 的唯一性搜索，要求
   methStat==1），命中则 LoadSymRef(方法自身符号)——qa26c 现在的
   adrp/add 直接指到 `_Derived2_Stat`，与 oracle 输出字节一致；委托类
   目的地照旧走 {thunk, env} pair。
5. **Main 的 int 返回值被丢弃**（顺手抓到的 rc 奇偶 bug）：EmitMainShim
   在 `bl _Cls_Main` 后无条件 `movz x0, #0`，`static int Main(){return
   7;}` 永远 exit 0（oracle exit 7；async Main 的驱动明明留下退出码也被
   抹掉）。修复：仅 void Main / 无 Main 形状清零 x0，非 void 透传。电池
   39 例全是 `return 0` 所以从未暴露。

验证：qa26_delg 四形状（泛型委托字段调用、多态类 this 绑定、匿名方法
捕获 List\<string\>、静态组→nint cast）与 oracle 输出+退出码全同；
qa27（真实 stdlib pull-in 复现）委托错全清；电池 39/39；crossboot 10
PASS；新定点 run.xwRh1F（stage2.o==stage3.o）；全量 sweep 零回归
（pass 315→316 / ncf 170→169 / om 3 / em 6 / mne 11 / rcf 119，total
624，翻绿案例 ns_conflict_generic_arity）；chart 剖面 2138→1670
（−468，52 例逐例对拍零回归，级联修正远超 6 个点名错误）。

**下一批**（按既定顺序）：Html Dictionary\<Action\>（3）→
FindNotAnyOf（2）→ Task.Delay-arg/ToStr-arity 级联（含 v14 记录的
WriteLine 直参 bool 标签族）→ Interop Com。

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

## v18 批（2026-09-29）：gen0 对齐推断/扩展路由 + 重载虚表签名键

**Sweep 380 → 400 pass**（ncf 100 → 76，零回归），formal bootstrap `run.dNvZTP`
stage2.o == stage3.o 逐字节定点。三项修复：

1. **AsyncStoreNg 赋值目标**（ngen_async.zan）：await 赋值的实例字段目标
   放开为任意接收者表达式（StaticTyOf/FieldOf 解析，含 ThisExpr）；新增
   Index 目标（数组元素写，沿用非 await 臂的越界守卫
   `_zan_rt_guard_fail3`，按 esz 发 strb/strh/str/str）。
2. **gen0 对齐的泛型推断与扩展路由**（ngen.zan，规范 =
   irgen_expr.c:10954 unify_method_tp）：UnifyMethodTp 结构统一（先绑先赢）；
   NonGenericOverloadFits 门槛（有贴合的非泛型重载时不做泛型推断——oracle
   先排全集合再推断）；TryPlanInferenceSpecNg 重写（接收者作用域内的 owned
   decl、实例化扩展跳过、类限定守卫、逐参数统一）；$-strip 仅对 List/Dict
   接收者且操作名已知时生效，未知 List 操作落回扩展解析；GenCall 入口钩子
   重扫 spec（spec 体内的 `Enumerable.SumNum<int>` 不再解析到开放泛型）；
   ScanShapedExtNg/ResolveShapedExtNg（形变泛型扩展 Sum<T>(this List<T>,
   KeySelector<T>) 按接收者统一 + 候选打分：接收者文本 +8 / 实参文本 +8 /
   lambda 委托返回族 +4），ExtRetTyNg 供 CallRetTy（`Console.WriteLine(xs.Sum())`
   正确按 double 渲染）；IsBoolExpr 的 Contains 豁免收窄到 string 接收者
   （List.Contains 渲染 true/false，string.Contains 保持 0/1）。probe5 与
   oracle 逐字节一致。
3. **虚表槽按签名 key**（ngen.zan + ngen_obj.zan）：SlotKeyNg = 方法名 +
   逐参数 TypeTagNg，CollectVSlotsNg / VSlotOf（改传贴合选中的 decl）/
   VSlotImplCls / FindInstanceInClsKey / EmitVtInit 五处一致。**根因**：
   SqliteConnection.Query(string) 与 Query(string, DbParams) 同名共享槽 0，
   单参体里的 `Query(sql, new DbParams())`（IDbConnection 来源的虚调用）
   经槽 0 派回自身，每层帧 alloc 一个 DbParams 直至栈溢出（崩溃报告：
   SqliteConnection_Query___string ×N → DbParams_ctor → zan_rt_alloc）。
   同时修掉"非虚重载共享虚名时被误虚分派"的潜在错误。

**Harness**：native_parity.py 的 reference_dylibs() 用 otool -L + LC_RPATH
镜像 oracle 的 DllImport 驱动绑定（@rpath dylib + -rpath），db/tls 驱动
extern 在链接期与 reference 同方式落位。

**chart_kinds_complete 全绿**：1670 错误（v16 基线）→ 0 错误，原生运行与
golden 逐字节一致（ Gui 驱动 dylib 链接；双方 dylib 都缺的 15 个
zan_gui_*/zan_dispatch_* extern 按.oracle 的 -undefined dynamic_lookup 延迟）。

**新暴露簇（下一批目标）**：泛型重载集合排序 + spec 名去歧义 —— 仅委托
类型不同的三个同名泛型重载（OrderBy<T> × KeySelector/StrKeySelector/
NumKeySelector）都注册成 `OrderBy$User`（先注册者赢走所有调用点），且
pre-pass 推断取第一个同名泛型而不按 lambda 返回族对集合打分；
linq_csharp_overloads 同时踩中两者（byname=carol ≠ alice）。其余 ncf 簇
不变：dbgen/orm（SyncStructure/BlogCols）、LINQ（6）、tuples/patterns/
nullable arrays（cs_b*）、reflection（3）、web binding（5）、async 控制
流（5）。

## v18d（2026-09-29，run.KqvY7o）：泛型重载集合排序 + 推断

**Sweep 624**：pass 400 → **407**，ncf 76 → 71，om 10 → 7，em 8，
mne 11 → 12，rcf 119（stale oracle）。零代码归因回归。

**解明**：
1. **spec 名去歧义**（SpecRegister）：同名泛型重载计数
   `GenericOverloadCountNg(name) > 1` 时，spec 名追加 `_o` +
   `TySymTagNg(SubstTy(参数类型名, DeclTps, 类型实参))` —— OrderBy 的
   KeySelector/StrKeySelector/NumKeySelector 三个重载注册成三个不同 body，
   不再先注册者赢走所有调用点（linq_csharp_overloads byname=carol ≠ alice
   的根因之一）。
2. **重载集合整体排序**（TryPlanInferenceSpecNg）：先收集同名泛型候选集
   （接收者链优先，其次全局），逐候选推断绑定并按
   InferCandScoreNg 打分（实参精确类型 +8 / 方法组贴合 +4 /
   lambda 返回族贴合 +4），最优绑定胜出；全绑定（所有 tp KnownTyName）
   才进入 SpecRegister。非泛型重载先经 NonGenericOverloadFits
   （含 DeclParamsMatchArgs 参数类型核对）短路。
3. **oracle 位置式 unify**（UnifyMethodTp / UnifyTps，对齐 irgen_expr.c）：
   位置对位置合并类型实参，**不比较泛型头名** —— Selector<T,R> 对
   Conv<int,int> 直接绑 T=int, R=int；裸 tp 先到先得。
4. **lambda 第二遍**（LambdaBindNg）：按裸名查委托（对部分绑定做整体
   SubstTy 会渲染出 `Pick<User,>` —— 未绑定 tp 变空串的教训），要求
   参数个数一致，lambda 参数按"替换后的委托参数文本或显式注解"定型入
   临时作用域，再对表达式体 StaticTyOf 与委托返回类型 unify；
   LambdaBodyTyNg 在扫描点 push/pop 形参（扫描点没有 lambda 形参在
   作用域内）。字面量证据 ArgNaturalTyNg（int/double/string/char/bool）。
5. **调用形态正确的人口过滤**（OwnedGenericDeclArityNg）：实例形态扩展
   调用 `xs.M<T>(a)` 接收者是声明参数（argc+1）；类限定 `Cls.M<T>(a)` /
   裸静态全部参数 1:1（argc）；实例方法接收者是 this（argc）。中途
   filter 过紧曾造成 generic_instance_method（`_Pool_Describe`）与
   generic_class_instance_generic_method（`_Pool_Echo`）回归，桶 diff
   当批抓住并修回。

**Unlocks**：linq_csharp_overloads om→pass；linq_chained / linq_equality /
linq_extended / generics_linq / generics_uniform_repr ncf→pass。
**漂移者（文档化，非代码）**：mqtt_lwt_retain em→pass（计时竞态），
http_client_keepalive mne→em，fileinfoex_mmap + mmap_owner om→mne
（oracle 漂移，native==golden）。

**验证**：39/39 回归电池；chart_kinds_complete golden 逐字节一致；
bootstrap run.KqvY7o 不动点（stage2.o == stage3.o）。

**下一批（v18e 候选）**：query 语法（linq_query expr kind 57 / parser
"expected 'select'"）、dbgen/orm 簇（SyncStructure/BlogCols）、reflection
intrinsics（3 例）、收窄赋值检查（oracle 拒绝 `long l = xs.Sum()`，
selfhost 存原位）、NativeMemory builtins、表达式位 Task.Spawn/Run。

## v18e（2026-09-29，sweep 种子 /tmp/v18e/r3/zanc）：dbgen ORM 门面补完 + async 分离误解析修复

**Sweep 624**：pass 407 → **419**，ncf 71 → 60，om 7 → 5，em 8，
mne 12 → 13，rcf 119（stale oracle）。代码归因回归为零。

**核心修复：AsyncResolveCallNg 全局同名回退**。语句级调用若接收者类链
（含基类）上没有该名方法（内置 `Dict<K,V>` 无声明方法、`List.Add` 例外
已声明），旧代码会退到 `FindInstanceIdx(name)` —— 全表按名找第一个实例
方法。orm_table_accessor 里测试自己的 `async int Add(string, int)`
（Users.Add）匹配了 `this.byCol.Add(col, c)` / `this.Cols.Add(c)` 这类
Dict/List 语句，PrepareAsyncStmtNg 把它们当异步调用**分离成协程 spawn**：
`sub.awaiter = sub`（接收者写进自身 +0x10 → dict.keys = dict、
list.items = _zan_co_reap）、`awaiter_step = reap`、`co_ready(frame,
[frame+0x28])`。第二次 `OrmMeta.Col` 时 `keys[0] = [dict] = count = 0x1`，
strcmp 读地址 1 → SIGSEGV（crash .ips：`_platform_strcmp ←
zan_rt_dict_set+184 ← OrmMeta_Col+436 ← AccUserCols___Meta+684 ←
__DbCF_AccUser_Sync`；stdout 全缓冲未刷 → 静默崩溃）。修复：全局回退
仅限隐式 this（Ident 被调），显式接收者只在自身类链解析。
**同族解锁**：cef_runtime_index om→pass、odbc_buffers om→pass、
mysql_async_nonblocking ncf→pass —— 三个旧漂移/挂起项一并真实转绿。

**dbgen ORM 门面（orm 簇 3/13 → 13/13）**：
1. **RewriteConflict**（对齐 oracle GenDb.zan:1731）：
   `OnConflict(a => a.col)` / `OnConflict(a => new { a.c1, a.c2 })` →
   每键列一个 `.OC("col")`；仅标量列；参数数≠1、nav/非成员、空集分别报
   "OnConflict expects a => a.<key> or a => new { ... }" / "OnConflict key
   must be a => a.<scalar field>" / "OnConflict must name at least one key
   column"；VisitCall 增 OnConflict 门控（I 链、单 lambda）。配套恢复
   RewriteUpsertSet 的 lambda 形状守卫（编辑时误删）。
2. parser 匿名 new（`new { a, b = expr }`，ival=2，checker 在 db lambda
   之外报错）；RewriteProj 的 Pj\<N\>/Pj\<N\>Map/One/Async 投影类按
   oracle GenDbEmit 逐字节；RewriteWhereIf / RewriteSet 一调用两实参 /
   GenAccess 按 Field 名分发 / RewriteSyncAllRoot 实参搬移 /
   RewriteAccRoot 访问器根（Insert/Update/Delete/Read/ById →
   I_/U_/D_/Q_ 链）/ RewriteUpsertSet ACC/GMX/GMN / RewriteToListCol /
   RewriteDoNothing / RewriteAgg/OrderBy/GroupBy 聚合体。
3. 翻绿：orm_upsert、orm_table_accessor、orm_dynamic、orm_extended、
   orm_freesql、orm_group_aggregate、orm_metadata_concurrency、orm_pool、
   orm_sync_all、orm_typed_query（均 ncf→pass）。

**漂移者（文档化，非代码）**：mqtt_lwt_retain pass→em —— 已验证 v18d 的
pass 是计时竞态：v18d 检查点种子（run.KqvY7o）现在同样 3/3 失败
（"Will message mismatch: got "）；http_client_keepalive em→mne 洗牌，
同族 flapper（隔离运行可通过）。

**验证**：13/13 orm 定向对拍；39/39 回归电池；chart_kinds_complete
原生运行与 golden 逐字节一致；全量 624 sweep（+13 翻绿、零代码归因回归）。
bootstrap：run.xwRh1F stage2 快照可干净编译当前源码；严格
stage2.o==stage3.o 不动点留待下次全 bootstrap 边界（stage2 内嵌旧
pull-in 引擎 39 文件，新编译器 50 文件，跨引擎对象对比不是不动点测试）。

**下一批（v18f 候选）**：query 语法（linq_query expr kind 57 / parser
"expected 'select'"）、reflection intrinsics（3 例）、收窄赋值检查
（oracle 拒绝 `long l = xs.Sum()`）、NativeMemory builtins、表达式位
Task.Spawn/Run、merge_partials、package-store 解析；ncf 残余簇：
tuples/patterns/nullable arrays（cs_b*）、web binding（5）、async 控制
流（5）。

## v18f（2026-09-29）— query 语法进 native 后端（419→420）

**根因与修复**（commit 97c8192）：
1. parser.zan：ParseQuery 不再产出 ngen 从未处理的 AK.QueryExpr(57) 节点，
   改为解析期脱糖到 Enumerable 扩展链
   （`from/where/orderby[ascending|descending,多键]/select` →
   `.Where/.OrderBy*/.Select`；多键 orderby 按逆序逐键发射单键稳定排序，
   复现 C# 主次序混合排序；恒等 select 省略 .Select，免去 R 推断）。
2. main.zan：demand pull-in 镜像两类"文本里没有 Enumerable 类型名"的写法
   —— 查询文本本身（`from X in`）与扩展形式的 DISTINCTIVE Enumerable
   成员调用（PiLinqExtMethod：Where/Select/OrderBy... → reach
   System/Linq + flag Enumerable）。超通用名（Count/All/Skip/
   Contains...）刻意不镜像：kernel11 证明 pull 进 Enumerable 会追加同名
   重载、挪移重载决议（`List<string>.Contains` 从 0/1 翻成 true/false，
   对拍失败）。
3. ngen.zan：InferExprTyNg 给关系/等值/逻辑运算命名 bool，LambdaBindNg
   对 lambda 体回退到它 —— 无类型 lambda 体 `n * 2` 终于能绑定
   Selector<T,R> 的 R（此前一切无类型 `.Select` 都是 unknown method；
   只有带注解 lambda 或成员/索引/调用体走得到返回位）。

**翻绿**：linq_query（ncf→pass，全量 sweep 419→420，零回归）。
**验证**：linq 六例 + orm x3 定向全 pass；39/39 回归电池（中途真回归
kernel11 被电池抓住并以上述镜像名单收敛修复）；全量 624 sweep。

**已探明的 ngen 开放缺口**（linq_query_clauses 相关，本批未修）：
lambda 内嵌套扩展调用、receiver 为外层 lambda 参数时解析失败
（`unknown method 'Select'`，repro row4）；receiver 换成捕获局部变量则
解析通过但 thunk 代码 SIGSEGV（repro row5）。clauses 批次因此改为
脱糖到扁平 stdlib 扩展（With/JoinPairs/JoinInto/GroupBySel + QPair
行类型），全程不产生嵌套 lambda。

**下一批（v18g 候选）**：linq_query_clauses（let/join/group 脱糖 + 上述
扁平扩展）、reflection intrinsics（3 例）、收窄赋值检查、NativeMemory
builtins、表达式位 Task.Spawn/Run、merge_partials、package-store 解析。

## v18g（2026-09-29）— query 子句补全（420→421）

**设计**：linq_query_clauses 的 let/join/join..into/group..by 走"扁平
脱糖"——全程不产生嵌套 lambda / 捕获（嵌套 lambda 的两个 ngen 缺口
见 v18f 节，仍开放）。脱糖落在新增的 stdlib 扁平扩展上：
1. stdlib Enumerable.zan：`QPair<A,B>`（first/second 行载体）+
   `With`（let：元素+投影值成行）、`JoinPairs`（join：嵌套循环连接，
   外序优先、组内保持内序，与 C# Join 枚举顺序一致）、`JoinInto`
   （join..into：空组也保留）、`GroupBySel/GroupBySelStr`
   （group..by：投影分组，复用 GroupByKeysInt/Str）。键为整数
   （KeySelector 族）。
2. parser.zan：ParseQuery 补全子句循环——`let d = e` → `.With(v => e)`
   （行映射：旧名读 row.first、d 读 row.second；`QSubstVar` 先把旧访问
   改写到新行变量上再包 .first——直接包旧访问会造出自引用行，join 后
   的 `p.name` 因此报 unknown field）；`join y in inner on l equals r
   [into g]` → `.JoinPairs/.JoinInto`；`group e by k [into g]` →
   `.GroupBySel`（终结或 into 后接 select）；select 恒等省略不变；
   QRewrite 把行映射代入后续子句体（嵌套 lambda 参数遮蔽映射名）。
3. ngen.zan：LambdaBindNg 返回位绑定先经委托实参映射——`Selector<T,V>`
   中委托自身返回型参 R 要先换成方法侧的 V 再统一；参数侧早有这层
   替换，返回侧没有，凡委托型参与方法型参不同名的扩展都永远绑不满。
4. main.zan：PiLinqExtMethod 增补五个行扩展名。

**翻绿**：linq_query_clauses（ncf→pass，输出与 golden 逐字节一致，
sweep 420→421）。**漂移**：http_client_keepalive mne→om —— 已归档的
mqtt_lwt_retain 计时 flapper 家族（双侧计时敏感，非代码回归）。
**验证**：39/39 回归电池；linq x7 + orm x3 定向全 pass；全量 624 sweep。

**下一批候选**：reflection intrinsics（reflect_typeinfo/
reflect_members/reflect_object_payload，3 例，需编译器发射类型记录——
独立大批次）、收窄赋值检查、NativeMemory builtins、表达式位
Task.Spawn/Run；ncf 残余簇：tuples/patterns/nullable（cs_b*）、
web binding（6）、async 控制流（6）。

## v18h — index 下标协议三例 + 参照重建（421 → 490）

**批次**："unsupported index target" 三例（dict_view_chain、
dict_remove_shared_prefix、operator_call_index）全部翻绿。ngen.zan
四处修改：
1. OpIndexFit 接受 indexer 协议的 static 拼写 `op_index(self, idx[,
   val])`——self 参数须名 为被走 receiver 类，index/value 形参偏移 +1；
   两种拼写的寄存器布局完全一致（x0 receiver、x1 index、x2 value），
   发射端零改动。ArgParamScore 以 int 字面量→long 得 6、string→string
   得 8 区分 CallableTable 的两个 op_index 重载。
2. `d.Keys[i]`/`d.Values[i]`：新增 IsDictViewAccess（MemberAccess
   Keys/Values、receiver 是 Dict——经 ExprCls 的 Call 臂，
   `f.Make().Keys` 链同样成立）；Index 读复用 List 元素路径（成员读本就
   落到 _zan_rt_dict_keys/_values 快照列表），ElemTyOf 回答 Dict 槽位
   类型；`d.Keys.Count` 收编进同一 helper。
3. TypeTagNg 追加 ival 数组层级——`f(int)` 与 `f(params int[])` 之前
   混叠成同一符号（_CallableTable_op_call___CallableTable_int 定义两次，
   ld 因重复 atom 断言崩溃）；标签在定义/调用两侧同一函数派生，自洽。
4. CallRetTy 应用 op_call 协议：`E(args)`（E 为类实例）发射端改写成
   `Cls.op_call(E, args)` 并按 FitPickIdx 择优，但类型模型无此臂——
   `t("zan")` 类型为 null，WriteLine 把返回的 string 指针当整数打印
   （5324699024）。现在 CallRetTy 解析同一合成调用、回答所选重载的声明
   返回型；IsStrExpr/IsBoolExpr 均汇于 CallRetTy，一处修复全一致。

**参照重建**：旧参照 zan-lang/build/zanc 是 Sep-13 二进制，早于
packages 特性，119 例 ZANPKG_MISSING 无法编译。本批从当前 zan-lang
源码 out-of-source 重建（cmake + homebrew LLVM；`-include ctype.h` 绕
main.c 严格 C99 下 isalnum 隐式声明；仓库本身一字未动），部署在
/tmp/v18h/ref/bin/zanc，旁置 exe 相对 stdlib（→ zan-lang/stdlib）与
zanrt_*.o。seed 与参照分别锚定各自的 stdlib 树（FindStdlibRoot 的
exe/../stdlib 优先于 parity 脚本的 cwd 符号链接），与生产布局一致。

**数字**：sweep 421 → 490（+69）。旧 119 rcf 中 103 例解封：66 直达
pass；22 rcf→ncf（chart_* 11 例、game_* 4 例、async_csharp_task、
generic_iface_convert、null_forgiving、ternary_is_type、defer_test、
app_update、sdk_wechat_* 2 例——这些 ngen 错误首次可见，是下一批的
真实工作清单）；7 rcf→om（chart 布局、cast_string_object、
sdk_jd_api）；4 rcf→em（dispatch_* 3 例、generic_deep_close）；
4 rcf→mne。余 17 rcf 为 Gui 重型。reference_timeout 8 例（>60s 或
网络型）。**回归共 6 例，全部参照侧**：namespace_qualified_call
（当前 oracle 源码对嵌套类限定名 `Outer.Inner` 报错——Sep-13 二进制
可编译，参照源码自身的回归，非我方）、http_bytes_redirect/
http_server_stress om、http_client_redirect/ipv6/tdengine_pool rto
（网络/计时 flapper）。**验证**：39/39 电池；events/delegates/dict/
linq/generics/params_variadic 定向 19/19；全量 624 sweep。

**下一批候选**：新解封的 ncf 清单里挑簇——chart_*（11 例，可能同
根）、game_*（4 例）、cs_b* tuples/patterns/nullable、async 控制流；
reflection intrinsics（3 例）仍留独立大批次。

## v18i — AddSPImm 大帧编码 + Chart 包同步（490 → 497）

**批次**：两个修复。
1. ngen.zan AddSPImm：ARM64 imm12 上限 4095，可选 lsl #12 是 ×4096 而
   非加宽——超过 4095 字节的帧把立即数 bit12 静默溢出进 sh 位，6,432
   字节的帧（新 ChartModel.zan FromJsonValue 的 402 个局部槽）编成
   `#imm12, lsl #12` == 9.5MB，在 8MB 主线程栈上第一次调用即 SIGSEGV
   （崩在 callee 的 stp 上）。任何局部槽 >251 的方法都会中招——这是
   更丰富的 stdlib 代码踩出的潜在编译器 bug，不是 Chart 专属。
   AddSPImm 改为分块发射：先 x4096 缩放形式、后普通余数（16 对齐
   保持；ngen_guard 早有同样的分块先例）。
2. stdlib/Gui/Component/Chart 从上游 Zan.Gui.Charts 包同步（10 文件
   更新、ChartBootstrap/ChartViewMatrix 新增、themes 刷新）——冻结
   快照早于 breaks/Matrix 时代（快照 ChartAxis 无 breaks 字段；包内
   ChartModel 24 处 breaks vs 快照 1 处）。其余核心 stdlib 校验为
   同步（494/495 一致；唯一 Linq 差异是我方 query 脱糖的刻意扩展）。

**数字**：sweep 490 → 497。6 ncf→pass（chart_axis_breaks/
splitline_pointer、chart_cached_events、chart_datazoom、chart_levels、
chart_option_behavior）；2 om→pass（chart_dataset_transform、
http_bytes_redirect flapper 回摆）；3 ncf→mne（chart_force_params、
chart_series_zorder、chart_tree_depth——能跑了但退出码非零）；1
ncf→om（chart_radar_values）。**遗留**：chart pie/layout 的 hugeTotal
路径——真实 ChartViewPie 里宽累计在 32 位回绕，而 `long += (int)(dbl+
0.5)` 的最小复现全对——更深的 ngen bug，下批开篇。http_forwarder_
keepalive 从 reference_timeout 后面浮出：已知 Task.Spawn 缺口。
mysql_async_nonblocking pass→rto（计时）。**验证**：39/39 电池；全量
624 sweep。

**下一批候选**：chart wide 累计回绕根因（mne 3 例 + om 复用同簇）、
Task.Spawn/Run（表达式位）、cs_b* tuples/patterns/nullable、
reflection intrinsics（独立大批次）。

## v18j — chart 宽累计回绕根因 + 字符串头三连（2026-09-29）

**批次**：四个 ngen 修复，全部靠在真实 ChartViewPie/ChartViewPolarBar 代码
路径上加插桩定位——同形状的最小复现全对，只有真实类出错。

1. ngen.zan Int32Kind 的 Ident 臂让**同名字段覆盖局部类型**：
   `t = locTy[li]` 之后无条件 `t = fld.ty`——PieLayout 同时声明
   `int total` 字段（55 行）和 Of 里的 `long total` 局部，于是
   `total = total + s.Value(di)` 被判成 int32，在 64 位 add 之后发射
   Wrap32（`sxtw x0,w0`）把和截断回 -1294967296（hugeTotal 路径）。
   修复：局部优先，命中局部即返回；rt/acc/acc2 不中招正因为没有同名字
   段——这解释了为什么全部最小复现与环模式路径都对，只有合并饼路径
   回绕。
2. ngen_obj.zan int2str/uint2str 返回裸 malloc(32) 缓冲、无 buf-8 字符
   串头——Convert.ToString 结果上的 Length/Substring/EndsWith 全部把
   malloc 的尺寸字读成长度，答出垃圾/空串（radar FracText "5000.50"、
   axis F() "2.800"——去尾零循环全数失效）。改走 str_alloc(32)（保留
   头槽位）并像 strcat2/substr3/fmt/chrstr 一样盖 magic|strlen。
3. strjoin 同样缺盖——string.Join 结果补上头。
4. ngen.zan IsDblExpr 的 Index 臂没有 List 分支（只有 Dict/数组/
   IsDblListOf——后者自身无 Index 臂），`List<List<double>>` 的链式
   元素读（PolarStackTops 的 tops[si][i]）被判成非 double，字符串拼接
   按 int2str 打出原始位型 4615063718147915776。补
   `ExprCls(e.a)=="List" -> IsDblTy(ElemTyOf(e))`（ElemTyOf 经
   StaticTyOf 递归可解链）。

**数字**：sweep 497 → 500。5 om→pass（chart_pie_layout、chart_radar_
values、chart_axis_interval_align、chart_polar_bar_layout、chart_stack_
strategy）；tdengine_pool flapper 回摆 rto→pass。4 个 chart mne
（cat_backfill/force_params/series_zorder/tree_depth）双侧 stdout 完全
一致——用例本身在 oracle 上也非零退出，属行为匹配而非缺口。**逐例重跑
确认零回归**：ws_client_auth pass→em 是 oracle 侧 SIGABRT（我方输出完
整，reference 4 行即崩）；async_landing_late_local、async_try_exit_
depth 重跑即 pass（oracle async 饿死打出空 stdout）；mysql_async_
nonblocking rto↔om 计时；sdk_jd_api om→rto。async_mt_sched、
http_forwarder_stream 以 rto→ncf 浮出：v18i 的 seed 同样编译失败
（干净 exit 1、stderr 空）——既有 async 后端缺口恰逢本轮 reference 没
超时才显形，与 Task.Spawn 同族，下批开篇。**验证**：39/39 电池；chart
家族 46 pass + 4 mne + 2 rcf（reference 侧编译失败）；全量 624 sweep。

**下一批候选**：async 后端（async_mt_sched 静默编译失败 + Task.Spawn/
Run 表达式位）、cs_b* tuples/patterns/nullable、reflection intrinsics
（独立大批次）。

## v18k — async 后端：Task.Spawn/WhenAll/真取消（2026-09-29）

**批次**：7 个 ncf 同三根。1. **表达式位 Task.Spawn/Run**：ngen 只有语句
弃位形态（PrepareAsyncStmtNg），`fan.Add(Task.Spawn(Leaf(i)))` 一类取值
位置全部 "unknown method 'Spawn'"。GenExpr 新增同构臂：发射被调 ramp、
awaiter=self（reap 标记）、awaiter_step=_zan_co_reap、ready、**不泵**，
x0 留帧句柄（irgen_call.c:621）。泛型实例化（async_generic_method 的
`Spawn(Slow<int>(5))`）走既有特化路径直接可用；非 async 实参落回普通
诊断，与 reference 一致。2. **await Task.WhenAll/WhenAny**：自宿 parser
缺 desugar_task_join（main.zan 只镜像了 prelude 拉取那一半）。ParsePostfix
把裸 Task 接收者的 WhenAll/WhenAny 改写成 TaskJoin——await 变成普通的
非泛型 async 方法 await，轮询由 stdlib TaskJoin.zan（与 oracle 逐字节
相同）的 IsDone + Delay 退避完成。3. **真取消**：原模型只是写标志无人
观察。帧头扩一词——child 在 +64（state/done/awaiter/step/result/
own_resume/exc/cancel 保持 0..7；args、保存局部镜像、sub 槽整体后移一
词，全量电池 + async 家族验证位移无害）；EmitAwaitNg 把被等的 sub 记入
child 槽；Task.Cancel 改为发射 _zan_ng_co_cancel 走链（标 frame[7]、
顺 child 下行）；每个 $resume 入口**先查** cancel，命中即走正常完成协议
（done=1、state=-1、唤醒 awaiter）且只做一次——后续唤醒（自己的 delay
定时器仍在途）直接返回，不再跑任何函数体。这正是 irgen_async.c
emit_async_cancel_check 的语义：调度器没踩过就被 cancel 的 spawn 永不
启动（never_hit=0）；cancel 父帧时它正挂着的 child 链在下一语句边界一起
死（Outer 被取消 → Worker 死，marks 保持 0）。4. **IsCancellationRequested
()**：async 体内读本帧 cancel 槽，体外 0（Probe 打出 before=0/after=1）。
5. **顺带根因**：_zan_co_after 的 deadline = now + ms*1e6 是回绕算术——
Task.Delay(int64.MaxValue) 回绕成"立即到期"，赢得它进入的每个 WhenAny
（async_delay_max 的靶心；oracle 在 rt_timer.c:674 饱和）。乘加两级都
饱和到 int64 max；泵里每次 nanosleep 截到 1s，永不到期的 deadline 不再
把 >999999999 的 tv_nsec 递给内核（EINVAL 会原地打转）。

**数字**：sweep 500 → 503。4 ncf→pass（async_cancel、async_mt_sched、
async_delay_max、async_generic_method）。http_forwarder_stream/tunnel
ncf→rto、keepalive 维持 rto：**native 侧三个用例已全部编译通过并逐字节
命中 golden .out（直接验证）**，但 oracle 自己过不了——它的二进制在
stream 上打出全 0 计数、在 tunnel/keepalive/stream 复跑上挂满 60s，而
reference_timeout 在 native 运行前短路，oracle 侧不修就永远到不了
pass。async_when_all 同理（native = golden，oracle 超时）。
sdk_jd_api rto→om、sdk_jd_client pass→rto 计时摆动；http_client_keepalive
em→mne（退出码对齐、两侧同非零）；async_landing_late_local、
async_try_exit_depth 本轮仍 om、重跑即 pass（oracle async 饿死，与
v18j 相同）。**验证**：39/39 电池；async 家族 13 例定向（12 pass +
async_csharp_task）；全量 624 sweep。

**下一批候选**：async_csharp_task（`async Task<T>`/ValueTask<T> 方法声
明糖 + Task 值的 Result/Wait/IsCompleted——cs_b15 共享后半）、cs_b*
tuples/patterns/nullable、reflection intrinsics（独立大批次）。

## v18l — async_csharp_task：Task 形接口 await 判定，async 阶段落幕（2026-09-29）

**批次**：C# 拼法的接口成员不带 `async`（`Task<int> ComputeAsync(int a,
int b);`），接口的抽象声明因此没有 async 修饰位，而具体覆写又不在接口
自身的候选链上（静态类型是 IService，Service 挂在它**下方**，没有向下
的查找图）——async_interface 里接口声明显式写了 `async int ...` 所以从
未暴露。AsyncResolveCallNg 的实例判定改为：**无函数体且返回类型呈
Task 形（Task / Task<T> / ValueTask<T>）的候选声明同样可 await**——C#
里正是声明类型决定可等待性；vtable 派发不受影响（覆写的 ramp 填同一
槽位，即 async_interface 已验证的机制）；返回 int 的普通接口成员照旧
拒绝。修复前先探针确认：静态 `async Task<int>`/`ValueTask<int>` 的元素
剥离与实例派发本来就好，缺口只有这一处判定。

**数字**：sweep 503 → 507。async_csharp_task ncf→pass——**async 家族
native 侧全绿**：12 例定向 pass + async_when_all（native 命中 golden，
oracle 自己超时）。http_forwarder_stream rto→om：oracle 本轮跑完了，
打出它的全 0 计数（直接验证过：其二进制 stream-status-200: 0、复跑挂
死），native 侧打的是 golden——记录为 om，oracle 侧不修到不了 pass。
async_landing_late_local / async_try_exit_depth 本轮落 pass（已知
oracle 饿死摆针）；sdk_jd_client rto→pass、mysql_async_nonblocking
om→rto、http_client_keepalive mne→em 计时摆动。**验证**：39/39 电池；
阶段边界全量 624 sweep。

**下一批候选**：cs_b* tuples/patterns/nullable（cs_b15 的 Task 值
Result/Wait/IsCompleted 与本批同族）、reflection intrinsics（独立大批
次）。

## v18m — cs_b18：无参构造的花括号对象初始化器（2026-09-29）

**批次**：`new Point { X = 5, Y = 6 }` 打在只有双参构造的类上——ngen 的
对象初始化器路径（ival==2：括号实参在前、字段写在后）只在类**完全没有**
构造时才回退裸分配。改为：花括号独用形态（零括号实参 + 至少一个字段
写）一律降为裸分配 + 成员写——即 reference 的形态（真 C# 要求无参构造，
reference 不要求）；裸 `new C()` 在只有带参构造的类上照旧报错。与
oracle 直接对拍 cs_b18_init ncf→pass；objinit_ctor_field_overwrite、
record_types、cs_b16_keyvaluepair 不受影响；39/39 电池。

**cs_b* 后续批次测绘**（已写入 baseline notes_v18m）：cs_b03 元组
（表达式 kind 52）与 cs_b06 switch 表达式（kind 49）、cs_b19 的 `int?[]`
在 oracle 里都是**多字聚合值**（LLVM struct：元组 map_tuple_struct、
`T?` 为 {payload, i1}）——共同前置是 ngen 8 字节槽模型的多字值槽（局部、
数组 ArrElemSize、save/reload）；cs_b08 集合表达式 `[1, 2]` 是 parser
缺口（脱糖 new T[]{...}）；cs_b05 模式变量绑错声明；cs_b_opcall_params
缺实参位的隐式 operator 转换；cs_b15（oracle 侧 rto）要 Task 值
（.Wait/.Result/.IsCompleted 直读协程帧）。本轮为批中批次，全量 sweep
留到 cs_b* 大批次阶段边界。

**下一批候选**：cs_b* 大批次（多字值槽前置 + 上述五项）、reflection
intrinsics（独立大批次）。

## v18o-11 — 泛型特化声明类型解析 + 裸重抛/字符串抛出双车道 + 过渡运行时定点（2026-09-30）

**批次**：sweep 564 → 573，om 3 → 0。(1) `CurDeclTyNg`（挂在 AddLoc 上）：
用类类型参数声明的局部/参数（`T keep = item;`）在声明时把参数名烧进
locTy，所有按键型降级（ToString 派发、string/bool/double 探测、标量分
类）都看到不可解析的 "T" 而落进名字匹配派发——`keep.ToString()` 对裸
字符串指针发射 `bl _Exception_ToString`（对字符字节做 strlen，故障地址
0x6f6c6c6560 就是 "hello" 正文；generic_class_async_generic_method，同
步 repro 同炸）。字段读取本来就经 CurField/sval2 惰性替换、CallRetTy 本
来就替换成员调用返回——声明局部现在走同一替换；开放模板（无 sval2）保
持擦除。(2) SpecRegister：扫描中的 spec 体内裸泛型调用（Pool<string>.
Chain<U> 克隆体里的 `Wrap<U>(x)`）继承被扫体的接收者实例化
（specScanMeth，RunSpecPipeline 每轮设、发射前清；owned 检查防扩展方法
误读），实例化并加入 spec 名（`Wrap$int$Pool.string`）——仅方法类型参
数会在类实例化间撞名，先注册的体赢走所有调用点（pi.Chain<int> 的裸
Wrap<int> 复用 Pool<string> 的体、对 int 做 strlen）。(3) EH：字符串抛
出点在 GenTryStmt 与 EmitAsyncTryNg 双车道都跳过 typed catch（async try
是独立降级——调试踪迹证实 async 体从不进 GenTry）；catch 子句内的裸
`throw;` 从子句自己的 #ehcx 帧槽重抛（体内嵌套 try 会覆盖全局在飞异
常）。exception_rethrow、ws_client_auth、generic_deep_close、
sqlserver_tls、string_throw_dispatch、cast_string_object 同批转绿。
(4) crt-transition：trio 重建；定点 run.z2BtL3 stage2.o == stage3.o
（字节一致）——NativeMemory 加 EntryPoint="zan_alloc" 改名后首个干净定
点（拉 repo stdlib 的编译器发射 _zan_rt_alloc → zan_alloc，严格链接需
runtime_core.o；parity seed 拉 ref stdlib 走内部路线故旧桩仍可链接）。
39/39 电池双运行时配置全绿；native_rt_core pass。**oracle 侧直证结
案**：firebird_wire（ref 自家 SRP 握手 "string index out of bounds" 3/3
确定崩溃；native 127/0==golden）、http_server_stress（ref MT 工人池并
发丢请求 33/7、29/11；native 每轮 golden 40/0）、http_forwarder_stream
（ref 全零计数或挂死；native==golden 字节一致）、closure_mutable_capture
（ref SIGSEGV；native==golden 直证）。**剩余种子侧**：linq_query_clauses
ncf（LINQ 簇，已用 38171e5 种子验证为既有缺口非本批回归）。
**验证**：39/39 电池（两配置）；定向 generic/async/closure 34/35（唯一
失败即 oracle SIGSEGV）；EH 16/16；dict/linq/event/delegate 17/18；全量
624 sweep。

## v18o-12 — 配置纠偏：正确拉取 + 过渡运行时组合，ncf 清零（2026-09-30）

**批次**：无编译器改动；v18o-11 的 sweep（573）被我 /tmp/v18q/stdlib → ref
树的符号链接带偏——harness 在每个 artifact 里把 stdlib 链到
zan-selfhost/stdlib，而 seed 原地调用时解析的是 exe_dir/../stdlib = 我的
链接。两个后果：(1) linq_query_clauses ncf 纯属配置错——parser 的查询
表达式脱糖目标是 With/JoinPairs/JoinInto/GroupBySel，只存在于自树
Enumerable.zan（v18i 有意的查询脱糖扩展）；用 harness 预期拉取后 linq
家族 7/7 全 pass（v18o-11 里"38171e5 既有的"结论同因错拉，更正）。
(2) 自树拉取必须配 crt-transition trio：NativeMemory 的 EntryPoint 改名
把 Alloc/Free/Crc32 路由到 zan_alloc/zan_free/zan_crc32（runtime_core.o
定义）；旧 /tmp/rt-new 桩没有这些符号，-undefined dynamic_lookup 留成懒
绑定，凡调 Alloc 的用例（zandb/ws/http/xlsx/crypto/sdk）首拍即
SIGSEGV——中间那轮"自树+旧桩"的 sweep 打出 79 个假 em/501 pass，才定位
运行时这个变量。System 的 .zan 源两侧只有 Enumerable.zan +
NativeMemory.zan 不同（其余为 drivers/皮肤/Gui-Hmi/Commercial，非 Gui 用
例拉不到）。**纠偏配置 sweep（自树拉取 + trio）：571/624，
native_compile_failed 1 → 0——首个零种子侧编译失败的全量 sweep**。
om 3 = async_try_exit_depth（oracle 饿死摆针）+ http_forwarder_stream/
http_server_stress（本轮 oracle 自己又坏：全零计数或挂死；native==
golden 字节一致，v18o-11 直证）。em 6 全部 oracle 侧/已记录摆针
（closure_mutable_capture、struct_arc_lifetime ref SIGSEGV；
dictionary_wide_values ref UAF；firebird_wire ref 自家 SRP 握手确定性
崩溃；http_client_keepalive、mqtt_lwt_retain 计时摆针）。mne 20 = 两侧
同非零且 stdout 逐字节一致（抽样 chart_force_params/redis_client/
win_tray_screen_smoke 验证）。**验证**：39/39 电池双运行时配置；
native_rt_core pass；bootstrap 定点 run.z2BtL3。全量 sweep 的每个非
pass 桶至此都是 oracle 侧或已记录摆针。

## v18o-13 — file_lock 根因收口（变参 open）+ Gui 簇激活 + stdlib/解析器收尾（2026-09-30）

**批次**：(1) **file_lock om 根因破案并根治**。排除法走完（懒绑定：strict/
flat_namespace/chained-fixups/-bind_at_load 重链全仍炸；绑定目标：间接
符号表逐项一致；自家代码生成：反汇编证实调用点 x2=420 已就位；umask；
libgmalloc 无堆损坏）后锁定真凶：**ngen 的 DllImport 调用三参 open——
open 在 C 里本就是变参函数（mode 由 va_arg 读出），这是已记录的"变参调用
降级"缺口的第二受害者**（snprintf 是第一受害者）。ngen 调用点的 x2 落在
寄存器残留值上，O_CREAT 文件按残留值建模式：实证 040/100/140/740/200，
每二进制确定、随链接布局漂移（exe 路径长度 ~24 字符悬崖、链接旗标、
DYLD interposer 都会挪动它；clang 对象按正确约定传参免疫；二参 DllImport
免疫）。第一版修复（stat 建好的文件、缺属主写位就 fchmod 0644 的启发式
守卫）**不够**：用内嵌踪迹版 runtime 在 harness 自己的 case 目录里按其
原样命令重链复现——踪迹显示首次 open 建出 0o100200（残留 x2 恰为 0o200：
有属主写、无属主读，守卫签名放行），二次 open 因 O_RDWR 需要属主读位而
EACCES(13) → relocked=0/released2=0。**根治**：zanstubs.c 增加
`int zan_open_creat(const char*,int){return open(path,flags,0644);}`——
从 ngen 视角是二参非变参调用（二参调用实证无恙），mode 由 C 车道按正确
约定传 0644；runtime_core.zan 的 file_try_lock 整体撤掉三参 copen 与
启发式守卫，规则入头注：**ngen 代码一律不得 DllImport 变参 libc 函数**。
验证：此前必炸的 harness 布局（zcr5ddr3 case 目录原样重链）连跑两次
golden-exact；六个链接路径长度 17..112 全 golden；native_rt_core pass；
39/39 电池（Zan 配置）+ 39/39 电池（C 基线 zanstubs_full+zanhost_full）。
变参降级本身的机制（WHY ngen 会错降）仍列为 ngen 侧待办，绕行面收紧到
"彻底避开"。(2) **Gui 簇激活**（此前 rcf 17 的主力）：自树 Gui-Hmi 经
dylib 链接配置激活 13 例——12 例 golden-exact，chart_grid_rect 5/6
（字体度量环境差：.AppleSystemUIFont 200@12 两侧 18 vs 23，跨机器漂移，
已记录为环境敏感）；oracle 反而无法严格链接（缺
zan_gui_draw_text_bold）。(3) **编译器/stdlib 收尾**：coll_postfix_init
golden（解析器花括号续接 + 门面 Add 收养括号参元素）；server_mvc_timezone
golden（ZanWeb 内容装进 exe 同胞 package store）；System 树与 ref 的
增量现为 Enumerable.zan + NativeMemory.zan 之外再加 AppPath.zan、
Diagnostics/ProcessControl.zan（沙箱 getpriority(1,self) 返回 ESRCH——
C 对照程序同样复现，故 POSIX who=0 查自身）、Diagnostics/ProcessList.zan
（oracle 抛 PlatformNotSupportedException，native 出真进程表）、
Net/Sockets/Socket.zan。(4) 归因落档：ws_client_auth 本批 native
stdout 与 golden 逐字节一致（10 行含 failures=0），ref 自己在第 8 行
SIGABRT（exit -6）——oracle 侧墙钟摆针；process_list_smoke em 归因
oracle 陈旧（native 更优）。
**验证**：file_lock 六长度 + harness 原样布局×2；39/39 电池双配置；
native_rt_core。**全量 sweep（修复后 trio）：574/624——新高**（v18o-12
为 571），native_compile_failed 0；file_lock 本轮 pass，ws_client_auth
本轮 pass（ref 自己活了下来，与摆针归因一致）。om 1 =
fileinfoex_mmap（已记录摆针）；em 7 = ref 崩溃四例（closure_mutable_
capture/struct_arc_lifetime SIGSEGV、dictionary_wide_values UAF、
firebird_wire SRP 崩溃，native==golden 直证）+ 计时摆针两例
（http_client_keepalive/mqtt_lwt_retain 定向重跑即过）+
process_list_smoke（oracle 陈旧，native 更优）；rto 7 = oracle 墙钟；
rcf 17 = oracle 编不过（其中 coll_postfix_init/server_mvc_timezone 是
oracle 未及的新特性用例，native 输出经 --expected 直证 golden-exact）；
mne 18 = 两侧同非零 stdout 逐字节一致。剩余非 pass 全部 oracle 侧或
已归因。

## v18o-14 (2026-09-30) — crossboot 5→13，双车道全绿

- ELF 车道(qemu-system-aarch64 裸机执行)与 PE-COFF 车道(结构校验)各 13/13:
  kernel8 kernel9 native_extern native_numeric_runtime dict_minimal dict_growth
  list_string_search native_lexical native_local_frame native_dict_out_address
  native_string_ops host_args_bounds native_generic_overload_fit
  (native_float_return_arg 仍排除，列为下批次候选)。
- 根因四件,全在 scripts/crossboot/stub.c:UNREACHED(strtod) 换成精确
  decimal→double 引擎(host 上 vs libc strtod 5 万例位级一致);MMU 关闭时一切
  访问皆 Device 内存、非对齐必炸(DFSC 0x21,与 SCTLR.A 无关)→ stub 编译加
  -mstrict-align;运行时 double 格式化走 snprintf("%.*e")→ stub 补上精确
  C %e/%f/%g 引擎(同一 bignum 机制,+%.* 与 %+03d);补 atoi 与
  setjmp/longjmp(EH)。host 验证:格式化 vs libc snprintf 12 种精度×2 万
  fuzz 逐字节一致。
- macOS 车道回归:电池 39/39(stub.c 只进 crossboot)。
- 旧账核销(本轮 probe 证据):instance async、async 内 for/foreach/finally/
  switch、f32 存储、LINQ 从 known_open 移除——全部实证已绿,纯记账滞后。
- 追补(同日):车道集扩到整个电池 —— 39/39 ELF + 39/39 PE-COFF,exit 0
  (DEFAULT_FIXTURES=39;native_float_return_arg 在 float printf 落地后收编)。
  新增 stub 面:round/pow(整数指数精确平方求幂、先正幂后取倒)、精确
  fabs/floor/ceil/fmax/fmin、sqrt 走硬件 fsqrt、sin/cos Cody-Waite+Taylor;
  kernel11 的 stdin+fgets(semihosting :tt);ngen_guard 因诚实 fopen 而
  取消降级 → mach_vm_read_overwrite 按裸机语义诚实实现(无 MMU,字节拷贝
  + KERN_SUCCESS)。首稿 x-x==0.0 把全部有限双精度当"已整数"返回——守卫改
  |x|≥2^53。

## v18o-15 (2026-09-30) — 评估批：flapper 复归因 + 三项 known_open 退役

- 全量 sweep 复测(种子同 v18o-14,零源码变更):570/624,ncf 仍为 0。
  mne 18 / rcf 17 / rto 7 / em 8 / om 4,桶族与 v18o-13 归因逐一相同。
- 11 例 em/om 定向重跑:exception_threads 与 mysql_async_nonblocking 当轮
  转 pass,http_forwarder_stream 转 rto,atomic_shared_table 与
  http_client_keepalive 在 em/om 间换签名——全部落在已归因 flapper 簇
  (线程时序/ref UAF/ref 握手崩溃/oracle 陈旧)。稳定态仍 574,±4 为
  逐轮成员噪声,非编译器回归。
- probe 证据退役三项 known_open(全部 PROBE-IDENTICAL):
  (1) stdlib API families——ncf 自 v18o-13 起为 0,"100 簇"是陈旧记账;
  (2) Int32Kind 字面量提升——i=2000000000 下 i*3 / i+i / j*3 / i*3L /
  (long)i*3 五形两侧逐字节一致(6000000000 / -294967296 / 1705032704 /
  6000000000 / 6000000000);
  (3) open_language_clusters 四簇全实证:块体 lambda+Func/Action、List
  初始化器、限定 catch 由既有 pass 用例覆盖;Dictionary 成员初始化器
  本轮新探 byte-exact;"无 select 查询"两侧同报
  expected 'select' in query expression——oracle 语言本无此特性,属 parity。
- known_open 仅剩 3:Mach-O ld-prime GOT assert(下一代码批:自制
  __nl_symbol_ptr 槽位,只许 reloc type 0/2/3/4)、Linux 用户态宿主层、
  x86_64 后端。电池复核 39/39。

### v18o-15b 追补(同日,代码批)—— Mach-O GOT assert 修复落地

- LoadSymRefGOT 不再发 GOT-load 对(type 5/6):每个 dylib-data 外部符号
  在自制 __DATA,__nl_symbol_ptr 段(S_NON_LAZY_SYMBOL_POINTERS)里占一个
  8 字节槽,槽内容挂 UNSIGNED extern reloc(type 0/length 3),链接期由
  连接器绑定;取址点改为普通 adrp+ldr(PAGE21 + PAGEOFF12)。PAGEOFF12
  一律 r_length=2(clang 对 64 位 ldr 也发 2;len=3 会被 ld 拒绝)。
- ELF 车道:槽跟在 .data 尾部,R_AARCH64_ABS64 进条件性 .rela.data
  (index 8,sh_info=.data);COFF 车道:槽在 .data,ADDR64(0xE),
  coffcheck 外部类型白名单加 0xE。无 GOT 引用对象与旧三段布局逐字节一致。
- bootstrap 链接器逐个揪出的错并复跑:段 cmdsize 312→392;ncmds 恒 2
  (nl 段在 segment 内);symOff 漏乘 8;TextFileOff 需随 nl 段 +80;
  .rela.data 的 sh_info 必须是 .data。定点 run.b4j4YF:
  stage2.o == stage3.o。
- 形状证据:kernel11.o 的 GOT_LOAD reloc 数 = 0,__nl_symbol_ptr 0x18
  (___stderrp/mach_task_self_/___stdinp),链接运行 golden-exact。
- 验收(新种子 /tmp/v18r/bin/zanc):电池 39/39 双配置、native_rt_core
  276 行金标、crossboot 39 ELF + 39 PE-COFF、全量 sweep 570/624——每个
  非 pass 都在已归因族内(时序 flapper 成员洗牌:tdengine_pool 入 rto、
  mqtt_lwt_retain 入 mne)。
- known_open 只剩 2:Linux 用户态宿主层、x86_64 后端。
- 环境异常记录:并发会话把在途第九批(io reactor co 助手)写进工作树,
  已快照至 /tmp/v18q/batch9_wip 并还原 HEAD,上述门禁均在 batch-8 提交态
  上运行。

## v18o-16 (2026-10-01) — elfcheck 补 .rela.data 校验 + Linux 车道精确侦察

- elfcheck.py 新增 .rela.data 段校验(v18o-15b 引入的段此前只有链接器实测):
  存在性 ⇔ l_.g.nl.* 槽全局存在;link/info 必须 .symtab/.data;每槽一条
  R_AARCH64_ABS64(257)、addend 0、8 对齐、落在 .data 槽尾、绑定 SHN_UNDEF
  外部符号、槽四字必须零初始化。负测双向:改重定位类型→FAIL、槽内容
  非零→FAIL;39×2 crossboot 复跑全绿(78 PASS/0 FAIL)。
- Linux 用户态车道侦察(写精确清单入 known_open):Zan 侧需 3 组 per-OS
  shim(__error→__errno_location、OSAtomic*→C11 原子、_NSGetExecutablePath→
  /proc/self/exe);编译器发射的 mach_task_self_/mach_vm_read_overwrite/
  ___stdXXXp 需同名 C shim;其余 trio 已可移植(clock_gettime/pthread/
  POSIX sockets,grepped)。两道环境阻塞:本机无 Linux 头/交叉 cc
  (无 zig/musl-cross/aarch64-linux-gnu-gcc);无执行载体(qemu linux-user
  不支持 macOS 宿主、brew qemu 无 qemu-aarch64、无 docker/lima/UTM/
  OrbStack)。任一载体到位即可按清单开工。
