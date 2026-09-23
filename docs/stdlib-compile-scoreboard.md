# stdlib 编译通过率记分牌（架构转向后的源驱动工作清单）

日期：2026-09-23 · v8 · 定点见下方 v8 节

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
