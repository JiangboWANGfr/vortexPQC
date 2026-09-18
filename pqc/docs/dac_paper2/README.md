# DAC 论文重写稿（dac_paper2）

题目：**Warp-Collective NTT and Keccak Instructions for ML-KEM on a RISC-V SIMT GPU**

本目录是 `../dac_paper/` 的完整重写，独立编译，不修改原目录。内容来源与 `../paper/`
（IEEE 长稿）相同的归档实验数据；写法参考
`paper_ref/DAC_2022-2026_GPU_Crypto_Hardware_Papers_17/pdfs` 中 PacQ、NTT-PIM、CHAM 等
DAC 论文的结构：摘要按“背景→问题→缺口→方法→数字”组织，引言按“背景→通信差异→已有方案为何不够→
本文思路→三条带数字的贡献”组织，相关工作并入 Background，评估以完整 ML-KEM 为主线，局限只作行内说明。

当前版本：IEEEtran conference、Letter、双栏，**正文 6 页 + 第 7 页仅参考文献（`\clearpage` 分隔）**，
8 幅图、4 张表、15 条参考文献。全部图为 TikZ/pgfplots 源码，图内文字尽量少，图意由图题说明。

## 章节安排

| 章 | 内容 | 图表 |
| --- | --- | --- |
| I Introduction | ML-KEM 吞吐场景；NTT 与 Keccak 的 warp 内通信差异；shuffle 软件与 pointer 引擎的代价；缺口；三条贡献 | 图 1 |
| II Background and Motivation | ML-KEM 与 Vortex 基础；`a[l+32k]` 布局与 `x+5y` 布局的通信图；六种映射的置换周期；相关工作 | 图 1、表 I |
| III Warp-Collective Instruction Design | 流水线位置与统一契约；pair butterfly（pair-low twiddle、各 lane 写自己的 rd）；Stage/Round 与 RV32 L/H；完整请求编程 | 图 2、3、4，表 II |
| IV Implementation | 16 乘法器 bank 的 beat 复用；两级/四级 Keccak 流水；验证与物理流程 | 图 3(b) |
| V Evaluation | 方法；完整 ML-KEM 六后端 M1/M8 及交叉点解释；融合粒度与 profile；NTT 通信与 bank 共享；面积/时序/RV64；设计点选择（表 IV）；范围与局限 | 图 5、6、7、8，表 III、IV |
| VI Conclusion | 一段，重复摘要中的三组数字 | |

## 图

| 图 | 文件 | 内容 |
| --- | --- | --- |
| 1 | `figures/comm.tex` | (a) 32 lane × 8 寄存器网格，lane 5 的寄存器内伙伴与跨 lane 伙伴；(b) 5×5 状态的 θ 列、ρπ gather、χ 行 |
| 2 | `figures/overview.tex` | Vortex 流水线中三个新执行单元与 pointer PE 的位置 |
| 3 | `figures/ntt_pair.tex` | (a) 距离 16 的 CT pair：rs1/rs2/rd 向量与单次模乘；(b) 16 乘法器 bank 的 CT/GS/NTTMUL beat |
| 4 | `figures/keccak_unit.tex` | (a) Stage 单元两级流水与六次 issue；(b) Round 单元四级流水与两次 issue |
| 5 | `figures/kem_xrt.tex` | 完整 ML-KEM-768 六后端 M1/M8 周期（pgfplots，读 `assets/kem_xrt.dat`） |
| 6 | `figures/fusion.tex` | 同展开 Round/Stage 在原语与完整 KEM 两个尺度的比值 |
| 7 | `figures/profile.tex` | XRT M1 互斥阶段 profile（堆叠横条） |
| 8 | `figures/ntt_direct.tex` | 四种 NTT 实现的孤立正/逆变换周期 |

## 数据与数字

`data/` 是仓库 `pqc/results/` 的字节一致快照，比 `../dac_paper/data/` 多两份：
`ablation_mlkem.csv`（引言中的 66%/13% 标量份额）和 `ntt_keccak_backend_ppa.csv`
（含 FPU 的旧核心上 pointer PE 面积，用于说明 pointer 的面积代价）。

`generate_results.py` 从这些快照生成 `assets/numbers.tex`（正文所有数字宏）、
`assets/*.dat`（pgfplots 数据）、`assets/ppa_rows.tex`（表 III 行）和
`assets/source_manifest.json`（SHA-256 与派生数字）。正文中没有手工输入的实验数字。

主要口径与 `../dac_paper/evidence_map.md` 一致：完整 KEM 用统一 XRT 运行时；融合原语对照用
P64 XRT 控制；融合应用对照用同展开 SimX 控制并在文中标明；profile 用 XRT M1 仪表化构建；
NTT 变换与 KEM 增量用 SimX 消融族；表 III 用严格 RV32IM/RV64IM 独立 post-route；
pointer 面积来自含 FPU 的旧核心族，只作倍数比较，不与表 III 绝对值相减。

## 编译

```sh
make -C pqc/docs/dac_paper2
```

中间文件写入 `build/dac_paper2/latex`，输出 `vortex_pqc_dac2_draft.pdf`。
依赖：Python 3、latexmk、IEEEtran、TikZ、pgfplots、pgfplotstable、BibTeX。

## 仍需作者判断

1. 作者、单位、致谢与 AI 使用声明（本稿未含）。
2. M8 交叉点的解释目前是与数据一致的推断（IPC 0.28 vs 0.47、协作映射占满 32 lane 数据通路），
   未归因到具体调度或缓存机制；若能补计数器证据会更强。
