# stdlib 编译通过率记分牌（架构转向后的源驱动工作清单）

日期：2026-09-17 · 编译器 187e3d2（00b5589 + 链式指数修复 + host 名字闸门）

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
