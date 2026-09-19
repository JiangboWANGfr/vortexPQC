# DAC 论文重写稿

**Register-Resident Keccak and NTT Acceleration for ML-KEM and ML-DSA on a SIMT GPU**

本稿参考 `../paper/` 的技术内容与实验依据，并参考本地 DAC 论文的论证结构和图形表达。原 `../paper/` 的源文件、PDF、数据生成器和源码包保持不变。

采用标准 IEEEtran conference、Letter、10 pt 双栏；当前版本不限制正文页数，包含 **9 幅图、3 张表、15 条引用**。作者暂为匿名。全部图在 LaTeX 内使用 **TikZ / PGFPlots** 绘制，没有嵌入旧版 Matplotlib 图。

## 主线

> ML-KEM 与 ML-DSA 如何共用 SIMT 寄存器态 Keccak/NTT 硬件，以及指令融合到哪一级才对完整请求有价值？

Keccak Stage 是主要设计对象，Round 和 Pointer 验证不同执行边界。NTT/INTT 使用相同的 32-lane 寄存器布局、跨 lane 配对路由、可序列化的共享乘法器 bank 和写回流水；ML-KEM 与 ML-DSA 只选择不同的模数约减和逆变换收尾。16/8/4/2/1 的匹配 RTL/PPA 扫描选择 M2，完整请求比较同时覆盖两种算法，RV64 是位宽敏感性实验。

三项贡献围绕这个问题展开：

1. **Stage collective 接口。** 用固定的跨 lane 通信实现列归约、字置换和行逻辑；RV32 明确区分 L/H 目的值，通过普通寄存器依赖和单目的写回维持正确性。
2. **跨算法共享 NTT 单元。** ML-KEM 16 位与 ML-DSA 32 位蝴蝶共用可序列化乘法器 bank 和配对网络，通过各自原生库 hook 完成正逆 NTT；五点扫描选择两个乘法器，ML-DSA 普通 pointwise 还复用同一标量模乘路径，L5 累加保持软件 W32 映射。
3. **指令粒度的受控比较。** 区分软件映射、新指令和整轮融合的收益，再与完整请求、并发度和已测配置的整核实现成本联系起来。

不将 25-lane 映射、寄存器驻留或 whole-round collective 本身声称为首次提出；相应前作在 Introduction 和 Background 中交代。

## 章节与图

| 章节 | 论证任务 |
| --- | --- |
| I. Introduction | 问题、已有工作的基础、接口约束与三项贡献 |
| II. Background and Motivation | 两种算法的 NTT 与 Keccak 通信结构、强软件分母 |
| III. Design | 核心接入、Stage 数据通路、RV32 依赖、Round/Pointer、共享 NTT 与应用映射 |
| IV. Implementation and Evaluation | 方法、完整 KEM、八组合消融、融合控制、两算法 NTT 与 DSA pointwise 收益、最终阶段占比、整核 PPA、RV64 |
| V. Conclusion | 状态位置、指令粒度与并发必须共同决定设计点 |

图中只保留模块名、短标签和数学符号；约束和计时口径进入正文或图注。

| 图 | 文件 | 说明 |
| --- | --- | --- |
| 1 | `figures/keccak_dataflow.tex` | 5×5 状态的列归约、旋转/置换、行邻居依赖 |
| 2 | `figures/collective_core.tex` | GPR/ALU 接入，Stage 两级与 Round 四级流水 |
| 3 | `figures/rv32_dataflow.tex` | L/H 双轨依赖、6 次与 2 次 issue 的区别 |
| 4 | `figures/ntt_mapping.tex` | lane 5/21 的具体系数伙伴和共享模乘 |
| 5 | `figures/kem_results.tex` | 同一 XRT 构建的六后端、M1/M8 完整请求 |
| 6 | `figures/stage_ablation.tex` | θ、ρπ、χι 的八种软件/指令组合 |
| 7 | `figures/fusion_results.tex` | loop/expanded/Round，以及原语和应用收益的差别 |
| 8 | `figures/shared_ntt_results.tex` | 两算法 NTT 收益，以及独立对照的 DSA pointwise M1/M8 XRT 收益 |
| 9 | `figures/ntt_bank_tradeoff.tex` | 16/8/4/2/1 的 NTTBF 性能—DSP Pareto 与 NTT 层级资源趋势 |

历史 K-only 主结果：Stage 相对同布局 Shuffle 的完整 KEM 加速为 **1.462× / 1.552×**；同展开 Round 的置换收益为 **3.168× / 3.291×**，独立 SimX 完整 KEM 控制只有 **1.028× / 1.033×**。这些不同层次的结果在图 7 并列展示，并明确各自测量路径。图 8 使用共享 K/D 单元：NTT 条形和 pointwise 条形采用各自匹配的软件分母，收益不能相加。

DSA pointwise 的 M1/M8 XRT 周期分别减少 **12.538% / 11.687%**。M8 使用 8 个不同输入，各输入经历 2–11 次签名尝试；基线、映射版各自在 SimX/XRT 上逐字节校验，共 32 次完整请求通过，模型周期差不超过 **0.593%**。M8 使用本次重编译的匹配基线，保留此前 M1 数据快照，不将两个批次拼成相同输入的缩放曲线。

表 II 展示最终共享核心的 M1 阶段占比与剩余开销；DSA 的 absorb/squeeze 未单独隔离，归入 Rest。KEM 的 Rest 还包含 reduction。百分比由原始周期求和后统一舍入。探针相对未插桩请求增加 **6.398% / 0.666%** 的 KEM/DSA 周期。表 III 将旧 K-only 与新共享 K/D 的独立整核 PPA 分行列出。

## 数据与复现

- `main.tex`、`references.bib`：英文正文和文献。
- `data/`：仓库 `pqc/results/` CSV 的字节一致快照，包括共享 K/D NTT、16/8/4/2/1 bank 性能/PPA、ML-DSA pointwise M1/M8 和最终阶段测量。
- `generate_results.py`：仅使用 Python 标准库，从 CSV 生成数字宏、绘图数据和 PPA 表行。
- `assets/`：PGFPlots 数据、数字宏、表行、SHA-256 来源清单。
- `evidence_map.md`：每个论断/图表对应的数据、字段和比较边界。
- `literature_notes.md`：本地 DAC 论文的阅读记录和具体借鉴方式。
- `vortex_pqc_dac_draft.pdf`：编译稿。
- `vortex_pqc_dac_source.zip`：独立源码包，含全部绘图源码与数据，不包含参考论文全文。

仓库内编译：

```sh
make -C pqc/docs/dac_paper
```

独立源码包解压并进入目录后：

```sh
make BUILD="$PWD/build"
```

依赖：Python 3、latexmk、IEEEtran、TikZ、PGFPlots、BibTeX。无需 NumPy 或 Matplotlib。仓库内的编译中间文件写入 `build/dac_paper/latex/`。

正文主比较采用 XRT RTL simulation；八组合 Stage 消融来自单独的 processor RTL 实验，NTT bank 选择来自此前的匹配构建。同展开的完整 KEM 控制使用 SimX。各自保持原始测量口径，不把它们拼成同一个实验。PPA 是独立整核 post-route，未写成板上性能或活动功耗结果。

共享 K/D NTT 增加了新的 SimX、XRT 和 Vivado post-route 实验。匹配 bank 扫描的 M16/M2 NTT 层级分别使用 13,379/3,891 LUT、10,998/5,944 FF 和 80/10 DSP；M2 的独立 NTTBF RTL 周期比 M16 增加 2.963%，完整 XRT workload 最大增加 0.143%。原始结果与哈希见 `data/ntt_multiplier_bank_*.csv`。AI 辅助说明保留在正文末尾；作者信息和投稿声明由作者定稿。
