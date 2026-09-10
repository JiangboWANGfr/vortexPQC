# IEEE 论文初稿与数据口径

题目：**Bottleneck-Guided Keccak Acceleration on an RV32 SIMT GPU**

中文工作题目：**面向 RV32 SIMT GPU 的瓶颈驱动 Keccak 加速设计**。

本稿采用 IEEEtran conference 模板、Letter 纸张、双栏排版，正文为英文。作者和单位暂用匿名信息，日期为 2026 年 9 月。尚未选定投稿会议，因此没有自行套用某个会议的页数限制。当前 PDF 为 9 页，含 15 张表、5 幅图和 11 条参考文献。

论文资料统一位于仓库的 pqc/docs/paper/：[PDF 初稿](vortex_pqc_ieee_draft.pdf)、[独立源码包](vortex_pqc_ieee_source.zip)、[正文源文件](main.tex) 和 [参考文献](references.bib)。这次工作整理已有实验，没有重新执行硬件实验。

## 1. 论文主线与贡献

论文按“软件基线和瓶颈 → SG25 软件映射 → 三阶段 ISE → GPR 型 KROUND 比较器 → pointer PE → 端到端与实现成本”展开。

贡献应落在以下三点：

1. 说明不同软件基线、输入和并行映射如何改变加速收益，区分 Keccak/NTT 消融、请求批处理和状态内部协作。
2. 在 RV32 上实现分阶段 subgroup Keccak 接口：状态由 25 个 lane 的普通 GPR 保存，使用显式低/高半字、单目的寄存器写回；给出完整的八组合阶段消融。
3. 实现 GPR 型整轮比较器，与三阶段实现及 pointer PE 在相同主平台比较，联系置换吞吐、完整 ML-KEM、LSU 操作数、FPGA 整核面积/时序及独立单元 ASIC 面积。

当前实现不需要改为 RV64。64 位 Keccak word 在 RV32 上由两个 32 位寄存器表示；三阶段每轮六次 collective issue，整轮比较器每轮两次。这些是发射数，不是完整操作的周期数，也不是整个内核的峰值寄存器用量。

不主张“首次使用 25 个线程实现 Keccak”或“首个 warp collective Keccak round”。ML-Cube 已描述 25-thread shuffle 映射；NVIDIA 的 US 2026/0222190 A1 已公开整轮 collective 语义。本稿的 KROUND 是本项目实现的 RV32 比较器，其周期和 PPA 不是 NVIDIA 硬件的数据。参考文献中提供了对应来源。

## 2. ML-KEM：Keccak 与 NTT 到底占多少

下表来自 [ablation_mlkem.csv](../../../pqc/results/ablation_mlkem.csv) 的数值行。配置为 **RV32、1 核、4 warps × 4 threads、单 lane、SimX、L2/L3 关闭**；工作负载是 ML-KEM-768 的 KeyGen + Encaps + Decaps，输入为该历史实验的 FIPS 203 KAT coins。

统一分母是带计数器的 profile 基线 **33,492,153 cycles**。

| 项目 | 相对基线移除的 cycles | 占基线比例 | 单独移除该项后的 cycles | 消融加速比 |
| --- | ---: | ---: | ---: | ---: |
| Keccak-f[1600] | 21,986,475 | **65.65%** | 11,505,678 | 2.911× |
| NTT + INTT | 4,243,182 | **12.67%** | 29,248,971 | 1.145× |
| 代数残差 | 7,262,496 | **21.68%** | 未测联合消融 | 不适用 |

计算式为：份额 = (原始 profile cycles − 单项消融 cycles) / 原始 profile cycles。

残差等于基线减去两次独立消融的差值。它不是单独测得的互斥计时区间，也不能全部称为 absorb/squeeze、访存或采样开销。它可能包含非 NTT 多项式运算、采样、编码、吸收/挤出、分配和控制；目前没有足够的独立计时进一步拆分。两项消融的可加性也没有通过联合消融验证。

三个 profile arm 都记录 144 次 scalar permutation。24 次 x4 wrapper 调用所触发的 scalar 工作已经包含在 144 中，不能再加上 4 × 24。NTT/INTT 为 15 次正变换、9 次逆变换。消融的密码输出有意无效，仅用于估算组件成本；调用数一致不等于证明全部数据相关路径完全不变。

旧说明文字中的 67.72%、11.97%、72.3% 不能作为当前表格结论。稿件使用当前 CSV 数值重新计算，并保留实验输入和分母。

## 3. ML-DSA：实测与外推分开

来源：[ablation_mldsa.csv](../../../pqc/results/ablation_mldsa.csv)。同样是历史单 lane、4w × 4t 配置，分别使用 low/full 内存预算。

| 范围 / 内存预算 | 原始 profile cycles | Keccak 份额 | NTT/INTT 份额 | 组件份额依据 |
| --- | ---: | ---: | ---: | --- |
| KeyGen / low | 42,022,206 | 69.56% | 7.13% | 单项消融实测 |
| KeyGen / full | 39,121,351 | 74.44% | 7.62% | 单项消融实测 |
| 完整流程 / low | 189,935,043 | 63.34% | 14.78% | 调用数 × 单次组件成本外推 |
| 完整流程 / full | 142,119,160 | 62.76% | 18.12% | 调用数 × 单次组件成本外推 |

签名和验证不能直接沿用删掉哈希的消融方法：这样会改变拒绝采样或使采样循环不能正常终止。因此后两行的总周期来自原始执行，但组件份额是外推，不能写成独立计时实测。

完整流程的 low/full 输入分别产生 782/582 次 permutation 和 103/95 次变换，属于该固定输入的结果。本稿不使用未完整归档且有 histogram overflow 的历史分布来声称中位数、p95 或跨输入稳定性。

## 4. 之前的软件基线

| 对照 | 已归档结论 | 分母与适用范围 |
| --- | --- | --- |
| Reference C 与 PQRV RV32 汇编 | 单次 Keccak：151,872.1 → 134,320.2 cycles，1.131×；完整 ML-KEM：33,066,490 → 30,615,811 cycles，1.080× | 同一历史 C/assembly 实验，4w × 4t |
| C / 汇编 / pointer 三列 | 32,778,558 / 30,321,833 / 11,527,027 cycles；pointer 相对 C 2.844×、相对汇编 2.631× | 另一个内部匹配实验，不能与上一行跨表相除 |
| 软件协作 forward NTT | 库参考 133,526 cycles；协作 L=1/2/4 为 211,506 / 106,297 / 53,682；L=4 相对库参考 2.487× | ML-KEM 正变换微基准；不代表完整 NTT/INTT 或整个 ML-KEM 的加速 |
| 独立请求批处理 | ML-KEM 在 M=8 时吞吐为 M=1 的 6.654×；ML-DSA 在 M=4 时为 3.940× | 8w × 4t；吞吐定义为 M × C(1) / C(M)，不是单请求延迟 |
| 历史 x4 lane 映射与缓存 | 单独给出 M × L 与 L2 开关扫描；L 增大并非始终获益 | 与 SG25 单状态协作是不同映射，不能把收益直接相乘 |
| ML-DSA 内存预算 | full 预算下 L=4 相对 full/L=1 为 1.601×；相对 low/L=1 为 2.118× | 两个分母对应不同问题，均在正文明确标注 |

SG5 保留为历史布局对照，其已归档比较使用 16-thread build，不进入当前 W32 的同平台性能排名。

## 5. 主实验：相同 8w × 32t 平台

完整 ML-KEM 主实验是 RV32、1 核、8w × 32t、32-lane ALU/LSU、L2/L3 关闭、固定输入字节 0…95。所有 arm 使用 serial FIPS-202 路径，每请求 143 次 permutation，通过相同正确性检查。以下是 SimX whole-launch device cycles，包含设备端入口/收尾，排除主机传输。

| 后端 | M=1 cycles | 相对同 M 的 SG1 | M=8 cycles | 相对同 M 的 SG1 |
| --- | ---: | ---: | ---: | ---: |
| SG1 C | 34,688,954 | 1.000× | 53,176,031 | 1.000× |
| SG25 shuffle | 14,320,553 | 2.422× | 26,588,208 | 2.000× |
| SG25 三阶段 | 11,749,447 | 2.952× | 19,228,330 | 2.766× |
| SG25 整轮 KROUND | 11,532,487 | 3.008× | 18,989,395 | 2.800× |
| pointer PE | 12,430,412 | 2.791× | 17,013,577 | 3.126× |

三阶段相对 SG1 的 2.952×/2.766× 包含软件映射收益。指令本身应与 SG25 shuffle 比较，对应 1.219×/1.383×。KROUND 相对三阶段的完整 ML-KEM 加速只有 1.019×/1.013×。

纯 permutation 的 RTL 阶段消融使用 4/8 次链式置换的跨度差，抵消固定开销。三阶段相对 shuffle 的单状态收益为 9.41×，八 warp 完成置换吞吐收益为 12.91×。另一个 64 次链式置换实验中，KROUND 相对三阶段的八 warp 吞吐为 4.093×。两个实验的分母与链长不同；后者也包含 literal-round 展开和阶段版循环控制的差异，尚缺匹配展开方式的独立控制。

上述 143 次的新主实验与历史 144 次的 KAT profile 不同。不能把 65.65% 直接当作新主平台的 Keccak 份额。

## 6. 成本与频率的写法

250 MHz 相同目标下，V80 整核 base 为 323,072 LUT / 263,116 FF，三阶段为 325,288 / 265,015，增加 0.69% LUT / 0.72% FF。整轮为 325,832 / 269,371。这里比较的是既定 W32 核上增加单元的成本；从窄核拓宽到 W32 的成本并不包含在该百分比中，正文另列了历史核宽度面积。

300 MHz 目标的 base、三阶段和整轮均有负 WNS，不能写成“整核已经闭合 300 MHz”。报告的 Fmax 是路由后 STA 估计，不是板上实测时钟。250 MHz 表中的零 WNS 也表示没有正时序余量。

整轮独立单元的 FPGA LUT 少于三阶段，但 FF 约为其 2.99×；ASAP7 下总 cell area 约为其 2.123×。整轮在本次纯置换的整核 throughput/LUT 指标上仍然领先约 4.086×，因此不能预设三阶段在所有面积归一化指标上获胜。

## 7. 表格与来源映射

以下路径均相对仓库根目录的 pqc/results/；生成清单保存 15 个源文件的 SHA-256。

| 正文内容 | 源文件 |
| --- | --- |
| 表 II、图 1 的 ML-KEM | ablation_mlkem.csv |
| 表 III、图 1 的 ML-DSA | ablation_mldsa.csv |
| 表 IV 软件 C/汇编 | keccak_baseline_columns.csv |
| 表 V 软件 NTT | ntt_cooperative.csv |
| 表 VI 独立请求批处理 | batch_baseline.csv |
| 图 2 M × L × L2 | mlkem_MxL.csv |
| 表 VII 内存预算 | mldsa_ram_x_lane.csv |
| 历史 pointer 对照文字 | keccak_ise_simx.csv、keccak_ise_mldsa.csv |
| 表 VIII、图 4 阶段消融 | keccak_sg25_stages.csv |
| 表 IX 整轮比较器 | keccak_kround25.csv |
| 表 X–XII、图 5 完整 ML-KEM、阶段周期、访存/栈 | keccak_sg25_mlkem.csv |
| 表 XIII–XV、PPA 分析 | keccak_sg25_ppa.csv |
| 历史核宽度成本 | core_config_v80.csv |
| 历史 SG5 配置说明 | keccak_sg5.csv |

表 I 汇总各实验配置，图 3 为本稿架构图。表格序号对应当前版本，后续插入内容后需同步。

LSU 列表示整个 launch 的 load/store 操作计数，不能直接写成 DRAM 字节流量或仅 spill 次数；stack peak 是栈水位，不能等同峰值活跃 GPR 数。正文保留这些区别。

## 8. 再生成与复核

需要 Python 3、NumPy、Matplotlib，以及提供 IEEEtran、TikZ、latexmk、BibTeX 的 TeX Live。可以从已有配置的 build 目录调用：

~~~sh
make -C ../pqc/docs/paper
~~~

也可在本目录直接执行 make。此目标只生成文档，不编译 Vortex 核心或测试程序。所有中间结果进入仓库 build/ieee_pqc/，最终 PDF 保存在本目录。

[generate_results.py](generate_results.py) 从 11 份结构化结果表生成数值、表格和四张矢量统计图，检查消融差值、调用数、NTT 正确性、SimX/RTL 阶段指令计数、差分样本 ELF 一致性、整轮样本链长、完整 ML-KEM 正确性和阶段周期之和。另将文字引用的四份历史文件纳入哈希清单；这些文件不是重新执行实验后的结果。

独立源码包含已生成的表格、图、原始 CSV 快照和 provenance.json，可在 Overleaf 选择 pdfLaTeX 编译 main.tex，也可在解压目录执行：

~~~sh
latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
~~~

独立包的表格数值是打包时的快照；更换 CSV 后，应在原仓库重新运行生成器再打包，直接重新编译 LaTeX 不会自动更新数据。

当前依赖版本：

| 依赖 | 本地提交 |
| --- | --- |
| mlkem-native | 1d7b486c4db3bbc620b009d05e4f69eca9e68d22 |
| mldsa-native | 19d32614b342840e02010825fa9ff4c22b855653 |

provenance.json 记录本次文档生成时的仓库 HEAD、工作区是否有修改、输入文件哈希和导出数值。它标识论文输入快照；由于实现包含本地修改，HEAD 不能单独标识实验代码版本。这些哈希也不能补回缺失的历史源码快照。

## 9. 投稿前最值得补的实验

1. 在最终 8w × 32t、同一 serial-FIPS-202 输入上补独立计时：Keccak permutation、NTT/INTT、absorb/extract，以及能够避免嵌套重复计数的其他阶段。加速后剩余开销是目前最关键的空缺。
2. 补同 W32 的 PQRV 汇编 arm、SG5/SG25 匹配映射对照，以及三阶段/整轮相同展开方式的控制实验。
3. 扩展多输入与混合 batch；如果论文覆盖 ML-DSA collective 性能，则完成其端到端集成和输入分布实验。
4. 根据最终投稿目标补板上运行、功耗/能耗和更完整系统 PPA。现有 XRT 证据是集成仿真，ASIC 数据是独立单元映射结果。

这些项目已在英文稿限制部分明确写出，没有用推算值填成实测结果。
