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
ZANC=/path/to/zanc ./scripts/bootstrap.sh          # 完整自举闭包
ZANC=/path/to/zanc zanc src/selfhost/main.zan ...  # 见 tests/run_selfhost.cmake
```
