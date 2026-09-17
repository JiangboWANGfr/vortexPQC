# DAC 独立论文稿

工作题目：**Warp-Resident NTT and Keccak Collectives for ML-KEM on a RISC-V GPU**。

中文题意：**面向 RISC-V GPU 上 ML-KEM 的 warp 寄存器态 NTT 与 Keccak 协作指令**。

本目录与 `../paper/` 独立，原论文的源文件、PDF、数据生成器和源码包均未修改。
当前采用标准 IEEEtran conference、Letter、10 pt 双栏：**6 页正文 + 第 7 页参考文献，7 幅图、3 张表、15 条参考文献**。作者信息暂为匿名。
这是已有实验支撑的完整第一稿，不代表已经完成投稿前的新颖性评估或作者审核。

## 主线与三项贡献

建议围绕一个问题写：

> NTT 与 Keccak 的跨 lane 通信结构不同，如何把它们接入普通 SIMT 寄存器与单目的写回通路，并以完整 ML-KEM 的收益决定指令粒度和硬件资源？

论文按“通信需求 → 状态布局 → 操作数与写回机制 → 数据通路 → 完整应用验证”展开。NTT 与 Keccak 是两个有不同通信需求的实例，Stage、Round、Pointer 是执行边界的对照，RV64 是敏感性实验。

1. **将密码运算的多输入、多输出需求适配到现有 SIMT 接口。**
   NTT 用 pair-low lane 提供 twiddle，一次 CT 模乘产生两个结果，各 lane 写自己的 `rd`；Keccak 使用明确的 RV32 L/H 输出选择，两个指令读取相同旧状态，通过普通寄存器依赖维护正确性。重点是操作数、参与范围、输出和依赖机制，不是声称首次发现寄存器驻留或 25-lane 布局。
2. **根据通信结构配置具体硬件。**
   NTT 使用静态 XOR 路由与 16 个乘法器：CT 一拍产品，GS 复用 bank 计算 twiddle 产品和 Barrett quotient 产品，NTTMUL 分两拍处理 32 lanes。Keccak 使用 Stage/整轮两种流水实现。它们接入相同的普通 operand/result 接口，但没有声称共享同一算术单元或物理 shuffle 网络。
3. **用完整 ML-KEM 揭示融合与并发的取舍。**
   单独分离软件协作、新指令和整轮融合的收益。共同核心 XRT 中，Stage 相对 SG25 软件为 1.462×/1.552×，Round 为 1.520×/1.583×（M1/M8）。同展开 XRT 原语对照中 Round 比 Stage 快 3.291×；独立同展开 SimX 完整 KEM 对照只有 1.028×/1.033×。M1 Round 比 Pointer 快 1.130×，M8 Pointer 比 Round 快 1.374×。这些是选择边界，不是某方案在所有指标上获胜。

第二项的资源证据：历史同配置 full-bank→half-bank 对照减少 14,767 LUT、1,565 FF、16 DSP，完整 KEM 周期最大变化 0.041%。当前严格 RV32IM 独立实现中，Stage/Round 相对 NTT-only 的 LUT 增量为 1.129%/0.898%，两者均闭合 250 MHz。历史 half-bank 对照包含 FPU，不能与整数核心的绝对面积跨表相减。

**投稿价值仍需重点打磨机制区别。** 已有 RISQ-V 的寄存器耦合设计、ML-Cube 的 25-thread Keccak、公开专利的 warp collective round；“实现了 NTT 和 Keccak”“做了 RTL 和综合”“移植到 RV64”各自不足以单独构成强创新。三项贡献是当前证据下合理的组织方式，不是首次性结论。

## 六页内容安排

| 内容 | 对应正文 | 主要证据 |
| --- | --- | --- |
| 问题、设计原则、贡献 | I | 不同通信图与单目的写回限制 |
| NTT/Keccak 布局及强软件分母 | II；图 1 | `a[l+32k]`、25-lane state、smem32 对照 |
| 接口与实现机制 | III；图 2、3、4；表 I | pair twiddle、CT/GS bank、L/H 依赖、mask、流水 |
| 方法及完整 KEM | IV-A/B；图 5 | 同 runtime 六后端、M1/M8、KAT |
| 融合、剩余开销、资源选择 | IV-C/D；图 6、7；表 II | 同展开控制、互斥 profile、half-bank |
| 整核面积与 RV64 | IV-E；表 III | F/D 关闭，独立 Vivado post-route |
| 前作定位、范围、结论 | V/VI | 明确既有先例和当前验证边界 |

原先 W4T4 的历史消融、ML-DSA 外推、SG5 fence 调试经过、多个旧综合轮次不进入主文，以免六页变成开发日志。

## 图是否需要更多

需要补强机制图。本次逐图题核对的七篇参考论文有 7–12 张编号图、1–3 张表，子图不另计；具体计数见阅读笔记。它们大量使用布局、数据通路、依赖和调度图，不只是实验柱状图。

本稿从五图版调整为 **四张设计图 + 三张结果图**，新增图替换相应长段说明，保留标准字号和页边距：

| 图 | 回答的问题 | 内容 |
| --- | --- | --- |
| 1 | 为什么 NTT 与 Keccak 要用不同 collective？ | NTT lane×register 布局；Keccak 列归约、固定 gather 和行邻居 |
| 2 | 单元接在 GPU 的什么位置？ | 普通 GPR/ALU 写回与 Pointer/LSU 两种执行边界 |
| 3 | 一次模乘如何服务两个 lane，16 个乘法器如何复用？ | lane5/21 CT 实例；CT、GS、NTTMUL 的 bank phase |
| 4 | RV32 怎么用单目的指令产生完整状态？ | Stage 六次 issue、Round 两次 issue及相同旧状态依赖 |
| 5 | 完整 ML-KEM 到底快多少？ | 同 runtime 六后端，M1/M8 并列 |
| 6 | 整轮融合的原语收益有多少？ | 同展开 Stage/Round 的 XRT 置换区间 |
| 7 | 原语更快后时间花在哪里？ | XRT 互斥阶段 profile，明确剩余工作 |

无需为了接近 PacQ 的 12 图而继续拆图。七幅图已经覆盖“动机/布局 → 接口/实现 → 应用 → 解释”的证据链。新增数据图应有新的实验问题和数据支持。

## 文献与格式

[阅读笔记](literature_notes.md)记录了目录盘点和重点论文的实际版式、页码、可借鉴方法以及引用边界。33 个本地 PDF 已盘点，重点阅读其中 10 篇；没有把其余论文称为已精读。主文引用了其中与论证直接相关的 6 篇 DAC 论文，并补充标准、处理器与直接前作。

DAC 2026 官方页面存在模板口径不一致：[Research FAQ](https://dac.com/2026/research-frequently-asked-questions)写 IEEE，[Research Manuscript Submissions](https://dac.com/2026/research-manuscript-submissions)写 ACM；两者都要求最多 6 页正文和 1 页仅含参考文献。本稿按用户要求使用 IEEE 模板，未把它称为目标届最终投稿模板。核查日期：2026-09-17。

正文保留了独立的 AI 内容说明，说明工具参与起草、文献组织和制图代码；实验数据来自已有归档，未生成或补造实验结果。作者、单位、投稿届次和最终声明仍需作者审核。

## 文件与复现

- `main.tex`：英文正文；`references.bib`：独立参考文献库。
- `vortex_pqc_dac_draft.pdf`：6+1 页稿件。
- `assets/`：3 张数据图、数字宏、PPA 表行和来源 SHA-256 清单。
- `figures/`：新增布局、Keccak 依赖的 TikZ 源文件；另两幅设计图在 `main.tex` 中。
- `data/`：12 份原始结果表的字节一致快照，来源为仓库 `pqc/results/`。
- `generate_results.py`：从这些快照重算图、主结果宏和面积表。
- `evidence_map.md`：论断到实验文件的对应关系与比较边界。
- `vortex_pqc_dac_source.zip`：独立源码、数据、图和本说明；不包含参考论文全文。

在仓库中编译：

```sh
make -C pqc/docs/dac_paper
```

中间文件写入仓库 `build/dac_paper/latex`，只更新本目录的新 PDF。独立源码包解压后可运行：

```sh
make BUILD="$PWD/build"
```

依赖：Python 3、NumPy、Matplotlib、latexmk、IEEEtran、TikZ、BibTeX。
正文数据归档基于仓库 `084f79465`；各实验自身 revision、ELF/runtime/config/log hash 以 CSV 为准。`mlkem-native` revision 为 `1d7b486c4db3bbc620b009d05e4f69eca9e68d22`。

## 后续应优先补强的证据

这次只写论文，没有重跑仿真或综合。若继续以 DAC 为目标，优先级为：

1. 更明确地论证接口机制相对 RISQ-V、既有 GPU 映射和 collective-round 先例的实质区别。不能用更多对比表替代这个论证。
2. 增加请求并发点和不同输入，检验 M1/M8 crossover 的稳定性，并用资源计数解释原因；当前只观察到现象，未把因果归给某一个 cache/仲裁机制。
3. 若要比较吞吐/面积，补齐严格 F/D-off Pointer PPA，并把性能与独立面积配置对应起来；当前不能使用旧含 FPU 的 Pointer 面积。
4. 若要将论题扩大到通用 PQC，完成 ML-DSA 路径；若要讨论能效，需 workload activity 与功耗证据。当前正文只主张 ML-KEM 的结果。
