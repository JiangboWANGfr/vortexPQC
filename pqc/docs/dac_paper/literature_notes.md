# DAC 参考论文阅读笔记

阅读日期：2026-09-17。来源目录：
`/home/jiangbowang/aphdcode/paper_ref/DAC_2022-2026_GPU_Crypto_Hardware_Papers_17/pdfs`。

目录实际有 **33 个 PDF**，文件夹名和旧 README 中的“17”不是当前文件数。
本次盘点全部文件的实际页数，重点阅读以下 10 篇；并非宣称 33 篇全部精读。
另核对本地 RISQ-V、Nannipieri NTT ISE 原文及现有 Keccak 前作笔记。

## 最值得用于本论文的参考

| 优先级 | 论文及直接来源 | 借鉴内容 | 本地 PDF 的实际版式 |
| --- | --- | --- | --- |
| 最高 | [PacQ, DAC 2025](https://doi.org/10.1109/DAC63849.2025.11132101) | p1 数据表示在哪里失效；p3–4 映射、算术、整核对应；p5–6 每个机制各有消融 | IEEE 7 页；结论延到 p7，不能当严格 6+1 样板 |
| 最高 | [NTT-PIM, DAC 2023](https://doi.org/10.1109/DAC56929.2023.10247747) | p2–3 伙伴位置决定单 buffer/双 buffer 指令；p3–4 映射、接口、twiddle；p5–6 参数敏感性 | IEEE 6 页，参考文献在 p6 |
| 最高 | [CHAM, DAC 2023](https://doi.org/10.1109/DAC56929.2023.10247696) | p1 明确区分 NTT 算子与应用收益；p3–4 数据流；p5 硬件/算子；p6 完整应用 | IEEE 6 页，参考文献在 p6 |
| 高 | [ZK-Flex, DAC 2026](https://doi.org/10.1145/3770743.3803941) | p1–2 精度、阶段占比带来的利用率问题；p3 框架；p4 机制；p5–6 消融和应用 | ACM 7 页，正文 p1–6、参考独立 p7 |
| 高 | [BP-NTT, DAC 2023](https://doi.org/10.1109/DAC56929.2023.10247691) | p3 布局；p4 小位宽实例讲清机制；p5 公平比较；p6 位宽和规模 | IEEE 6 页，参考文献在 p6 |
| 高 | [SynGPU, DAC 2025](https://doi.org/10.1109/DAC63849.2025.11132753) | p1–2 两个明确瓶颈；p3 算法；p4–5 架构；p5–6 去掉 rearrangement 的消融 | IEEE 7 页，正文 p1–6、参考独立 p7 |
| 中 | [GEM, DAC 2025](https://doi.org/10.1109/DAC63849.2025.11132713) | circuit heterogeneity 与 SIMT 冲突导出执行抽象；区分单次延迟与吞吐，保留不利负载 | IEEE 7 页，正文 p1–6、参考独立 p7 |
| 中 | [GATSPI, DAC 2022](https://doi.org/10.1145/3489517.3530601) | p4 区分 kernel 与 application；p5–6 profiling、消融与实际工作流 | 本地 ACM arXiv v1 为 7 页，参考跨 p6–7；正式版 pp.1231–1236 |
| 中 | [MATCHA, DAC 2022](https://doi.org/10.1145/3489517.3530435) | p2 三项具体机制贡献；p3 breakdown；p5 正确性/资源；p6 分析延迟与吞吐 | ACM 6 页，参考在 p6 |
| 背景 | [CryptoPIM, DAC 2020](https://doi.org/10.1109/DAC18072.2020.9218730) | NTT 数据移动与近存算术的前作；不是普通 SIMT GPR collective | 本地是 **16 页单栏 ePrint**，正文至 p13，参考 p13–16 |

作者、年份、DOI 依据本地原文出版信息、作者机构或出版记录核对。
主文使用前六篇；GEM/GATSPI/MATCHA/CryptoPIM用于结构和范围判断，没有为了凑引用数量加入正文。

## 图表数量与用途核查

以下按实际图题、表题逐一核对，并检查渲染页；子图不另计，正文里的 Fig./Table 引用不计为新图表。

| 论文 | 编号图 | 编号表 | 图的用途 |
| --- | ---: | ---: | --- |
| PacQ | 12 | 2 | 1–4 背景/问题，4–6 布局/乘法器/架构，7–12 实验/消融 |
| SynGPU | 9 | 2 | 2–3 动机，1、4–8 协作/算法/数据路径/映射，8(b)、9 结果 |
| NTT-PIM | 8 | 3 | 1、3 背景，2、4–6 硬件/映射/流水，7–8 敏感性 |
| BP-NTT | 8 | 1 | 1–3 基础/动机，4–7 系统/布局/乘法实例，8 结果 |
| CHAM | 8 | 3 | 2 动机/设计探索，1、3–4 架构/NTT/twiddle，5 实现布局，6–8 结果 |
| ZK-Flex | 8 | 2 | 1–3 协议/工作负载/设计动机，4–7 框架/算术/调度，8 实验 |
| GEM | 7 | 2 | 1 动机/总览，2–7 执行器/编译/映射/编码，结果主要在表 II |

分类可有重叠：同一图可以同时解释动机与新机制。上述文件的页数和模板不同，不能把计数解读成统一的六页投稿要求。

我们的七图安排把机制图从两幅增至四幅，结果图保持三幅。新增的是状态布局/通信图，以及 RV32 L/H 依赖图；CT 图补充 half-bank phase。优先用图代替难以理解的机制描述，不以继续增加柱状图凑数量。

## 具体怎么借鉴

### PacQ：从数据布局推导硬件，而不是列模块

p1 Fig.1 指出压缩权重在 DRAM/L2 有效，却在 RF/计算前解包而失去优势。
p2 的贡献有依赖关系：packing/dataflow → 对应乘法器 → SIMT 集成。
p3 Fig.3–4 展示 warp/octets 映射导致的取数行为；p4 Fig.5–6 将算术机制与整核接入放在一起。

用于我们：先画 NTT 的寄存器内/跨 lane 伙伴，再解释 NTTMUL、NTTBF 和 bank 数量；先画 Keccak 列归约、固定置换与行邻居，再引出 Stage/整轮。**不能按“先做了哪个后端”排序论文。**

### NTT-PIM：伙伴所在位置决定操作粒度

p2–3 从 DRAM 行切换和计算/传输比说明为什么一个 buffer 不足、增加一个能解决什么。
p3 的 C1 在一个 atom buffer 中做多层，C2 在两个 buffer 间做一层向量蝶形。
这种分解与我们的前三层 lane-local、后四层 XOR-pair 在逻辑上相似，但存储域与体系结构接口不同。

用于我们：写清 `a[l+32k]` → `len=128/64/32` 对应寄存器伙伴 → `len=16/8/4/2` 对应 lane 伙伴。
不能把“按通信距离分层”“原地更新”“减少数据搬移”本身写成首创。

### CHAM：原语提速必须回到应用

p1 直接提出只加速 NTT、keyswitch 等小操作可能不能显著改善应用。
Fig.1 系统流水、Fig.2 设计选择、Fig.3/4 NTT 微结构、Table II 实现资源、Table III 算子、Fig.7/8 应用形成完整证据链。

用于我们：置换 3.29× 与完整 KEM 约 3% 是不同层次的结果，profile 用来解释这个差别。CHAM 的实际 FPGA 应用测量也提醒我们明确写 **XRT RTL simulation + post-route implementation**，不能写成板上执行。

### ZK-Flex：贡献对应具体瓶颈，保留不利结果

p1–2 把问题压缩为多精度算术与 POLY/EC 阶段比例变化造成的利用率问题；p2 Fig.2 提供动机。
p2 的贡献包括软件机制、Toom–Cook TCore 与 linked-list memory，而不是只有“提出加速器”。
p6 在部分应用面积效率不如 LegoZK 时解释利用率原因，没有只挑获胜项。

用于我们：如实保留 Pointer 的 M8 优势和 Round 的 FF 成本。我们的 NTT/Keccak 是集成在同一 GPU 的独立单元，不能称作 ZK-Flex 式的统一算术资源复用。

### BP-NTT：一个具体实例比长篇 RTL 描述有效

p3 画 bit-parallel 布局，p4 用 3-bit 例子展示 carry/shift 机制，p5–6 再证实资源和规模效果。
用于我们：图 3 具体到 lane5、lane21，标出 `rs1`、pair-low `rs2` 和两个 `rd`；解释 GS 为什么需要第二次物理乘法。
其 SRAM/CPU 的带宽结论不能直接当成 Vortex 的已测瓶颈。

### SynGPU、GEM、GATSPI、MATCHA

- SynGPU 的瓶颈与机制一一对应，并用移除数据重排的控制证明效果。我们对应的是 full/half bank、software/ISE、同等 round 展开。
- GEM 从工作负载和 SIMT 的不匹配导出执行抽象。它的延迟/吞吐区分适合解释 M1/M8 胜者不同，不能只展示一个 batch 点。
- GATSPI 区分 kernel runtime、应用 runtime 和实际工作流，我们应区分 permutation span、完整 KEM launch、实现面积/时序。
- MATCHA 把近似算术的有效性与性能同时验证；我们只借鉴“机制必须有验证”，不能移植 TFHE 的近似运算，也不能在没有活动功耗时宣称能效。

## 新颖性的直接边界

1. RISQ-V 已有 CPU 寄存器耦合 NTT/Keccak、整轮和多层处理。它不是 pointer PE。
2. Nannipieri 等已有模乘、reduce、twiddle 和单蝶形指令。窄位宽模乘并非新概念。
3. ML-Cube §5.1.1 使用 25-thread Keccak shuffle，并将该映射归于更早的 CUDA-SSL（ICCST 2017）。不能声称首次 25-lane GPU Keccak。
4. NVIDIA US2026/0222190 A1 的 Listing 3 已描述 64-bit warp collective round。我们的 RTL 周期/PPA 是本项目实现的结果，不是该专利或 NVIDIA GPU 的实测数据。
5. 因此论文应突出 **现有 SIMT 操作数/写回约束下的具体机制及其资源取舍**，同时诚实限定验证范围。还需要作者进一步判断这些机制区别是否足够支撑目标会议的研究贡献。

## 完整目录盘点

下面只记录文件名、实际页数和本次阅读深度。较长预印本不能直接用作六页版式模板。

| 文件 | 页数 | 本次范围 |
| --- | ---: | --- |
| 01_2026_G-Power_ Architecture-level GPU Power Modeling with Aggregated Knowledge Foundations from Known GPUs.pdf | 7 | 版式/文件盘点 |
| 02_2026_Beyond Exact_ Tight WCET Analysis of GPU Kernels with Branch Divergence.pdf | 8 | 版式/文件盘点 |
| 03_2026_An Efficient Heterogeneous Co-Design for Fine-Tuning on a Single GPU.pdf | 7 | 版式/文件盘点 |
| 04_2026_ZK-Flex_ A Flexible and Scalable Framework for Accelerating Zero-Knowledge Proofs.pdf | 7 | 重点阅读 |
| 05_2025_GEM_ GPU-Accelerated Emulator-Inspired RTL Simulation.pdf | 7 | 重点阅读 |
| 06_2025_ABC-FHE_ A Resource-Efficient Accelerator Enabling Bootstrappable Parameters for Client-Side Fully Homomorphic Encryption.pdf | 7 | 版式/文件盘点 |
| 07_2025_SeDA_ Secure and Efficient DNN Accelerators with Hardware_Software Synergy.pdf | 7 | 版式/文件盘点 |
| 08_2025_ZK-Hammer_ Leaking Secrets from Zero-Knowledge Proofs via Rowhammer.pdf | 7 | 版式/文件盘点 |
| 09_2025_AmpereBleed_ Exploiting On-chip Current Sensors for Circuit-Free Attacks on ARM-FPGA SoCs.pdf | 7 | 版式/文件盘点 |
| 11_2023_CHAM_ A Customized Homomorphic Encryption Accelerator for Fast Matrix-Vector Product.pdf | 6 | 重点阅读 |
| 12_2023_NTT-PIM_ Row-Centric Architecture and Mapping for Efficient Number-Theoretic Transform on PIM.pdf | 6 | 重点阅读 |
| 13_2023_BP-NTT_ Fast and Compact in-SRAM Number Theoretic Transform with Bit-Parallel Modular Multiplication.pdf | 6 | 重点阅读 |
| 14_2023_Towards A Formally Verified Fully Homomorphic Encryption Compute Engine.pdf | 6 | 版式/文件盘点 |
| 15_2023_Primer_ Fast Private Transformer Inference on Encrypted Data.pdf | 6 | 版式/文件盘点 |
| 16_2022_MATCHA_ A Fast and Energy-Efficient Accelerator for Fully Homomorphic Encryption over the Torus.pdf | 6 | 重点阅读 |
| 18_2021_Optimized Polynomial Multiplier Architectures for Post-Quantum KEM Saber.pdf | 13 | 版式/文件盘点 |
| 19_2021_PSC-TG_ RTL Power Side-Channel Leakage Assessment with Test Pattern Generation.pdf | 6 | 版式/文件盘点 |
| 20_2021_SEALing Neural Network Models in Encrypted Deep Learning Accelerators.pdf | 14 | 版式/文件盘点 |
| 21_2020_CryptoPIM_ In-memory Acceleration for Lattice-based Cryptographic Hardware.pdf | 16 | 重点阅读 |
| 22_2020_Prive-HD_ Privacy-Preserved Hyperdimensional Computing.pdf | 8 | 版式/文件盘点 |
| 23_2022_PathFinder_ Side Channel Protection through Automatic Leaky Paths Identification and Obfuscation.pdf | 20 | 版式/文件盘点 |
| 24_2022_Apple vs. EMA_ Electromagnetic Side Channel Attacks on Apple CoreCrypto.pdf | 6 | 版式/文件盘点 |
| 25_2022_PARIS and ELSA_ An Elastic Scheduling Algorithm for Reconfigurable Multi-GPU Inference Servers.pdf | 13 | 版式/文件盘点 |
| 26_2022_GTuner_ Tuning DNN Computations on GPU via Graph Attention Network.pdf | 6 | 版式/文件盘点 |
| 27_2022_GATSPI_ GPU Accelerated Gate-Level Simulation for Power Improvement.pdf | 7 | 重点阅读 |
| 28_2022_Xplace_ An Extremely Fast and Extensible Global Placement Framework.pdf | 6 | 版式/文件盘点 |
| 29_2025_FastPath_ A Hybrid Approach for Efficient Hardware Security Verification.pdf | 7 | 版式/文件盘点 |
| 30_2024_MSMAC_ Accelerating Multi-Scalar Multiplication for Zero-Knowledge Proof.pdf | 6 | 版式/文件盘点 |
| 31_2025_SynGPU_ Synergizing CUDA and Bit-Serial Tensor Cores for Vision Transformer Acceleration on GPU.pdf | 7 | 重点阅读 |
| 32_2025_PacQ_ A SIMT Microarchitecture for Efficient Dataflow in Hyper-asymmetric GEMMs.pdf | 7 | 重点阅读 |
| 33_2023_GenFuzz_ GPU-accelerated Hardware Fuzzing using Genetic Algorithm with Multiple Inputs.pdf | 6 | 版式/文件盘点 |
| 34_2026_iHyperG_ Incremental Hypergraph Partitioning on GPU.pdf | 7 | 版式/文件盘点 |
| 35_2026_A Differentiable Approach to Task Graph Partitioning_ A Case Study in RTL Simulation.pdf | 7 | 版式/文件盘点 |
