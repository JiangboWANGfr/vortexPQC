# IEEE 论文初稿与数据口径

题目：**Bottleneck-Guided Keccak Acceleration on an RV32 SIMT GPU**

中文工作题目：**面向 RV32 SIMT GPU 的瓶颈驱动 Keccak 加速设计**。

本稿采用 IEEEtran conference 模板、Letter 纸张、双栏排版，正文为英文。作者和单位暂用匿名信息，日期为 2026 年 9 月。尚未选定投稿会议，因此没有自行套用某个会议的页数限制。当前 PDF 为 12 页，含 23 张表、5 幅图和 11 条参考文献。

论文资料统一位于仓库的 pqc/docs/paper/：[PDF 初稿](vortex_pqc_ieee_draft.pdf)、[独立源码包](vortex_pqc_ieee_source.zip)、[正文源文件](main.tex) 和 [参考文献](references.bib)。本次更新补齐同 W32 的 PQRV 汇编、SG1/SG5/SG25 映射和 Stage/KROUND 相同展开方式的控制实验，并保留直接阶段计时及严格 RV32IM/RV64IM 的功能、性能和 Vivado post-route 对比。

[Keccak 相关工作接口核对](keccak_related_work.md) 区分 RISQ-V 的 CPU 寄存器耦合、专用状态单元及 pointer/DMA 加速器，并提供原文依据；该补充笔记尚未并入正文和 PDF。

## 1. 论文主线与贡献

论文按“软件基线和瓶颈 → SG25 软件映射 → 三阶段 ISE → GPR 型 KROUND 比较器 → pointer PE → 端到端与实现成本”展开。

贡献应落在以下三点：

1. 说明不同软件基线、输入和并行映射如何改变加速收益，区分 Keccak/NTT 消融、请求批处理和状态内部协作。
2. 在 RV32 上实现分阶段 subgroup Keccak 接口：状态由 25 个 lane 的普通 GPR 保存，使用显式低/高半字、单目的寄存器写回；给出完整的八组合阶段消融。
3. 实现 GPR 型整轮比较器，与三阶段实现及 pointer PE 在相同主平台比较，联系置换吞吐、完整 ML-KEM、LSU 操作数、FPGA 整核面积/时序及独立单元 ASIC 面积。

论文主设计仍为 RV32。64 位 Keccak word 在 RV32 上由两个 32 位寄存器表示；三阶段每轮六次 collective issue，整轮比较器每轮两次。RV64 分别降为三次和一次。匹配实验在两种 XLEN 都关闭 F/D，并保持窄 NTT 指令不变，用来分离整数宽化和 Keccak 发射数变化。

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

残差等于基线减去两次独立消融的差值。它不是单独测得的互斥计时区间，也不能全部称为 absorb/squeeze、访存或采样开销。它可能包含非 NTT 多项式运算、采样、编码、吸收/挤出、分配和控制；该历史配置没有足够的独立计时进一步拆分。两项消融的可加性也没有通过联合消融验证。

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

历史 SG5 文件使用 16-thread build，不与 W32 数据直接相除。新增 W32 对照使用相同核心和计时边界，另行归档于 `keccak_w32_controls.csv`；其 fence 引起的时序模型限制见下文。

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

合并 NTT 后又完成了一组最终统一实验：固定 `NTT=reg32 NTTBF=ise NTTMUL=ise ARITH=all ARITH_MUL=ise SERIAL=1`，只切换 Keccak 后端。输入统一为 FIPS 203 KAT coins，每请求均为 140 次 permutation、15 次 NTT、9 次 INTT，并逐字节验证 pk/sk/ct/ss。

| 后端 | M=1 cycles | 相对 SG1 | M=8 cycles | 相对 SG1 |
| --- | ---: | ---: | ---: | ---: |
| SG1 C | 27,739,317 | 1.000× | 44,824,668 | 1.000× |
| SG25 shuffle | 7,900,300 | 3.511× | 19,176,807 | 2.337× |
| SG25 三阶段 | 5,409,748 | 5.128× | 12,246,844 | 3.660× |
| SG25 整轮 KROUND | 5,198,475 | 5.336× | 11,947,257 | 3.752× |
| pointer PE | 5,911,515 | 4.692× | 8,901,903 | 5.035× |

M=1 时 stage 和 KROUND 分别比 pointer PE 快 1.093× 和 1.137×；M=8 时 pointer 分别快 1.376× 和 1.342×。这是真正的延迟/吞吐交叉，不能只选一个 batch 点下结论。stage 和 KROUND 的 XRT M=1 均通过，分别为 5,409,552 和 5,203,321 cycles，与 SimX 的 whole-launch 差为 0.004% 和 0.093%。

匹配实验使用 RV32IM/RV64IM，二者均关闭 F/D，固定最终 half-bank NTT、全部协作算术、同一 FIPS 203 KAT 输入和 W8T32 配置，只分别启用 Stage 或 KROUND：

| 后端 / XLEN | M=1 instructions | M=1 cycles | M=8 instructions | M=8 cycles |
| --- | ---: | ---: | ---: | ---: |
| Stage RV32IM | 436,262 | 5,409,748 | 3,490,096 | 12,170,437 |
| Stage RV64IM | 405,716 | 5,189,658 | 3,245,728 | 12,225,892 |
| KROUND RV32IM | 415,822 | 5,198,475 | 3,326,576 | 11,937,913 |
| KROUND RV64IM | 391,996 | 5,043,599 | 3,135,968 | 11,970,704 |

RV64 将两种 Keccak 后端的 collective issue 数减半，但完整 ML-KEM 的 Stage/KROUND 指令只减少 7.002%/5.730%。M=1 cycles 分别减少 4.068%/2.979%，M=8 则增加 0.456%/0.275%。SimX、RTL 和 XRT 的 M=1 KAT 全部通过且退休指令一致；最大 SimX/RTL 差为 0.515%，最大 XRT/RTL 差为 0.055%。

最终 RV32IM W8T32 的 M1 直接 profile 用互斥区间计时，absorb/squeeze 排除嵌套 permutation；下表为 XRT 占带探针总区间的比例：

| 后端 | Permute | Absorb | Squeeze | NTT + INTT | Mulcache + basemul + reduce | 残差 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Stage | 4.964% | 7.423% | 4.239% | 2.344% | 4.529% | 76.501% |
| KROUND | 1.266% | 7.646% | 5.256% | 2.398% | 4.673% | 78.761% |
| Pointer PE | 0.516% | 9.521% | 17.263% | 2.114% | 4.183% | 66.402% |

三组 KAT 全部通过，SimX/RTL/XRT 的退休指令数完全一致；最大 SimX/RTL 和 XRT/RTL 区间差为 0.785%/0.078%。相对同配置无探针 M1，Stage/KROUND/Pointer 的探针开销为 7.189%/8.299%/6.632%，因此不用这张表重算端到端加速比。残差还包含采样、编码/压缩、比较、控制、访存、wrapper、分配和探针开销，不是一个密码原语。M8 仅保留无探针的全局 makespan，不对重叠的单请求区间求和。Pointer 的独立配置 M8 为 8,330,412 cycles，不替换上面全开统一核的 8,901,903 cycles。

### W32 匹配软件与展开控制

新增数据来自 `38c56304f`，固定 RV32IM、F/D 关闭、1 core、W8T32、最终 half-bank NTT，以及同时启用 Stage/KROUND 的同一个核心。所有 ML-KEM arm 都保留 serial FIPS-202、W32 NTT 和协作算术，每请求为 140 次置换、15 次正变换和 9 次逆变换。下表以 PQRV 为更强的软件分母，周期为完整 launch，KAT 全部通过。

| 后端 | M1 cycles | 相对 PQRV | M8 cycles | 相对 PQRV |
| --- | ---: | ---: | ---: | ---: |
| SG1 C | 27,739,317 | 0.891× | 44,211,503 | 0.855× |
| PQRV | 24,714,477 | 1.000× | 37,785,504 | 1.000× |
| SG25 shuffle | 7,900,300 | 3.128× | 19,204,022 | 1.968× |
| Stage 循环 | 5,409,748 | 4.569× | 12,170,437 | 3.105× |
| Stage 全展开 | 5,341,994 | 4.626× | 12,336,487 | 3.063× |
| KROUND 全展开 | 5,198,475 | 4.754× | 11,937,913 | 3.165× |

同展开控制使用每状态 64 次连续置换、每 warp 一个状态；下表为 SimX cycles/已完成置换。输入与输出被 CTA barrier 隔离在计时区间外。

| 后端 | 每置换 ISE 次数 | W1 | W8 |
| --- | ---: | ---: | ---: |
| Stage 循环 | 144 | 2,079.312 | 479.463 |
| Stage 全展开 | 144 | 1,584.906 | 401.688 |
| KROUND 全展开 | 48 | 502.406 | 121.727 |

反汇编确认两个应用中的置换函数一致：循环 Stage 44 B、全展开 Stage 676 B、KROUND 196 B；后两者没有 round-loop branch。等展开后 KROUND 的置换收益为 3.155× / 3.300×，不能继续将旧循环版分母的约 4× 写成纯硬件融合收益。对应完整 ML-KEM 收益仅为 1.028× / 1.033×。Stage 展开减少 M1 cycles 1.252%，但 M8 增加 1.364%。

软件映射微基准同时测每 warp 一个状态和最大打包：SG1/PQRV 为 32 个，SG5 为 6 个，SG25 为 1 个；按完成的置换总数归一化。这是两个占用点的控制实验，并非搜索所有 occupancy 后的最优吞吐。装满 warp 不保证吞吐更好。

**SG5 的功能通过不能写成时序模型通过。** 它保留原来的每轮两次 transpose fence；两个 XRT 对照与 SimX 的整 launch 周期差分别为 6.932% 和 12.223%，超过未修改的 5% 阈值。RTL 的 cache flush 尚未被 SimX fence 完整建模，同配置的独立 fence/barrier 探针也复现差异。CSV 分别记录数值检查结果和 `parity_status=FAIL`；正文保留这个限制。

后续同步对照新增 `MAPPING=sg5 SG5_SYNC=barrier`，使用每 warp 独立的两次 BAR，原两组差距降为 **1.387% / 1.193%**。但八 warp 的 S6/P2 和 S1/P8 仍有 **5.442% / 9.627%**；原 fence 在这两个点分别为 20.439% / 26.588%。双 warp barrier 的 11 组中有 9 组通过时序阈值，不能写成 SG5 全面闭合。CTA barrier、LMEM 和单次 barrier 也各有失败点。全部 31 对、62 次运行数值正确且成对退休指令完全一致，时序结果为 **20 对 PASS、11 对 FAIL**，未修改 5% 阈值。默认仍为 fence；barrier 保留为实验选项。

之后的轨迹对照定位到独立的 cache 模型错误：RTL 从暂存的回填数据直接转发，SimX 却等待缓存数组写入完成。`6455855c2` 修正转发时机，并保留 write-through store 在回填期间的合并顺序。缓存单元测试覆盖数组深度、读写顺序、响应反压和回填附近的 store，在 RV32/RV64 均通过。保持同一 barrier ELF，重新运行全部 11 对 SimX/XRT 后，数值和指令数全部一致，整 launch 时序全部通过未修改的 5% 阈值；W8/S6/P2 与 W8/S1/P8 分别降为 **4.068% / 3.294%**，最大整 launch 差为 **4.225%**。但 W8/S2/P2 的独立置换区间仍差 **5.633%**。这轮没有复测其他同步方案，也没有补齐真实 fence flush；历史表格保留原模型快照。

来源：`keccak_w32_controls.csv`、`mlkem_w32_controls.csv`、`keccak_sg5_sync.csv`，新增模型复测单列于 `keccak_sg5_cache_forward.csv`，轨迹摘要和回归证据见 `keccak_sg5_cache_forward_trace.json`。后三种诊断实现的源码补丁归档于 `keccak_sg5_sync_sources.zip`，分别基于 `68845dd72`；保留的 fence/barrier 实现为 `1bd44cf34`。构建命令、原始日志目录和验证范围见 `docs/proposals/keccak_sg25_proposal.md`。同步轮只改软件与实验入口，后续轮修正 SimX 模型；均未改 RTL 或重新综合。

## 6. 成本与频率的写法

NTT 不是只有一种候选。仓库先后保留了 scalar library、shared-array
cooperative、`reg32+SHFL`、`smem32` transpose、`NTTMUL.K`、full-bank
`NTTBF+NTTMUL.K`，以及最终的 half-bank `NTTBF+NTTMUL.K`。论文主比较固定
最后一种：一个 W32 warp 保存一个 transform，前三层在 lane 内执行，后四层使用
XOR 16/8/4/2 的 CT/GS butterfly；16 个有符号 16×16 乘法器由 32 lanes 共享。
硬件对五种合法编码距离使用静态路由，该内核使用其中四种。

历史同源 PPA 扫描中，无 NTT ISE、注册 full bank、half bank 分别使用
334,917 / 356,447 / 341,680 LUT，265,187 / 272,430 / 270,865 FF，
以及 160 / 192 / 176 DSP；三者均包含相同 pointer Keccak PE。最终 half bank
相对该历史无 NTT ISE 分母增加 2.019% LUT、2.141% FF 和 16 DSP；相对 full
bank 减少 14,767 LUT、1,565 FF 和 16 DSP，完整 KEM 周期最大变化 0.041%。
这些数据用于解释 NTT 架构选择，不能与新的独立开关构建跨表相减。

新 PPA 使用提交 `8798cb61a` 的独立开关，在同一 RV32、1 core、W8/T32、
XCV80、Vivado 2025.1、OPT3 和 250 MHz 约束下重新跑四次完整实现。每组均从头
综合和布局布线，不复用增量 checkpoint：

| 构建 | LUT | 相对 NTT-only | FF | 相对 NTT-only | WNS |
| --- | ---: | ---: | ---: | ---: | ---: |
| NTT-only | 330,530 | — | 267,838 | — | 0.000 ns |
| NTT + Stage | 334,561 | +4,031（+1.219%） | 269,806 | +1,968（+0.735%） | +0.006 ns |
| NTT + KROUND | 332,710 | +2,180（+0.660%） | 274,237 | +6,399（+2.389%） | +0.003 ns |
| NTT + Pointer | 341,751 | +11,221（+3.395%） | 271,058 | +3,220（+1.202%） | +0.004 ns |

四组都是 113 RAMB36 / 40 RAMB18 / 176 DSP，且 route report 为零 routing
errors。Stage、KROUND、Pointer 层级分别为 902/2,025、2,503/6,130、
10,414/4,556 LUT/FF；整核差值还包含 decode、仲裁、布局和路由影响，因此不应
强制等于层级数字。所有构建都闭合 250 MHz，但余量很小。历史 300 MHz 数据仍
保留在 `keccak_sg25_ppa.csv`，只用于独立单元及旧配置说明，不再作为最终整核
Keccak 面积分母。

严格的整数 PPA 对比使用提交 `766b66a74`，固定 V80、1 core、W8T32、
OPT3、250 MHz 和最终 half-bank NTT。RV32IM 与 RV64IM 都关闭 F/D；每个
XLEN 分别从头实现 NTT-only、NTT + Stage 和 NTT + KROUND：

| XLEN / 构建 | LUT | FF | BRAM tiles | DSP | WNS | STA Fmax |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| RV32IM NTT-only | 220,950 | 131,798 | 133.0 | 112 | +0.004 ns | 250.3 MHz |
| RV32IM NTT + Stage | 223,444 | 133,512 | 133.0 | 112 | +0.004 ns | 250.3 MHz |
| RV32IM NTT + KROUND | 222,935 | 138,065 | 133.0 | 112 | +0.007 ns | 250.4 MHz |
| RV64IM NTT-only | 429,067 | 228,266 | 261.5 | 16 | 0.000 ns | 250.0 MHz |
| RV64IM NTT + Stage | 432,843 | 232,350 | 261.5 | 16 | -0.148 ns | 241.1 MHz |
| RV64IM NTT + KROUND | 431,004 | 234,240 | 261.5 | 16 | -0.013 ns | 249.2 MHz |

六组 route report 都是零 routing errors。RV32IM 三组和 RV64IM NTT-only 闭合
250 MHz；RV64IM Stage/KROUND 完成 post-route physical optimization，但仍按
`timing_unmet` 记录。相对各自 NTT-only，Stage 在 RV32IM/RV64IM 增加
2,494/3,776 LUT 和 1,714/4,084 FF；KROUND 增加 1,985/1,937 LUT 和
6,267/5,974 FF。所有组的 FPU 层级均为零，NTT 始终为 16 DSP。整核 DSP 从
RV32IM 的 112 减到 RV64IM 的 16，是因为通用 MULDIV 从 96 DSP 改为零 DSP 的
串行 RV64 实现，不是 NTT 资源下降。每行只有一个 OOC seed，不能外推为器件或
布局无关结论。

整轮独立单元的 FPGA LUT 少于三阶段，但 FF 约为其 2.99×；ASAP7 下总 cell area 约为其 2.123×。使用历史循环版 Stage 分母时，整轮的纯置换整核 throughput/LUT 领先约 4.115×。这个比值包含展开方式差异，不能写成同展开控制的收益；后者单独报告。不能预设三阶段在所有面积归一化指标上获胜。

## 7. 表格与来源映射

以下路径均相对仓库根目录的 pqc/results/；生成清单保存 25 份 CSV、1 份诊断源码包和 1 份轨迹证据 JSON，共 27 个输入文件的 SHA-256。

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
| 表 XIII 最终 NTT+Keccak 五后端完整 ML-KEM | keccak_ntt_unified_mlkem.csv |
| 表 XIV–XV、同 W32 软件映射与循环展开控制 | keccak_w32_controls.csv |
| 表 XVI、同 W32 PQRV 与展开控制的完整 ML-KEM | mlkem_w32_controls.csv |
| SG5 同步实现、失败点与源码对照 | keccak_sg5_sync.csv、keccak_sg5_sync_sources.zip |
| SG5 cache 转发模型修复与复测 | keccak_sg5_cache_forward.csv、keccak_sg5_cache_forward_trace.json |
| 表 XVII、最终 RV32IM 直接阶段 profile | mlkem_phase_profile.csv |
| 表 XVIII、匹配 RV32IM/RV64IM 完整 ML-KEM | rv32im_rv64im_keccak.csv |
| 表 XIX、最终 NTT 架构 PPA | ntt_v80_ppa.csv |
| 表 XX、统一 NTT 分母的 Keccak 整核 PPA | ntt_keccak_backend_ppa.csv |
| 表 XXI、匹配 RV32IM/RV64IM 整核 PPA | rv32im_rv64im_keccak_ppa.csv |
| 表 XXII–XXIII、独立单元 PPA 分析 | keccak_sg25_ppa.csv |
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

[generate_results.py](generate_results.py) 从 21 份结构化结果表生成数值、表格和四张矢量统计图，检查消融差值、调用数、NTT 正确性、SimX/RTL 阶段指令计数、差分样本 ELF 一致性、整轮样本链长、两组完整 ML-KEM 正确性、阶段周期之和、最终阶段 bucket 的互斥求和、探针开销与三模型一致性、RV32/RV64 KAT 与 model parity、SG5 同步实验的 PASS/FAIL 分类、cache 修复前后的二进制与指令一致性及 launch/span 差距，以及两组 XLEN 的综合配置、路由和闭合状态。另将文字引用的四份历史 CSV、一份 SG5 诊断源码包和一份轨迹证据 JSON 纳入哈希清单，共记录 27 个输入文件；这些历史文件不是重新执行实验后的结果。

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

provenance.json 记录本次文档生成时的仓库 HEAD、工作区是否有修改、输入文件哈希和导出数值。它标识论文输入快照；新 W32 控制的代码快照为 `38c56304f`，CSV 同时记录各 ELF 的哈希。部分历史实验带有当时的本地修改，论文生成时的 HEAD 和 CSV 哈希不能补回这些历史源码快照。

## 9. 投稿前最值得补的实验

最终 8w × 32t 的直接阶段计时、匹配软件映射与展开控制已完成。剩余优先项为：

1. 补齐真实 fence 的 cache-flush 建模，并追踪 SG5 W8/S2/P2 独立区间仍有的 5.633% 差距。BAR/LSU/cache 对照已定位并修复 fill 转发时机；保留的双 warp barrier 方案全部 11 对整 launch 已通过，但不能外推到其他同步方案或所有独立区间。
2. 扩展多输入与混合 batch；如果论文覆盖 ML-DSA collective 性能，则完成其端到端集成和输入分布实验。
3. 根据最终投稿目标补板上运行、功耗/能耗和更完整系统 PPA。现有 XRT 证据是集成仿真，ASIC 数据是独立单元映射结果。

这些项目已在英文稿限制部分明确写出，没有用推算值填成实测结果。
