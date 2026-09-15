# zan-selfhost

Zan 编译器的自举车道（lane）：用 Zan 写 Zan 编译器，终局目标是**完全抛弃 C 和
LLVM 依赖**。本仓库从 zan-lang monorepo 独立出来，以避开主仓库的并行开发噪音。

## 引导契约（bootstrap contract）

- 本仓库**不构建** gen0。gen0 = pin 的 zanc（zan-lang 的 C host 编译器），
  通过环境变量 `ZANC` 传入。
- `src/selfhost/*.zan` 必须能被 pin 的 gen0 版本编译（自举子集）。
- 需要新语言能力时的流程：向 zan-lang 主仓库提需求 → 主仓库发布新版 →
  本仓库 bump `BOOTSTRAP_ZANC_VERSION`。禁止在本仓库内 fork/魔改 zanc。
- 语义权威最终是 `src/selfhost/`；main 仓库的 C host 是过渡期的引导工具。

当前 pin：`BOOTSTRAP_ZANC_VERSION = zan-lang main @ 2b847bbd（待首次构建后填入版本号）`

## 当前验收边界

原生路线已达到一个可核验固定点：`build/native-bootstrap/run.nYdDdi/`
的 `stage2.o == stage3.o`，native-born stage2 回归 39/39 通过，另有 15 项
集成回归（转换/委托/显式布局/C 字符串读取/packed out 写回/整数窄化边界等）全部通过。
该代际链直接输出 macOS arm64 Mach-O 对象，由系统 `ld` 链接，不调用 clang、
不传项目 C runtime 对象；动态依赖只有 libSystem。最新增量还包含 `var` 局部
类型推断、Dictionary 整数键校验、`Keys/Values` 链式类型与 packed int 数组
元素的 TryGetValue 写回宽度修复。详情与哈希见
[原生自举验收记录](docs/native-bootstrap-acceptance.md)。

这不代表完整语言能力已与 C host 等价。后续源码修改需重验固定点，完整兼容性
仍以阶段对拍结果为准，不能因自举闭合就提前删除参考编译器。

统一的编译器输入清单位于 `scripts/selfhost_sources.txt`，Shell 与 CMake
自举脚本共同读取它。定向测试入口为 `scripts/native_regression.py`；与原 C
编译器对拍使用 `scripts/native_parity.py`，全量扫描必须显式传 `--all`。
对拍记录编译、链接、运行退出码和原始 stdout/stderr，保留种子与输入哈希。

## 首次闭合记录（2026-09-13）

- **M0 已达成**：自举闭合恢复。`gen0 → gen1 → g2.ll → gen2（clang 链接）→
  g3.ll`，`g2.ll == g3.ll`（5,073,570 字节，首次在 macOS 上闭合）。
  顺带完成 `tests/selfhost/prog1.zan` 端到端语义验证（stdout 与 golden 一致）。
- 为达成 M0 修复的编译器 bug（均在 `src/selfhost/`）：
  1. `ref`/`out` 形参声明与调用点 `ref` 实参（B6-SH1 主因；值类型 + 局部变量
     子集，ARC 类型明确报错）—— parser/binder/checker/irgen 四处；
  2. stdlib 路径拼接用 Windows 反斜杠，POSIX 上 stdlib 自动拉入从未生效
     （`Exception` unknown type 的真因）—— 改为 `/`；
  3. 重载构造函数 LLVM 符号撞名（如 `StreamWriter` 的两个 ctor 都发
     `@X_ctor`）—— 新增 `CtorSym` 按参数类型 mangling，`new` 与基类链
     按实参个数选重载（`FindCtorArity`）。
- `stdlib/` 是裁剪后的快照（仅 `System/Exception.zan`）：bootstrap 只消费
  这一个 stdlib 类型，其余均为 `using` 目录级拉入带进来的死代码，且部分
  引用 macOS 缺失的运行时符号。扩展 stdlib 快照 = 显式任务，逐文件验证可链接。

## 资产分类（决定哪些代码值得投入）

| 分类 | 文件 | 待遇 |
|------|------|------|
| 终局资产 | lexer / parser / binder / checker / diag / ast / token / main | 长期维护，语言能力落点 |
| 过渡资产 | irgen / irgen_expr / irgen_stmt / irgen_async（发 LLVM IR 文本） | 去 LLVM 后重写；只修正确性，不做优化投入 |
| 过渡资产 | jsongen / dbgen（镜像 C host pass） | 跟随终局资产，非后端 |
| 待淘汰 | 依赖 clang 链接 .ll 的闭包步骤（scripts/bootstrap.sh 第 3 步） | 自研后端落地后删除 |

## 里程碑

1. **M0（当前）— 恢复自举闭合**：修复 B6-SH1（selfhost parser 不认
   `ref`/`out` 形参声明，dbgen.zan 已使用；另 Stopwatch.Start 成员解析缺口），
   gen1→g2→gen3 跑通，`g2.ll == g3.ll` 字节一致。
2. **M1 — 最小自研后端**：x86-64 -O0 直出机器码（Mach-O/ELF 目标文件），
   不经 LLVM IR / clang，能编译运行 `tests/selfhost/prog1.zan`。
3. **M2 — 自举脱离 clang**：固定点测试改为 gen2==gen3 且全程不调 clang。
4. **M3 — 后端扩展与质量**：arm64、wasm32 目标；-O1 级优化；与 C host 产物
   的 conformance 等价验证。
5. **M4 — 收编**：selfhost 编译器替换主仓库 C host 成为默认编译器；
   zan-lang 的 `src/compiler/` 退役。

## 与 zan-lang 主仓库的同步协议

- **主 → 本仓库**：语言 spec / bootstrap 子集能力变更，按需 bump 引导版本。
- **本仓库 → 主**：语言行为分歧时本仓库为准（与 zan-lang `docs/BOOTSTRAP.md`
  的既有政策一致），修正同步回 C host。
- 主仓库**不要**直接改 `src/selfhost/`（本目录为唯一活跃副本）；本仓库
  不改主仓库的 runtime/C host，除非走引导契约。

## 运行

```sh
ZANC=/path/to/zanc ./scripts/bootstrap.sh          # 过渡 LLVM 固定点
SEED=/path/to/gen1 RT_OBJS="/path/to/runtime.o ..." ./scripts/native_bootstrap.sh
python3 scripts/native_regression.py --seed /path/to/gen1 --runtime "/path/to/runtime.o ..."
python3 scripts/native_parity.py --seed /path/to/gen1 --reference /path/to/gen0 \
  --runtime "/path/to/runtime.o ..." generic_statics exceptions_basic
```
