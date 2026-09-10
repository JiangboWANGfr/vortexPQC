# Keccak 加速器接口：RISQ-V 与 pointer PE 的区别

核对日期：2026-09-10。本文是论文写作的来源核对笔记；尚未并入 IEEE 正文和 PDF。

## 1. 分类依据

分类应看硬件指令传入什么、状态在两次指令之间保存在哪里、一次调用完成多少工作。C 包装函数接受数组指针，并不能证明底层指令采用指针接口；使用专用 Keccak 电路，也不代表它采用存储器接口。

| 设计 | 指令/控制接口 | 状态与计算粒度 |
| --- | --- | --- |
| 本项目 pointer `PQC.KECCAKF` | `rs1` 为 200-byte 状态地址 | AGU 读状态，PE 执行 24 轮，再原址写回；指令间状态在内存 |
| 本项目 SG25 stages | 两个源 GPR vector，单目的 GPR vector | 25 个 lane 的 GPR 保存状态；RV32 每轮六次 collective issue |
| 本项目 SG25 whole round | 两个源 GPR vector，单目的 GPR vector | 同样使用 lane GPR；RV32 每轮两次 collective issue |
| RISQ-V [1] | 隐式读取/更新 CPU 的 PQR；操作数选择轮次和复位 | 32 个 FPR + 18 个 GPR 保存 1600-bit 状态；一条指令一轮 |
| Berkeley SHA3 RoCC [2, 3] | 自定义指令提交消息地址、结果地址和消息长度 | 加速器自主读取消息并写回摘要；完整哈希接口 |
| MS3 [4] | TileLink 配置读地址、写地址、长度和启动信号 | DMA 传入消息、传出摘要；完整 SHA-3 加速器 |
| Karl et al. [5] | 比较寄存器耦合、MMIO/SRAM、MMIO/FIFO、LSU/FIFO | 同一平台评估多种接口及通信成本；不能把所有方案统称 pointer 指令 |
| Bolat et al. [6] | `shatr` 驱动专用执行单元 | 单元内 200-byte 专用状态寄存器；一条指令一轮 |
| HORCRUX [7] | `LOAD/STORE`、`KINIT/KABSORB`、`KSTART/KPERM` 等 | 专用 50×32-bit 寄存器文件；可触发完整 24 轮置换 |

本项目接口可核对 [AGU](../../../hw/rtl/pqc/VX_pqc_agu.sv)、[stage 单元](../../../hw/rtl/core/VX_alu_ksg25.sv) 和 [whole-round 单元](../../../hw/rtl/core/VX_alu_kround25.sv)。表中的 issue 数不是周期数。当前主实验每核各有一个对应单元，多个 warp 共享；SG25 的每个 warp 有自己的架构寄存器状态。

## 2. RISQ-V 的直接证据

RISQ-V 的 §3.8 和图 5 明确复用 CPU 的全部 32×32-bit FPR 及 18×32-bit GPR，合称 PQR。单轮电路并行读取并写回这些寄存器。§4 的 Keccak Operation Class 明确说明 `keccak.f1600` 的 `rs1` 选择轮次，`rs2` 控制复位。这里的 `rs1` 不是内存地址。[1]

因此，RISQ-V 在“寄存器保存状态、专用整轮电路”上更接近本项目的 whole-round 比较器，但它没有 SIMT lane 协作，也没有本项目 RV32 低/高半字的单目的 vector 写回接口。RISQ-V 还明确讨论跨采样过程保持寄存器状态，不能把寄存器常驻本身写成本项目首创。[1]

## 3. 哪些是 pointer PE 的相近先例

Berkeley SHA3 RoCC 是自定义指令提交地址、加速器访问内存的直接接口先例。2015 年资料是技术报告；官方 Chipyard 文档详细描述了消息地址、结果地址、长度构成的执行上下文及自主读写过程。[2, 3]

MS3 是已发表的 DMA 存储器耦合例子，其 §3.5 和图 5–6 明确给出地址配置及 DMA 读写流程。它处理完整消息和摘要，不能称为本项目 200-byte 原位置换指令的复现。[4]

Karl 等人的 TECS 论文 §3 比较四种接口，§4.2 进一步讨论吸收阶段的寄存器搬移成本。这是本项目讨论通信开销和端到端收益的重要前作；其中 MMIO/FIFO 与 LSU/FIFO 接口也不等于一次传入状态地址的指令。[5]

## 4. 对论文表述的影响

可以把本项目 pointer PE 称为 **memory-pointer full-permutation baseline**，把 RISQ-V 称为 **CPU-register-coupled round ISE**，把 Bolat/HORCRUX 归入专用状态设计。三者都可能使用专用 Keccak 电路，接口与状态归属仍然不同。

贡献应聚焦 RV32 SIMT 上跨 lane 的阶段接口、显式单目的写回，以及 stage / GPR whole round / pointer PE 在同一核上的吞吐、端到端和 PPA 对照。寄存器常驻、整轮硬件、自主存储器访问及通信成本比较均已有先例；最终新颖性还应结合正文已有的 ML-Cube 和 NVIDIA collective 语义前作讨论。

## 来源

1. Fritzmann, Sigl, Sepúlveda, **RISQ-V: Tightly Coupled RISC-V Accelerators for Post-Quantum Cryptography**, TCHES 2020(4), 239–280. [原文](https://eprint.iacr.org/2020/446.pdf)，§3.8、图 5、§4 Keccak Operation Class。
2. Schmidt, Izraelevitz, **A Fast Parameterized SHA3 Accelerator**, UCB/EECS-2015-204, 2015. [Berkeley 技术报告页面](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2015/EECS-2015-204.html)。出版信息来自该页面；接口细节另由 [3] 核对。
3. Chipyard, **SHA3 RoCC Accelerator**. [官方文档](https://chipyard.readthedocs.io/en/stable/Generators/SHA3.html)，Technical Details；这是实现文档，不是另一篇论文。
4. **A multimode SHA-3 accelerator based on RISC-V system**, IEICE Electronics Express 21(11), 20240156, 2024. [期刊页面](https://www.jstage.jst.go.jp/article/elex/21/11/21_21.20240156/_article/-char/en)、[原文](https://www.jstage.jst.go.jp/article/elex/21/11/21_21.20240156/_pdf)，§3.5。
5. Karl, Schupp, Sigl, **Performance and Communication Cost of Hardware Accelerators for Hashing in Post-Quantum Cryptography**, ACM TECS 24(5), Article 66, 2025. [DOI](https://doi.org/10.1145/3676965)、[作者机构全文](https://publica-rest.fraunhofer.de/server/api/core/bitstreams/62370613-ab72-412b-bdd1-e72fa635143a/content)，§3–4；扩展自 COSADE 2024。
6. Bolat et al., **Microarchitecture Design and Benchmarking of Custom SHA-3 Instruction for RISC-V**, arXiv:2508.20653v1, 2025，预印本。 [原文](https://arxiv.org/html/2508.20653v1)，§III。
7. **HORCRUX: A Complete PQC RISC-V eXtension Architecture**, arXiv:2607.13939v1, 2026，预印本。 [原文](https://arxiv.org/html/2607.13939v1)，§IV-A/B、表 III。
