# 在 SIMT GPGPU 上加速 PQC：批处理并行 vs 消息内并行的证据、量化与 Vortex 映射

**面向对象**：准备 HPCA/ISCA/MICRO/TCHES 级论文的体系结构研究者
**性质**：工作用研究参考（dense、tabular、对不确定性诚实），不是论文草稿
**基准平台**：Vortex `feature/pqc`，`VX_config.toml` 默认配置 —— 1 cluster / 1 core / `VX_CFG_NUM_WARPS = 4` / `VX_CFG_NUM_THREADS = 4`（`VX_CFG_SIMD_WIDTH = "expr: $VX_CFG_NUM_THREADS"`，`VX_config.toml:56`）/ `VX_CFG_ICACHE_SIZE = VX_CFG_DCACHE_SIZE = 16384` / `VX_CFG_LMEM_LOG_SIZE = 14` / `VX_CFG_L2_ENABLE = VX_CFG_L3_ENABLE = false`；SimX 后端；目标板 AMD Alveo V80

**验证状态标记（全文通用）**：
**[全文]** 已取得并阅读正文 · **[摘要]** 仅核实出版方摘要/元数据 · **[二手]** 经另一篇已验证文献转述 · **[标准原文]** 逐条对照 NIST FIPS 原文 · **[源码]** 逐行对照本仓库或上游源码 · **[本地实测]** 本仓库既有测量 · **[本轮新测]** 本轮新增的实现与测量 · **[未核实]** 明确未能核实
**规则**：本文不把任何未核实的数字当作事实陈述。凡与草稿冲突处，以带 **[本轮新测]** / **[源码]** / **[标准原文]** 的结论为准，并在原地用「⚠️ 修订」标出改了什么、为什么。

---

## §0 三十秒版：头条实测结果 —— 跨消息批几乎免费；消息内并行值不值，取决于 cache 层次

草稿的核心论断（"Vortex 上应当以消息内并行为主、跨消息批为辅"）**被实测推翻了一半**。真实的机器给出的排序是反过来的 —— 而且在补上二维扫描（§0.42）之后，"消息内并行没用"这一条被证明只在 **L2 关闭**时成立：同一格开了 L2 之后差 3.1×。先读 §0.42，§0.1–§0.4 都是 L2 关闭下的一维切片。

### 0.1 完整 ML-KEM-768 往返，同一二进制，只改 launch 形状 **[本轮新测]**

（`tests/pqc/mlkem_width/`，`MLK_CONFIG_CUSTOM_ALLOC_FREE` per-hart arena 构建，device cycles 取自 `PERF`）

| 配置 | 活跃 hart | cycles | 完成消息数 | **吞吐** | IPC |
|---|---|---|---|---|---|
| 1 warp × 1 lane | 1 | 36,488,730 | 1 | 1.000× | 0.104 |
| 1 warp × 2 lane | 2 | 28,555,478 | 1 | 1.278× | 0.101 |
| 1 warp × 4 lane | 4 | 25,412,002 | 1 | **1.436×** | 0.096 |
| **2 warp × 1 lane** | 2 | **36,491,581** | **2** | **2.000×** | 0.208 |
| **2 warp × 2 lane** | 4 | 28,643,489 | 2 | **2.548×** | 0.202 |

> **两条独立的 ML-KEM-768 往返花 36,491,581 cycles，一条花 36,488,730 —— 多做一倍的工作只多花 0.008% 的时间**（指令数 7,576,108 = 2.0000 × 3,788,060，arena 失败 0 次）。不改库、不改栈、不做 xN。

### 0.2 干净的 xN 核（`genmat_xn`，lane 私有 XOF 流，校验和逐位验证）**[本轮新测]**

（`VX_CFG_NUM_THREADS=16`、`VX_CFG_NUM_WARPS=4`；9 多项式与 7 多项式的校验和在 W ∈ {1,2,3,4,5,6,8,9,12,16} 与所有块数下均为 `817bf6c4` / `104bd412`）

| 配置 | cycles | 消息数 | **吞吐** | IPC |
|---|---|---|---|---|
| 1 warp × 1 lane | 7,093,897 | 1 | 1.000× | 0.102 |
| 1 warp × 9 lane | 2,467,001 | 1 | 2.876× | **0.057** |
| 1 warp × 16 lane | 2,576,747 | 1 | 2.753×（**倒退**） | 0.055 |
| 2 warp × 1 lane | 7,109,786 | 2 | 1.996× | 0.199 |
| **4 warp × 1 lane** | 7,145,516 | 4 | **3.971×** | 0.396 |
| **4 warp × 2 lane** | 5,615,001 | 4 | **5.054×** ← 最佳实测点 | 0.298 |
| 4 warp × 4 lane | 5,790,567 | 4 | 4.900×（**倒退**） | 0.182 |
| 8 CTA（2 波）× 1 lane | 14,236,926 | 8 | 3.986×（4 warp 槽饱和） | 0.397 |

### 0.3 结论的一句话形式

> **跨消息（warp 轴）批处理在 Vortex 上近乎免费：线性到 4 warp（3.979×，效率 99.5%），拐点在 8 warp。消息内（lane 轴）并行在 L2 关闭时 ~1.5× 就饱和并转负 —— 但这是容量结论不是并行度结论：开 L2 后同一个轴单调上升，最佳点是 M=8 × L=4 的满占用 14.679×（§0.42）。**

机制（实测，非推测）：**cycles/slot 从 W=1 到 W=9 上升 2.26×**（186,553 → 202,692 → 421,309），IPC 从 0.102 掉到 0.057。软件 Keccak-f1600 在 32-bit lane 上每轮把 25 个 64-bit 状态字溢出到栈；W 宽的批就是 W 条发散的 cache-line 流挤过一个 LSU，而单 warp 没有任何东西可以用来隐藏这个延迟。加 warp 恰好提供了这个隐藏。

⚠️ **修订**：草稿 §3.2/§4.2 主张"4 路 Keccak 批不增加工作集，因此 (b)/(d) 优于 (a)"。**该论证的前提在实测中不成立**——多-lane 端到端臂用的是冗余 SPMD（每 lane 跑整条 KEM，只有 Keccak 批不同），工作集实际乘 W，这正是 W=8 崩掉的原因；而"4 条独立消息把 19 KB 工作集乘 4 会撞 D$"的预言也不成立，2 warp × 1 lane 实测 0.008% 开销。真正的约束是**每 hart 8 KB 栈槽**（§4.2），不是 16 KB D$。

### 0.4 warp 轴扫到 16：拐点在 8，天花板不是发射宽度 **[本轮新测]**

§0.2 的 warp 轴只到 4，因为那是 `VX_CFG_NUM_WARPS` 的默认值。用 `ci/blackbox.sh --warps=N` 扫开（derived 的 `ISSUE_WIDTH`/`NUM_OPCS`/`LSU_PENDING_SIZE` 都是对 `NUM_WARPS` 的预处理器表达式，`build32/sw/VX_config.h:318,326,451`，所以 `-D` 覆盖是自洽的，不是 AGENTS.md 警告的那种配置错位）。

`genmat_xn`，`NUM_THREADS=4`，每条消息 1 lane，B = NW（填满 warp 槽）：

| NW | B | cycles | instrs | IPC | **吞吐** | **效率** |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 7,004,540 | 720,652 | 0.103 | 1.000× | 100% |
| 2 | 2 | 7,016,115 | 1,417,319 | 0.202 | 1.997× | 99.8% |
| 4 | 4 | 7,041,866 | 2,829,119 | 0.402 | 3.979× | 99.5% |
| **8** | **8** | 8,705,553 | 5,650,983 | 0.649 | **6.437×** | **80.5%** |
| 16 | 16 | 15,611,676 | 11,241,084 | 0.720 | 7.179× | 44.9% |

**空 warp 槽的代价精确为零**（B=1 对照，指令数与校验和恒为 720,652 / `817bf6c4`）：

| NW | cycles | IPC |
|---:|---:|---:|
| 1 / 2 / 4 | 7,004,540（**三行逐位相同**） | 0.103 |
| 8 | 7,124,300（+1.7%） | 0.101 |
| 16 | 7,257,180（+3.6%） | 0.099 |

**天花板的机制**：IPC 停在 0.720，而 `ISSUE_WIDTH = up(NUM_WARPS/16) = 1` 意味着峰值 IPC = 1.0 —— **卡住的不是发射宽度**。每条消息的工作量恒定（XOF 块数 27/27/26.5/27.4/27.3，指令数 720k/709k/707k/706k/703k），所以只能是访存。开 L2 验证：

| 配置 | cycles | IPC | 吞吐 | Δ |
|---|---:|---:|---:|---:|
| NW=8, B=8, L2 off | 8,705,553 | 0.649 | 6.437× | — |
| NW=8, B=8, **L2 on** | 7,976,772 | 0.708 | **7.025×** | **+9.1%** |
| NW=16, B=16, L2 off | 15,611,676 | 0.720 | 7.179× | — |
| NW=16, B=16, **L2 on** | 13,834,940 | 0.813 | **8.101×** | **+12.8%** |

> **L2 只回收了 9–13%，不是缺失的那 40%。** 所以天花板是访存系统相关的，但 L2 容量单独解释不了；剩下的最可能是未完成 load 池（`VX_CFG_LSU_PENDING_SIZE = min(32, max(8, 2·NUM_WARPS))`，NW=16 时正好撞 32 上限）与平台内存带宽（`VX_CFG_PLATFORM_MEMORY_NUM_BANKS = 2`）。**这一条尚未分离，属于开放项。** **[未核实]**

**分离"warp 槽数"与"在飞消息数"**：NW=8 跑 B=16（两波）= 17,148,854 cycles = 6.535×，对比 NW=16 跑同样 16 条消息的 7.179×。**把槽位从 8 加到 16 只买到 +9.8%** —— 8 个槽已经拿到了绝大部分延迟隐藏。

**结论**：**拐点在 NW=8。** 4→8 换 +62% 吞吐（2× 上下文）；8→16 只再加 +11.5%（又 2× 上下文），效率从 80.5% 掉到 44.9%。开 L2 不移动拐点。

⚠️ **本表的适用范围**：`genmat_xn` 每条消息足迹仅约 1.2 KB，而完整 KEM 每条消息要 19.2 KB arena。**完整 KEM 的 warp 扩展性一定低于 6.44×**，这个数应作为**上界**理解（它覆盖的恰好是占 72% 的 XOF/Keccak 阶段）。完整 KEM 的 warp 扫描是第 2 步必须补的第一个数。

⚠️ **`genmat_xn` 的仪器缺陷（本轮发现）**：NW=8 / B=16 的两波运行中，kernel 回报的 `genmat`/`noise`/`blocks`/`stack` 全部是垃圾值，校验和变成 `00000001/00010001`（正确值 `817bf6c4/104bd412`），**而 `main.cpp` 仍然打印 `PASSED!`**——它从不比较校验和，只检查 API 返回码。首要嫌疑是 `kernel.cpp:47` 的 `static uint32_t gx_blocks;` 是 file-scope 变量，被同一 hart 上先后两波的 CTA 竞争。**这正是本仓库已经立为失败条件的"仪器静默失效"模式。修好之前，`genmat_xn` 的任何 B > NW 的数据点都不可用。** **[本轮新测]**

---

## §0.42 二维扫描：两个轴的符号取决于 cache 层次 **[本轮新测]**

§0.4 的 warp 扫描是一维的，而两个轴会互相干扰。在修好 `genmat_xn`（缓冲区按 launch 分配、XOF 计数改为每 hart 一份、校验和从「打印」升级为「断言」）之后，在同一个二进制上扫 M（独立消息数）× L（每消息 lane 数），每格校验和逐格匹配：

吞吐（归一到 M=1,L=1,L2 off = 7,000,136 cycles）：

| M | L | harts | **L2 关** | **L2 开** | Δ |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 1.000× | 1.000× | +0.0% |
| 1 | 2 | 2 | 1.626× | 1.626× | +0.0% |
| 1 | 4 | 4 | 2.507× | 2.506× | −0.0% |
| 2 | 1 | 2 | 1.995× | — | |
| 2 | 4 | 8 | 4.129× | — | |
| 4 | 1 | 4 | 3.973× | — | |
| 4 | 2 | 8 | 5.495× | 6.054× | +10.2% |
| 4 | 4 | 16 | 5.756× | 8.686× | **+50.9%** |
| 8 | 1 | 8 | 6.668× | 7.305× | +9.6% |
| 8 | 2 | 16 | 7.001× | 10.661× | **+52.3%** |
| **8** | **4** | **32** | 4.742×（倒退） | **14.679×** | **+209.6%** |
| 16 | 1 | 16 | 7.435× | 8.472× | +13.9% |
| 16 | 2 | 32 | 5.197×（倒退） | 13.423× | **+158.3%** |

### 0.42.1 L2 关闭时：M 轴支配，L 轴的交互项为负

同 hart 数横向比：4 hart 时 M=4,L=1（3.973×）比 M=1,L=4（2.507×）快 **1.58×**；8 hart 时 M=8,L=1（6.668×）比 M=2,L=4（4.129×）快 **1.61×**。而 L 轴的边际收益随 M 增大而消失并转负：`L=1→4` 在 M=1 时 ×2.51，M=2 时 ×2.07，M=4 时 ×1.45，**M=8 时 ×0.71**。

**这正是一维扫描会骗人的地方**：在 M=1 下扫 L 轴得出「L=4 值 2.5×」，而在真实工作点（warp 填满）上它是负的。IPC 从 0.649 掉到 0.161。

### 0.42.2 L2 开启时：符号翻转，满占用变成最优

同一个 L 轴变成单调的，**最佳点从 M=8,L=2 的 7.00× 移到 M=8,L=4 的 14.68×** —— 同一格相差 **3.1×**，只因为一个 cache 层次的开关。而且同 hart 数下**混合配置反过来赢纯 M 轴**：16 hart 时 M=8,L=2（10.661×）比 M=16,L=1（8.472×）快 **26%**；32 hart 时 M=8,L=4（14.679×）比 M=16,L=2（13.423×）快 **9%**。

L2 对单 lane **完全无效**（M=1 三行 Δ 均在 0.04% 内）。它买的正是多 hart 时的容量压力：M=8,L=4 那格 IPC 从 0.161 回到 0.497。

### 0.42.3 方法学结论（本报告最可迁移的一条）

> **「lane 并行在这台机器上没用」是一个 cache 容量的结论，不是一个并行度的结论。** 换掉存储层次，同一个轴的符号就翻转。任何关于 warp 该多宽的陈述，都必须连同它所依据的 cache 配置一起给出；`VX_CFG_L2_ENABLE` 因此是设计变量而不是背景设置，要进 PPA 表。

推论：**`1 core × 8 warp × 4 lane` 这个配置是对的，但它的正确性依赖 L2 开启。** 在 L2 关闭的数据上选它，理由是错的（结论碰巧对）。

**适用范围**：`genmat_xn` 只覆盖 100% 可批的独立流阶段，不含串行 G/H/J 链（占 ML-KEM Keccak 置换的 30.8%）、NTT（14%）与打包（13%），所以上表是**完整往返的上界，不能当基线**。交叉验证：把 L=4 的 2.507× 与「可批 Keccak 占总时间 50%」复合，预测端到端 1.43×，`mlkem_width` 在完整往返上实测 **1.436×**（差 0.4%）。

原始数据：`pqc/results/sweep2d_genmat_xn.csv`。

---

## §0.45 基线已被取代（栈修复之后重测）**[本轮新测]**

§4.2 指出的每 hart 8 KB 栈槽溢出**已经修复并实测确认**（`tests/pqc/pqc_stack.h` 涂色水位探针 + `MLK_/MLD_CONFIG_CUSTOM_ALLOC_FREE` 的 per-hart bump arena）。修复前后：

| | 修前 | 修后 |
|---|---:|---:|
| ML-KEM-768 栈峰值 | **8,144 / 8,128 可涂区（溢出，测试仍 PASSED）** | **3,944**（余量 52%） |
| ML-KEM arena 峰值 | 全在栈上 | **19,232** / 24,576，`fail=0` |
| ML-KEM 总周期 | 32,742,056 | **32,777,984**（+0.11%） |
| ML-DSA-65 栈峰值 | 同样溢出 | **3,096** |
| ML-DSA arena 峰值 | 21,568（全局单份，多 lane 会竞争） | **21,568**（每 hart 一份，数字未漂移） |
| ML-DSA 总周期 | 189,821,707 | **190,204,131**（+0.20%） |
| `mlkem_indcpa_enc` 栈帧（`-fstack-usage`） | **13,184 B** | **384 B** |
| 最大栈帧 | 13,184（`indcpa_enc`） | 2,944（`poly_rej_uniform_x4`） |
| `.bss` | 0 | 393,408 B（16 hart × 24 KB） |

> **本文其余章节里出现的 32,742,056 与 189,821,707 是修复前的数字，不能再用作加速比的分母。** 它们来自两个跑不了多 lane 的程序：单 lane 之所以「能跑」，只是因为 hart 1..N−1 空闲、它们的 slab 吸收了越界写。新的分母是 **32,777,984 / 190,204,131**，同配置（1 cluster / 1 core / 4 warp / 4 thread、L2-L3 off、simx）。

**探针是可证伪的**：只加探针不加 arena 的中间版本打印 `*** stack peak 8144 B filled its whole 8144 B paintable slab -- it overflowed into the next hart` 并 `FAILED!`，**而四个 KAT 全部通过** —— 这正是它存在的理由。

**副产品**：arena 峰值 **19,232 B（ML-KEM）/ 21,568 B（ML-DSA）就是每 lane 的片上 scratch 需求**，此前藏在栈里不可见。它也给 §0.4 的保留意见一个数：8 warp 满载时完整 KEM 的工作集是 8 × 24 KB = 192 KB，对 16 KB D$。

**波及范围**：`vortex_mlkem_config.h` 被 `mlkem_profile` / `mlkem_width` / `genmat_xn` 共享，所以它们都继承了 arena，凡跑完整 KEM 的都必须在阶段之间 `mlk_arena_reset()`。八个 PQC 测试全部重跑通过，原语计数（156/27、782/6、15/9、54/49）与修复前逐个相同。

---

## §0.5 本稿相对草稿的修订清单

| # | 草稿说法 | 本稿结论 | 依据 |
|---|---|---|---|
| 1 | lane 宽度投影收益 W=4/8/9 = 1.60× / 1.79× / 1.97× | **实测 1.44–1.56× / 1.03×（真 x4 API）/ 1.71×（xN 合成）**；warp 轴 2 消息 +0.008% cycles、4 warp 3.97× | [本轮新测] §0、§3.3 |
| 2 | "156 次 permutation"作为主数字 | **156 是 p = 6.27% 的 seed；众数 144（p = 92.75%）、E = 144.81**。Keccak 占比是 **70.7–72.3% 的区间**，Amdahl **3.4–3.6×** | [本轮新测] §3.2 |
| 3 | 串行项 = 48；多出的 ~3 组归因于"max-of-4 尾部，约 11%" | **两个模型两个数**：x4-API 口径 48（= 39 串行 sponge + 9 第 9 个矩阵多项式），真 xN 口径 **39**。25 次"未归属"= 9（FIPS 203 §7.3 Hash check）+ 4（PRF 第 4 路 padding）+ 12（3 轮补块，每轮**整组 4 次**）。补块的**期望**代价是 x4 组的 **0.82%**，不是 11% | [标准原文]+[源码]+[本轮新测] §3.2 |
| 4 | ML-DSA 只有 3.1% 可批，是 `MLD_CONFIG_REDUCE_RAM` 的结构性后果 | **是 `polyvec_lazy.h:427` 的单槽 `mld_poly cur;`**，一个数据结构字段。开 4 宽窗口 +6.6 KB → 55.1%（padded 77.3%）；**关掉 REDUCE_RAM 在 Vortex 上实测 83.2% 批 + 快 25.2%**，代价 +65 KB。**R7 的第二条腿断了** | [源码]+[本轮新测] §3.5 |
| 5 | 单-lane 基线"可辩护，作为 ISA 的分母" | 仍成立，**但它当前越界**：峰值栈 18,040 B > 8,192 B/hart 槽，只因 hart 1..15 空闲才看不出来；点亮第二 lane 立刻 `Error: misaligned memory access` | [本轮新测] §4.2 |
| 6 | 每次 permutation 151,999 cycles → Keccak 72.4% / Amdahl 3.62× | 仓库里的实际值是 **151,777**（`tests/pqc/mlkem_profile/main.cpp:43`），NTT 160,220 / invNTT 249,571 → **72.31% / 3.61×** | [源码] §3.1 |
| 7 | "`VX_CFG_SIMD_WIDTH = 4` 恰好是最优宽度，因为 x4 API 封顶在 4" | **该论证是 API 的人造物，必须弃用**。结论换成实测饱和：lane 宽度过 4 的边际端到端回报 ≤ 10%，用真 x4 API 在 W=8 转负 | [本轮新测] §3.3 |
| 8 | （未提及） | **`MLK_PROF_KECCAK_X1` 是总 permutation 数（已含每个 x4 组的 4 次内层），`MLK_PROF_KECCAK_X4` 是组数。** mldsa 同构。极易误读 | [源码] §3.6 |

---

## §1 并行度分类学

先把术语钉死。设一条消息（一次 KEM 往返或一次签名）为一个 *operation*；SIMT 机器的层级是 core > warp > lane。

| 类别 | 每 lane 持有的工作单元 | 同步点 | 延迟/吞吐权衡 | 文献代表 |
|---|---|---|---|---|
| **(a) inter-operation / batch**<br>1 thread = 1 条完整消息 | 整个 KEM/签名的全部状态 | 无。零跨 lane 通信、零 divergence（除拒绝循环） | 吞吐最高；单条延迟 = 该 thread 的全串行运行时间 | Gupta et al. TPDS'21；HI-Kyber；Classic McEliece；GOLF-coarse；Römer et al. |
| **(b) intra-operation across lanes**<br>N thread 协作一条消息 | 一个蝶形 / 一个 Keccak lane / 一个系数 | 每 NTT 层一次 barrier；Keccak 每轮一次 θ 归约 + ρ/π 全交换 | 延迟最低；吞吐 = 1/k（除非延迟降 ≥ k 倍） | Ono/Bian/Sato ISCAS'21；Cayrel et al. ISA'11（25 线程/状态） |
| **(c) hybrid：1 warp 或 1 block 一条消息** | 一条消息的一个切片，阶段不同宽度不同 | block 内 `__syncthreads`；warp 内退化为 `__syncwarp` | 二者折中；**2022–2026 年的事实标准** | cuPQC（`BlockDim<128>()`）；cuDilithium；ConvKyber；Lee et al. Falcon/Mitaka |
| **(d) intra-lane SIMD 批（keccak_x4 式）**<br>1 lane 的向量寄存器里放 k 个独立状态 | k 个**互不相关**的同类状态 | **零**——θ/χ 永不跨 SIMD lane 边界 | 面积线性、吞吐线性、**延迟恒定不变** | CIRCL `StateX4`；mlkem-native `shake128x4`；Li/Mentens/Picek RVV |

**(d) 在 SIMT 上恰好退化为跨 lane 的 (a)-in-miniature**：k 个交错状态直接摊到 k 个 lane，指令流一致、零 shuffle、零 divergence、**不改变算法**。这是草稿推荐路线的基石，而 §0 说明它在 Vortex 上的**收益上限只有 ~1.5×**，因为瓶颈不在指令而在单 warp 的访存延迟裸露。

⚠️ **修订（重要，且这是本报告最可迁移的概念）**：真正的分界线不是 (a)/(b)/(c)/(d)，而是 **XOF 的类型**：

| XOF 类型 | 实例 | 独立流数 | 单消息内可批性 | 需要 lane 间通信吗 |
|---|---|---|---|---|
| **独立流 XOF** | ML-KEM `gen_matrix`（k² = 9 流）、噪声 PRF（6/7 流）；ML-DSA ExpandA（k·ℓ = 30 流）、ExpandS（k+ℓ = 11 流）、ExpandMask（ℓ = 5 流） | 多 | **天然可批，宽度由参数集决定** | **零** |
| **串行 sponge** | ML-KEM 的 G/H/J 链、FIPS 203 §7.3 Hash check；ML-DSA 的 H(pk)、μ、ρ''、c̃、SampleInBall | 1，严格链式 | **单消息内不可批** | 只能靠 25-lane-per-state 或跨消息批 |

ML-DSA-65 单条消息里，前者占 slot 的 **93.7%**（1316.70 / 1399.27，期望口径），后者 6.3% **[本轮新测]**。ML-KEM-768 同构：39 次串行 sponge / 143 次总 permutation ≈ 27%（x1 串行构建口径），其余是独立流。**这个划分对两个方案同时成立，是比"ML-KEM 好批、ML-DSA 不好批"精确得多的说法。**

关于 (b) 的吞吐代价，GOLF 给出了唯一一个显式的代数式 [Dai et al., ePrint 2025/749] **[全文，前序核]**：`Generated Signatures = Gridsize × Blocksize / Threads Used`，因此 *"throughput will decrease unless the latency decreases to k⁻¹× or lower"*。RTX 4090 同 launch 配置（128×512）A/B：coarse 361,726 sig/s @181.18 ms vs fine(k=2) 177,027 sig/s @185.10 ms —— **延迟只降 2.1%，吞吐正好腰斩**。

---

## §2 文献实际怎么做

**范围声明**：以下"收敛"结论**限定于格基方案（ML-KEM / ML-DSA / Falcon / Frodo / NewHope / Saber）**。哈希基签名（SLH-DSA / SPHINCS+）与非 NVIDIA GPU 平台**未被本轮检索覆盖**，见 §8。

### 2.1 ML-KEM / Kyber

| 年份 | 工作 | 映射 | 关键数字 | 标记 |
|---|---|---|---|---|
| 2020 | Gupta et al., IEEE TPDS 32(3) | **纯 coarse**：1 thread = 1 个完整 Kyber-1024（源码已读：`N_TESTS 16384`, `BLOCK_SIZE 32`，`ntt_p` 单线程走完 7 层，`poly` 转置为 `coeffs[256].threads[16384]`） | 473K KX/s，51.05× ref-C（QUADRO GV100）；vs AVX2 仅 14.6× | [摘要 + 源码全文，前序核] |
| 2021 | Ono/Bian/Sato, ISCAS 2021 | **纯 fine**："each block processes a single key exchange"；SHA-3 = 1 warp（warp shuffle）；NTT = *"n/2 threads … to a single polynomial"* = 128 线程 | 最优并发数塌缩到 **24–376** 条消息 | [全文，前序核] |
| 2022 | Wan et al., ESORICS 2022 | hybrid，coarse 主导："each thread holds one instance"，NTT 装进 Tensor Core warp box，**强制 batch ≥ 16** | Kyber-1024 R3080 KX 819.7 kops/s | [全文，前序核] |
| 2023 | HI-Kyber, IEEE TPDS 35(6) | **回到 coarse**，寄存器驱动 DFS-NTT；原文明说 *"it is impossible to load N NTT coefficients for the scarce resource registers"* | Titan V：KX **1,358** kops/s（1,664 是 V100 行） | [全文，前序核] |
| 2024 | ConvKyber, TCHES 2024(2) | **最干净的 hybrid**："each thread handles one Kyber instance" + *"the NTT module is executed collaboratively by 32 threads (1 warp)"* | KX 1,336 kops/s = 2.82× Gupta；polyvec_ntt 单实例 107.81 → 16.65 → **8.61 ns** | [全文，前序核] |
| 2024- | NVIDIA cuPQC | **工业定论（本轮核到的部分）**：`BlockDim<128>()`，`keygen_kernel<<<batch, BlockDim>>>`，*"each block computes a single public_key and secret_key"* | H100 ML-KEM-768 吞吐与 batch = 1,000,000 系前序核 | [全文，2026-09-05 访问] |

⚠️ **修订**：草稿写"cuPQC 的 `Block()` 是唯一支持模式、BlockDim 只接受 32/64/128/256"。本轮**未能在文档页确认这两条**，也未确认 ML-DSA-65 的 BlockDim 与吞吐。**这三条降级为 [未核实]，正式引用前须重新取得。** 已确认的只有 `BlockDim<128>()` 与 *"Tunability, options to adjust how many threads perform the operations (BlockDim)"*。

**决定性的 Amdahl 数据点**：ConvKyber 把 NTT 做快 12.5×（107.81 → 8.61 ns），端到端只拿到 2.82×，因为 Keccak/XOF/PRF 仍是 one-thread-per-instance——他们自陈 *"the computational load for KeyGen and Dec is primarily hash-based, which are challenging to accelerate on parallel computing platforms"*；Wan et al. 更直白：*"we have not optimized the hash algorithm yet"* **[全文，前序核]**。

**文献分歧比看上去小**：GOLF（coarse 2.04× fine，FALCON）与 Römer et al.（batch 5.6× single，FrodoKEM；A100 上 single mode 比单个 CPU 核还慢 ×0.57）指向 coarse；DPCrypto 的 block-per-KEM FrodoKEM 在同一块 V100 上比 Gupta 的 thread-per-KEM 快 4.37×，ConvKyber 的 warp NTT 比 Gupta 单线程 NTT 快 12.5×，指向 fine。矛盾在于混淆了两个问题：**"某个 primitive 是否该协作"（通常是，因为 256 系数溢出寄存器）** vs **"整条 operation 的调度是否该协作"（通常否，因为 GOLF 的 k⁻¹ 恒等式）**。
⚠️ **修订**：草稿用 Antao et al. 的 RNS-ECC（"3,138 vs 1,413 op/s / 30.3 vs 305.0 ms，机器太窄时 fine 两轴全赢"）作为"这正是 Vortex 的处境"的类比。**该文正文从未取得（[摘要 + 二手]）**，且 §0 的实测已经直接回答了这个问题（在 Vortex 上 coarse 赢）。**该类比在本稿中降级为文献线索，不承担任何 Vortex 结论。**

### 2.2 ML-DSA / Dilithium

映射高度一致：**一个 block 一条签名**。cuDilithium [Shen et al., IEEE TPDS 35(11)] **[全文，前序核]**："one block computes tasks in one instance"；cuML-DSA "dedicates a block of 128 threads to each task"，明确拒绝 32 线程因为 *"33.3% theoretical occupancy achieved with just 32 threads"*。

三个对我们最有用的细节：

1. **Keccak 是 25 线程 / 状态 + warp shuffle**：*"the 24 rounds of permutation are executed by 25 threads, and each thread stores 64-bit of the state in registers … warp-shuffle is employed in each round"*。但 cuDilithium 保留 SWarp 变体的理由恰恰是 *"reduces waste in hash functions where **parallelism is limited to 25**"*——25 塞进 32-lane warp 浪费 22%，塞进 128-thread block 浪费 80%。**[全文，前序核]**
2. **Fiat-Shamir 拒绝循环制造 batch 内负载不均**："average number of repeat rounds is around 4, some worst cases require dozens of rounds, leaving many blocks idle"。缓解手段是投机（对同一 seed 试不同 κ，最小合法 κ 获胜），**没有任何论文用 stream compaction 或 task sorting**。**[全文，前序核]**
3. **Profile 与我们同形**：草稿引用的"3090 Ti 上 10,000 个 Dilithium2 任务，ExpandA 4,805 µs，NTT 只有 20–23 µs"——⚠️ **该具体数字本轮未能复核（eprint HTTP 429 + 搜索配额耗尽），标记 [未核实]，正式引用前须重新取得正文并注明是 arXiv v2 还是 TPDS 版（两版数字相差约 33%）。** 定性结论（FIPS-202 压倒 NTT）由本仓库的独立测量支持（§3.1、§3.5）。

### 2.3 Keccak：25-lane 问题的直接证据

这是最需要小心的一条，因为流传的说法多半是错的。**已核事实**：

- Lee, Phan, Goi, Chen, Zhang, Xiong, *IEEE Access* 6:37991-38002 (2018) 是唯一做了 **1 vs 5 vs 25 线程/permutation 正面对比**的工作。结论：*"the parallel granularity of one thread produces the highest hash throughput at 28.51 Gb/s"*。**[全文，前序核]**
- **但机制不是 occupancy 也不是寄存器压力**——原文：*"1T-Keccak is hashing the entire Keccak within one thread … In contrast, 5T-Keccak and 25T-Keccak need to use **shared memory** for sharing intermediate state values … involved a lot of overhead"*。他们的 5T/25T 用的是 **shared memory**，不是 warp shuffle，所以这条结果**并不直接否定** Ono/Shen 的 shuffle 版 25 线程设计。
- **排序是批大小相关的**：*"1T-Keccak only outperform the other two implementations when the tree height reach certain level"*。**低并行度时 25 线程赢，高并行度时 1 线程赢。**
- Cayrel, Hoffmann & Schneider, ISA 2011 是 25-线程/状态的源头。⚠️ **正文未取得（[二手]，经上条转述）。"25 线程/状态、shared memory bank conflict 是其 open problem、0.0025–0.2533 GB/s" 在引用具体数字前必须取得原文。**
- **5 才是 Keccak 的自然内并行宽度，不是 25**。θ 的 C[x] 要跨一个 plane 的 5 个 lane 归约；Li/Mentens/Picek 的 `EleNum=5` 就是一个状态；Ye et al. 独立地把 SIMD 寄存器堆定成 5×64=320 bit（一个 plane），14 条指令/轮；Rawat & Schaumont 把 5 塞进 128-bit NEON 时被迫加 `chi2/chi3` "last lane" 指令——**5 与 2 的幂不整除，4-lane warp 会遇到同一个问题**。**[全文/幻灯片全文，前序核]**

超过 5 之后，**所有设计都用更多独立状态填宽度，没有一个继续细分**。Li/Mentens/Picek 是决定性的：EleNum 5/15/30 = 1/3/6 个状态，**延迟恒定**（LMUL=8 时都是 75 cycles/round），吞吐线性（0.846/2.537/5.073 bits/cycle），**面积也线性**（7,323/24,789/48,180 slices）。117.9× 性能对应 111.2× 面积，perf/area ≈ 1.06。**批处理 Keccak 是按比例买吞吐，零延迟收益。**

### 2.4 NTT

规范映射与其天花板 [Zhang & Franchetti, CGO 2025] **[全文，前序核]**：*"each CUDA thread processes one or more butterfly operations … This parallelism is limited to **min(n/2, 1024)**-way for NTTs of size n."* 对 n=256 就是 **128**。cuPQC 的 `BlockDim<128>` 绝非巧合。

FHE 侧那一整套 layer-fusion 文献解决的问题 **PQC 根本没有**：Ozcan & Savas 的约束是一个 kernel 最多融 `log2(2·bDim)` 层，Kyber 只有 7 层、Dilithium 8 层；Ozerk et al. 测得单 kernel→多 kernel 的分水岭在 n=2¹⁴。**N=256 整条变换装进一个 block、SMEM 常驻、零 global barrier。** 问题从"怎么融层"变成"变换太小了，剩下的并行度从哪来"。

VeloFHE 给了最锋利的度量（N=2¹⁰，A100）：batch=1 用满 1024 线程要 **12.0 µs**；batch=8192、每 op 128 线程时摊到 **0.049 µs** —— **245×，纯粹来自批**。**[全文，前序核]** 这与 §0 的 Vortex 实测同向。

---

## §3 单条消息内部到底有多少可并行性（定量）

以下全部来自**本仓库的直接测量与源码计数**，不是文献外推。

### 3.1 ML-KEM-768 基线剖面

单-lane 基线（`tests/pqc/mlkem/kernel.cpp:14-15` 的 `if (blockIdx.x != 0 || threadIdx.x != 0) return;`，grid 1×1）**[本地实测]**：

```
往返总计            32,742,056 cycles  (keypair 9,383,535 / encaps 10,567,151 / decaps 12,791,370)
```

归因用的每-调用成本取自 `tests/pqc/mlkem_profile/main.cpp:43-46` **[源码]**：
`keccak_f1600_x1 = 151,777` · `poly_ntt = 160,220` · `poly_invntt = 249,571`

| 项 | 计数 | cycles | 占比 |
|---|---|---|---|
| Keccak-f1600 | 156 | 23,677,212 | **72.31%** |
| NTT + invNTT | 15 fwd + 9 inv | 4,649,439 | **14.20%** |
| 残差（采样 / 打包 / 编解码 / 控制） | — | 4,415,405 | 13.49% |

⚠️ **修订**：草稿用 151,999 cycles/permutation 算出 72.4% / 3.62×。仓库里的实际常数是 **151,777**，正确的是 **72.31% / 3.61×**。差别不大，但跨表相加时会累积。

ML-DSA-65 **[本地实测]**：189,821,707 cycles（keypair 42,029,462 / sign 107,014,136 / verify 40,778,109，签名占 56.4%）；782 次 permutation × 151,777 = 118,689,614 = **62.53%**；103 次 NTT/invNTT = 28.80M = **15.2%**；残差 22.3%。

**两个必须写进论文的 Amdahl 上界**（用修正后的常数）：

| 加速对象 | ML-KEM-768 | ML-DSA-65 |
|---|---|---|
| 仅 NTT | 1/(1−0.1420) = **1.17×** | 1/(1−0.152) = **1.18×** |
| 仅 Keccak | 1/(1−0.7231) = **3.61×** | 1/(1−0.6253) = **2.67×** |
| 两者 | 1/(1−0.8651) = **7.41×** | 1/(1−0.777) = **4.49×** |

⚠️ **seed 区间修订（R12）**：上面的 72.31% 建立在 **156 次 permutation** 上，而 156 是一个 **p = 6.27%** 的 seed（§3.2）。众数是 144，对应 Keccak 占比 **~70.7%**、Amdahl **3.41×**；`tests/pqc/mlkem` 的基线构建（串行 PRF）是 152 次，对应 **71.79%**。**诚实的表述是「Keccak 占 ML-KEM-768 往返 70.7–72.3%，Amdahl 上界 3.41–3.61×」，并说明区间来自 (i) 152/156 的构建分歧、(ii) 144/156 的 seed 分布。**

**任何 ML-KEM 自定义 ISA 论文首先必须是一个 Keccak 故事；NTT 是二阶项。** 这与 OpenTitan 的独立测量同形（ML-KEM-768 基线 Hash 占 66–70%，ML-DSA-65 占 55–77%）**[全文，前序核]**。

### 3.2 ML-KEM Keccak：逐调用点归属（求和精确等于 156）

成本模型，读自 `pqc/third_party/mlkem-native/mlkem/src/fips202/fips202.c` **[源码]**：
`mlk_keccak_absorb_once(mlen, r)` → `⌊mlen/r⌋` 次 · `mlk_keccak_squeezeblocks(nblocks)` → `nblocks` 次 · `mlk_keccak_squeeze_once(outlen, r)` → `⌈outlen/r⌉` 次 · `mlk_keccakf1600x4_permute`（无 native hook）→ **4 × `mlk_keccakf1600_permute`**（`keccakf1600.c:179-190`）。

| # | 调用点 | rate | in→out | 每次 perm | keypair | encaps | decaps | **perms** | x4 组 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | XOF absorb，gen_matrix x4 | 168 | 34 B | ⌊34/168⌋ = **0** | 0 | 0 | 0 | **0** | 0 |
| 2 | XOF squeeze，gen_matrix x4，初始 3 块（2 批 × 3 次 gen_matrix） | 168 | 504 B | 3 组 = 12 | 24 | 24 | 24 | **72** | 18 |
| 3 | XOF squeeze，gen_matrix x4，**rejection 补块**，1 块 | 168 | 168 B | 1 组 = 4 | 4 | 4 | 4 | **12** | 3 |
| 4 | XOF absorb，gen_matrix x1（第 9 个多项式） | 168 | 34 B | **0** | 0 | 0 | 0 | **0** | 0 |
| 5 | XOF squeeze，gen_matrix x1，初始 3 块 | 168 | 504 B | 3 | 3 | 3 | 3 | **9** | 0 |
| 6 | XOF squeeze，gen_matrix x1，补块 | 168 | — | 1 | 0 | 0 | 0 | **0** | 0 |
| 7 | PRF η，**x4 批**（`shake256x4`，2 次/阶段） | 136 | 33→128 B | 0 + 1 组 = 4 | 8 | 8 | 8 | **24** | 6 |
| 8 | G = SHA3-512 | 72 | 33/64→64 B | 0 + ⌈64/72⌉ = 1 | 1 | 1 | 1 | **3** | 0 |
| 9 | H = SHA3-256(ek)，**规范内**（Alg 16 step 3 / Alg 17 step 1） | 136 | 1184→32 B | 8 + 1 = **9** | 9 | 9 | 0 | **18** | 0 |
| 10 | H = SHA3-256(ek)，**FIPS 203 §7.3 Hash check**（`mlk_kem_check_sk`，`kem.c:104`，由 `kem.c:423` 调用） | 136 | 1184→32 B | 8 + 1 = **9** | 0 | 0 | 9 | **9** | 0 |
| 11 | J = SHAKE256(z‖c)（Alg 18 step 7） | 136 | 1120→32 B | 8 + 1 = **9** | 0 | 0 | 9 | **9** | 0 |
| | **合计** | | | | **49** | **49** | **58** | **156** | **27** |

**[标准原文]** FIPS 203 逐条核对：§4.1 eq. 4.3–4.5（`H := SHA3-256`、`J := SHAKE256(s, 8·32)`、`G := SHA3-512`、`PRF_η(s,b) := SHAKE256(s‖b, 8·64·η)`）；Alg 13 step 1；Alg 16 step 3；Alg 17 step 1；Alg 18 steps 6–8；**§7.3 item 3 "(Hash check) Perform the computation `test ← H(dk[384k : 768k+32])`"** 并附 *"need not be performed … with every execution of ML-KEM.Decaps"*；Table 2 ML-KEM-768 参数 ek 1184 / dk 2400 / ct 1088。

由此：27 组 → 108 次批内 perm；48 次非批 perm；**75 个顺序 permutation 步**；156/75 = **2.0800×**；lane 利用率 156/(4×75) = **52.00%**。

⚠️ **修订 A —— 25 次"未归属"的 permutation 分解为 9 + 4 + 12，没有一次是草稿说的"max-of-4 尾部"**：

- **+9 = FIPS 203 §7.3 Hash check**，每次 decaps 都跑。**这是 58 次 decaps permutation 中的 9 次（15.5%），花在输入校验上，而标准明说它不必每次执行。** 论文里这是一个合法、可引用的设计点（"我们把它提出热循环"是可辩护的声明）。
- **+4 = x4 PRF 把 3 宽负载补到 4 lane**。k=3 时 `mlk_poly_getnoise_eta1_4x` 的第 4 路传 `NULL`，但批路径**仍然发满 4 宽**（`r3 != NULL` 的保护只存在于串行 `#else` 分支）。**mlkem-native 已经在浪费自己 16.7% 的批 PRF lane** —— 这是"宽度是对的、宿主负载填不满"的直接证据。
- **+12 = 3 轮补块，每轮是一个完整的 x4 组（4 次 perm，不是 1 次）**。`mlk_poly_rej_uniform_x4` 的补块循环是 `while (ctr[0]<N || … || ctr[3]<N)`，**为四个状态同时挤一块**。一个短多项式因此花 4 次 permutation。**这是草稿真正的概念错误。**

⚠️ **修订 B —— 156 是罕见 seed，众数是 144**：`MLKEM_GEN_MATRIX_NBLOCKS = 3` 块 = 504 B = 336 个 12-bit 样本，接受概率 3329/4096。精确二项（`Fraction`，非正态近似）：

| 事件 | 精确概率 |
|---|---|
| 单个多项式需要 ≥1 补块：P(Bin(336, 3329/4096) < 256) | **8.327692×10⁻³** |
| 需要 ≥2 补块 | 2.22×10⁻³² |
| E[每多项式补块数] | **0.0083277** |
| P(一个 4 宽批需要 ≥1 轮补块) = 1−(1−Q₁)⁴ | **0.0328970** |

**但 6 次 x4 批调用不独立**：三次 `mlk_gen_matrix` 从同一个 ρ 展开同一个矩阵（keypair `transposed=0`，encaps 与 decaps 重加密 `transposed=1`）。**一轮往返只有 9 次独立的多项式抽样**，被在两种 4/4/1 划分下各数三遍。插桩 trace 精确证实：**同一个亏损多项式 seed(i=2,j=1) 出现三次，都只接受了 244/256**。

穷举全部 2⁹ 亏损模式：

| 总 permutation 数 | 概率 | x4 组 | 顺序步 | perms/步 | lane 利用率 |
|---|---|---|---|---|---|
| **144** | **0.927499** | 24 | 72 | **2.0000** | **50.00%** |
| 147 | 0.007789 | 24 | 75 | 1.960 | 49.0% |
| **156 ← 本次 run** | **0.062704** | 27 | 75 | **2.0800** | **52.00%** |
| 159…171 | Σ ≈ 0.002 | | | | |

E[x4 补块组] = 0.19738；**E[总 permutation] = 144.8145**；P(≥3 轮补块) = **0.0647**（约 1/15 的 seed，不是 1/10000）。

> **草稿的"~11% rejection 尾部"是 3/27，一个 per-seed 实测分数，不是预测，且是总体均值的 ~14×。诚实的表述：补块在期望上花掉 x4 组的 0.82%（permutation 的 0.55%）；这个 seed 付了 11%。**

### 3.3 宽度扫描：解析模型 vs 实测（本报告最重要的一节）

#### 3.3a 两个"串行项"，不要混

| 模型 | 串行项 | 含义 |
|---|---|---|
| **x4-API 口径**（库原生，宽度硬编码为 4） | **48** = 39 串行 sponge + 9（第 9 个矩阵多项式，因 9 mod 4 = 1 而落单） | 顺序步 = 27 + 48 = 75 |
| **真 xN 口径**（lane 私有流，宽度自由） | **39** = kp 10 + enc 10 + **dec 19** | `slots(W) = 9·⌈9/W⌉ + ⌈6/W⌉ + 2·⌈7/W⌉ + 39` |

⚠️ **修订**：草稿的串行项 30 漏了 `mlk_kem_check_sk()` 的 9 次（`kem.c:104` 的 `mlk_hash_h(test, sk + MLKEM_INDCPA_SECRETKEYBYTES, …)`），30 → **39**。用 `MLK_CONFIG_SERIAL_FIPS202_ONLY` 构建实测总数 **143**（kp 44 / enc 45 / dec 54），分解为 gen_matrix XOF 84 + 噪声 PRF 20 + 串行链 39 = 143 ✔。

#### 3.3b 解析模型（两种串行项并列）

| W | slots (serial=30，草稿) | Keccak | e2e | **slots (serial=39，实测)** | **Keccak** | **e2e @ f=0.723** |
|---|---|---|---|---|---|---|
| 1 | 131 | 1.000 | 1.000 | **140** | 1.000 | 1.000 |
| 2 | 86 | 1.523 | 1.330 | **95** | 1.474 | 1.303 |
| 4 | 63 | 2.079 | 1.601 | **72** | 1.944 | 1.541 |
| 8 | 51 | 2.569 | 1.791 | **60** | 2.333 | 1.704 |
| ≥9 | 42 | 3.119 | 1.965 | **51** | 2.745 | 1.851 |

草稿的 131/86/63/51/42 与 2.08/2.57/3.12、1.60/1.79/1.97 **全部逐位复现**，模型本身自洽；修正串行项后 W→∞ 天花板从 3.12×/1.97× 降到 **2.75×/1.85×**。

#### 3.3c 实测：真实 lane，完整 ML-KEM-768（同一二进制，arena 构建）**[本轮新测]**

| W | slots（计数） | 总 cycles | e2e 加速 | slot 比 |
|---|---|---|---|---|
| 1 | 156 | 36,095,100 | 1.000 | 1.000 |
| 2 | 102 | 28,206,763 | **1.280** | 1.529 |
| 4 | 75 | 24,133,810 | **1.496** | 2.080 |
| 8（`VX_CFG_NUM_THREADS=8`） | **75** | **35,138,617** | **1.027** | 2.080 |

W=2 与 W=4 两个点独立解出的 Keccak 归属周期是 **22.79 M 与 23.04 M（相差 1.1%）**——"Keccak 时间 ∝ slots、其余不变"这个机制在 W≤4 上被**定量验证**。W=8 那一行是草稿前提的直接反证：**x4 API 把批宽封在 4，slot 数不变（75），而 run 慢了 46%。**

#### 3.3d 实测：真 xN，lane 私有流（`genmat_xn`，`VX_CFG_NUM_THREADS=16`）**[本轮新测]**

| W | gen_matrix slots | cycles | 加速 | **cycles/slot** | 噪声 slots | cycles | 加速 |
|---|---|---|---|---|---|---|---|
| 1 | 27 | 5,036,925 | 1.000 | 186,553 | 7 | 1,295,887 | 1.000 |
| 2 | 15 | 2,887,424 | 1.744 | 192,495 | 4 | 755,109 | 1.716 |
| 3 | 9 | 1,816,397 | 2.773 | 201,822 | 3 | 578,689 | 2.239 |
| 4 | 9 | 1,824,225 | 2.761 | 202,692 | 2 | 401,658 | 3.226 |
| 5 | 6 | 1,469,569 | **3.427** | 244,928 | 2 | 461,175 | 2.810 |
| 6 | 6 | 1,532,304 | 3.287 | 255,384 | 2 | 484,599 | 2.674 |
| 8 | 6 | 1,701,358 | **2.961** | 283,560 | 1 | 332,154 | 3.901 |
| 9 | 3 | 1,263,928 | **3.985** | **421,309** | 1 | 331,775 | 3.906 |
| 12 | 3 | 1,263,020 | 3.988 | 421,007 | 1 | 331,818 | 3.905 |
| 16 | 3 | 1,264,562 | 3.983 | 421,521 | 1 | 332,410 | 3.898 |

slot 计数在每个 W 上都精确等于 `3·⌈9/W⌉` 与 `⌈7/W⌉` —— **slot 算术是对的，错的是成本模型**：cycles/slot 从 W=1 到 W=9 上升 **2.26×**，于是 gen_matrix 饱和在 **3.99×** 而不是 9×，**W=8 在相同 slot 数下比 W=5 更慢**。`PERF` 给出机制：IPC 0.102 (W=1) → 0.092 (W=4) → **0.057 (W=9)**。

#### 3.3e 合成端到端（实测微基准 + 实测固定项）

`Total(W) = 14,984,147 + 3·genmat(W) + 3·noise(W)`，固定项 = 39 次串行 sponge（5,919,303）+ 非 Keccak 算术（9,064,844）。
**自洽检验**：合成 W=1 = 33,797,456 vs 实测 `SERIAL_FIPS202_ONLY` 臂 33,605,629 —— **偏差 0.57%，无任何拟合参数。**

| W | 1 | 2 | 3 | **4** | 5 | 6 | **8** | **9** | 16 |
|---|---|---|---|---|---|---|---|---|---|
| e2e | 1.000 | 1.310 | 1.530 | **1.564** | 1.632 | 1.612 | **1.607** | **1.714** | 1.713 |

#### 3.3f 三个模型的对账

| 论断 | 草稿（投影） | 修正后的 slot 模型 | **实测** |
|---|---|---|---|
| W=4 e2e | 1.60× | 1.54× | **1.44–1.56×** |
| W=8 e2e | 1.79× | 1.70× | **1.61×**（合成）；**1.03×**（真 x4 API） |
| W=9 e2e | 1.97× | 1.85× | **1.71×**，饱和 |
| **W=4→9 增益** | **+23%** | +20% | **+9.6%** |
| **W=4→8 增益** | **+12%** | +11% | **+2.7%** |

⚠️ **修订（结论替换）**：草稿写"`VX_CFG_SIMD_WIDTH = 4` 恰好是 ML-KEM-768 批式 Keccak 的最优宽度，因为 x4 API 封顶在 4"。**这个论证是库 API 的人造物，必须弃用。** 换成实测版本，它更强也更可辩护：

> **lane 宽度过 4 的边际端到端回报 ≤ 10%，且用真实 x4 API 在 W=8 转为 −46%；而 warp 数的边际回报线性到 4 warp（3.97×）。因此 lane 宽度不是该花面积的轴。**

并且 `VX_CFG_NUM_THREADS=8/16` 隐含 `VX_CFG_SIMD_WIDTH=8/16`（`VX_config.toml:56`），即 2×/4× 的 ALU/LSU/RF 宽度——**W≥9 那 +9.6% 是用 4× 数据通路买的。**

#### 3.3g "一个 backend hook，库保持 pristine" —— 对 x4 成立，对 xN 不成立 **[源码]**

mlkem-native 的 FIPS-202 backend 接口（`mlkem/src/fips202/native/api.h`）原文：*"You can replace 1-fold or 4-fold batched Keccak-F1600."* 只有 `MLK_USE_NATIVE_FIPS202_X1` / `_X4`。宽度 4 硬编码在库**核心**的六处，没有一处在 hook 后面（路径相对 `pqc/third_party/mlkem-native/mlkem/`）：

| file:line | 内容 |
|---|---|
| `src/fips202/keccakf1600.h:11` | `#define MLK_KECCAK_WAY 4`（无 x2/x8 变体） |
| `src/fips202/fips202x4.h:18-21,37` | `mlk_shake128x4ctx` 尺寸 `LANES*WAY`；`squeezeblocks(out0..out3, …)` |
| `src/fips202/fips202x4.c:164-173` | `mlk_shake256x4` 手写 `tmp0..tmp3` |
| `src/symmetric.h:44,63` | `mlk_prf_eta1_x4`、`mlk_xof_x4_squeezeblocks` 文本展开四个下标 |
| `src/sampling.c:155,161` | `mlk_poly_rej_uniform_x4(vec0..vec3, …)`、`buf[4][…]`、`ctr[4]` |
| `src/indcpa.c:246` / `src/poly_k.c:348` | `for (i=0; i<(K*K/4)*4; i+=4)`；`mlk_poly_getnoise_eta1_4x(r0..r3)` |

所以 **xN 是对库核心的 fork**，会丢掉 `tests/pqc/mlkem/vortex_mlkem_config.h:10-12` 自己声明的那条性质（"Reached through `-DMLK_CONFIG_FILE`, the library's own override hook, so the submodule tree is read-only to this test"）。`tests/pqc/genmat_xn/` 的做法是把 xN 循环放在**库之上**，只调用库的非批原语，从而绕开这一点。

**附带发现：x4 API 有一笔隐藏的 SIMT 税。** 它的四个状态共享一个交错的 `4×25 uint64` 数组，lane 并行后端必须在每次 permutation 前后 scatter/gather 200 个字。实测：`(36,095,100 − 33,605,629) − 13 × 151,777 = 516,370` cycles / 27 次 x4 调用 = **19,125 cycles/次 = 一次 permutation 的 12.6%**。lane 私有的 xN 完全不付这笔钱。

### 3.4 NTT：从来不是宽度瓶颈

`mlk_gen_matrix` 直接在 NTT 域做 rejection sampling，**9 个矩阵多项式贡献 0 次变换**（常见误解）。k=3 实际计数（与计数器 15 fwd + 9 inv 完全吻合）**[源码]**：

| 阶段 | 例程 | 源码行 | fwd | inv |
|---|---|---|---|---|
| keypair | `polyvec_ntt(skpv)` + `polyvec_ntt(e)` | `indcpa.c:502-503` | 3+3 = **6** | 0 |
| encaps | `polyvec_ntt(sp)` | `indcpa.c:580` | **3** | — |
| encaps | `polyvec_invntt_tomont(b)` + `poly_invntt_tomont(v)` | `indcpa.c:586-587` | — | 3+1 = **4** |
| decaps | `polyvec_ntt(b)` | `indcpa.c:641` | **3** | — |
| decaps | `poly_invntt_tomont(sb)` | `indcpa.c:644` | — | **1** |
| decaps | **`indcpa_enc` 重加密（Alg 18 step 8）—— 草稿漏掉的一行** | `kem.c:443` → `indcpa.c:580,586-587` | **3** | **4** |
| | **合计** | | **15** | **9** |

⚠️ **修订**：草稿的 decaps 行写成 "3 fwd + 1 inv"，漏了重加密。补上后 12+5 → 15+9，与计数器一直说的一致。逐阶段：**kp 6/0、enc 3/4、dec 6/5**。归因检验：15×160,220 + 9×249,571 = 4,649,439 = **14.20%** ✔（只有表错了，数字从来没错）。

每蝶形实测成本 = 4,649,439 / 21,504 = **216 cycles/butterfly**（ML-DSA：28,799,390 / 105,472 = **273**）。这个荒谬的数字本身就是论文的一张图：`-march=rv32imaf` 没有 Zbb、没有旋转指令，Montgomery 约简全靠 `mul/mulh`。

**最窄的阶段仍有 128 个独立蝶形**，4 / 16 / 32 lane 全部喂得饱；只有最后两层的蝶形伙伴距离 < lane 数，需要一次 `vx_shfl_bfly`（`sw/kernel/include/vx_intrinsics.h:436`，在 Vortex 上是**一条 ALU 指令**，见 §4.1）。

### 3.5 ML-DSA：3.1% 不是结构性的 —— 同一条 Pareto 曲线上的两个点

⚠️ **修订（本报告第二重要的更正）**：草稿断言 ML-DSA 的 3.1% 批宽是 `MLD_CONFIG_REDUCE_RAM`（为塞进 Vortex 内存而必须开）的结构性后果，并据此写下 R7"ML-DSA 无法 per-lane 批"。**两条腿都断了。**

#### 3.5a 真正的原因：`polyvec_lazy.h:427` 的一个字段 **[源码]**

`pqc/third_party/mldsa-native/mldsa/src/polyvec_lazy.h:427` 是 `mld_poly cur; /**< On-demand sampled matrix element A[k][l]. */` —— **一个单槽暂存**。lazy 路径逐 entry 调 `mld_polymat_expand_entry`，窗口宽度因此天然被钉死为 1。这不是算法约束，是数据结构。

各批接口的 guard 分布（比"被 REDUCE_RAM 编译掉"精确得多）：

| 批接口 | guard | REDUCE_RAM 下 |
|---|---|---|
| `mld_poly_uniform_4x`（ExpandA, SHAKE128） | `!SERIAL_FIPS202_ONLY && (!REDUCE_RAM \|\| UNIT_TEST)` | **被编译掉** |
| `mld_shake128x4_*` | `!REDUCE_RAM \|\| UNIT_TEST` | **被编译掉** |
| `mld_poly_uniform_eta_4x`（ExpandS, SHAKE256） | `!SERIAL_FIPS202_ONLY` | **在** ← **这就是那 3.1% 的全部来源** |
| `mld_poly_uniform_gamma1_4x`（ExpandMask） | `!SERIAL && (!REDUCE_RAM \|\| UNIT_TEST)` | 被编译掉 |
| `mld_keccakf1600x4_permute` 及 x4 xor/extract | `(!NO_KEYPAIR_API \|\| !REDUCE_RAM \|\| UNIT_TEST) && !SERIAL` | **在**（keypair API 开着时） |

**4 路 Keccak 的底层原语在 REDUCE_RAM 下本来就编进去了**（`keccakf1600.c:187-199`）；被 `#if` 掐掉的只有 SHAKE128 那层薄封装。移植成本 = 放宽两个 `#if` + 把 `cur` 变成 `cur[4]`，**不是重写**。

#### 3.5b Pareto 点 1：`polyvec_lazy` 4-entry 窗口（host 实测）**[本轮新测]**

改动 6 个文件、**+148 / −59 行**，patch 已就绪；正确性：50 次随机 keygen+sign+verify，pk/sk/sig 的 FNV 哈希与未改动 build **逐位相同**。

峰值栈（pthread 固定栈 + 0xA5 painting，ML-DSA-65，40 次）：

| build | keypair | sign | verify | Δ vs base |
|---|---|---|---|---|
| REDUCE_RAM 上游 | 21,128 | 18,344 | 16,968 | — |
| **+ x4 窗口** | 27,784 | 25,064 | 23,624 | **+6,656 / +6,720 / +6,656** |
| + x4 padded tail | 27,784 | 25,096 | 23,624 | +6,656 / +6,752 / +6,656 |
| eager（关 REDUCE_RAM） | 54,248 | 77,992 | 50,088 | **+33,120 / +59,648 / +33,120** |

+6.6 KB 的拆解对得上：`cur[1]→cur[4]` +3,072 B · `buf[4][840]` vs `buf[840]` +2,520 B · `mld_shake128x4ctx`(800) vs `mld_shake128ctx`(≈208) +592 B · `seed_ext4[4][40]` +160 B = 6,344 B + 对齐 ≈ 6,656 B ✔
⚠️ **修订**：gap 描述的"+4 KB"应改成 **+6.6 KB**；而"整矩阵 A = 6×5×256×4 = 30,720 B"这个锚点是对的，但真正关掉 REDUCE_RAM 的代价是 **sign +59,648 B**。

可批比例（5000 trial，mean attempts 5.0708；"slots" = 一个 x4 组算 1 步）：

| | lane_perms | batched | batched % | **slots** | Keccak 加速 |
|---|---|---|---|---|---|
| REDUCE_RAM 上游 | 1399.27 | 24.00 | **1.72%** | 1381.27 | 1.000× |
| **+ x4 窗口** | 1399.27 | 771.08 | **55.11%** | 820.96 | **1.682×** |
| **+ x4 padded tail** | 1652.81 | 1278.16 | **77.33%** | 694.19 | **1.990×** |
| eager | 683.34 | 545.42 | 79.82% | 274.27 | 5.036× |

三个参数集（各 2000 trial）：

| | base slots (batched%) | x4 slots (%) | x4pad slots | x4 加速 | x4pad 加速 |
|---|---|---|---|---|---|
| **ML-DSA-44 (4,4)** | 747.79 (2.06%) | **361.21 (69.92%)** | 361.21 | **2.070×** | 2.070× |
| ML-DSA-65 (6,5) | 1360.26 (1.74%) | 807.73 (55.19%) | **683.56 (77.30%)** | 1.684× | **1.990×** |
| ML-DSA-87 (8,7) | 1937.25 (1.60%) | **918.15 (70.90%)** | 918.15 | **2.110×** | 2.110× |

> **ML-DSA-44 是"零摩擦"情形**：`row_lazy` 内层 ℓ=4、`yvec_lazy` 内层 k=4，两个窗口都正好 4，**没有尾巴、没有 padding 浪费，ExpandA 100% 走 x4**。对一台 `SIMD_WIDTH=4` 的机器，这是恰好匹配的参数集，应当作为主结果之一。

x86-64 wall-clock（5000 trial，µs/op）：base 71.1/397.0/61.2 → x4 窗口 73.0/**399.1 (+0.5%)**/61.6 → x4 padded 72.8/**454.9 (+14.6%)**/61.4 → eager 67.9/208.6/57.9。
**x4 窗口在标量机上几乎免费（+0.5%）**——它只是把工作重排成 4 条可并行的流；**padding 在标量机上要多花 14.6%，只有真 4-lane Keccak 才回本。** 这正是"ISA 扩展带来的重排收益"型证据。

#### 3.5c Pareto 点 2：直接关掉 `MLD_CONFIG_REDUCE_RAM`（Vortex 实测）**[本轮新测]**

在本仓库 SimX 上直接测（`tests/pqc/mldsa/vortex_mldsa_config.h:27` 的 `#define MLD_CONFIG_REDUCE_RAM` 注释掉）：

| 口径 | REDUCE_RAM **开**（当前基线） | REDUCE_RAM **关**（eager） | Δ |
|---|---|---|---|
| `keccak_f1600_x1`（**总** permutation） | 782 | **582** | −25.6% |
| `keccak_f1600_x4`（**组**数） | 6 | **121** | ×20.2 |
| 批内 permutation = 4 × 组 | 24 | **484** | |
| **批比例** | **3.1%** | **83.2%** | |
| 4-lane 顺序步 slots = 组 + 串行 | 764 | **219** | |
| **Keccak 步数加速（若真 4-lane）** | 1.02× | **2.66×** | |
| `ARENA: peak` | 21,568 B | **86,912 B** | **+65,344 B** |
| 总 cycles | 189,821,707 | **142,030,250** | **−25.2%** |
| ├ keypair | 42,029,462 | 39,125,147 | −6.9% |
| ├ **sign** | 107,014,136 | **64,690,774** | **−39.5%（1.65×）** |
| └ verify | 40,778,109 | 38,214,329 | −6.3% |

三条读法：

1. **`MLD_CONFIG_REDUCE_RAM` 同时买走了 25.2% 的周期和 96.9% 的批处理度，换来 65 KB 内存。** 这是一个极陡的取舍，而库 README 只说 *"This trades memory for performance"*，**没有提 lazy re-expansion 的代价**。
2. **省下的不是"更快的 Keccak"，是"更少的 Keccak"**：总 permutation 从 782 掉到 582（−200）。Keccak 占比几乎没变（62.5% → 62.2%），非 Keccak 部分也同步降了 24.5%。⚠️ 这 200 次的逐调用点归属**未测**（推断为 lazy 在每次签名重试里重新展开 A 与 y 的冗余计算，host 侧 5000-trial 的对应量是 5.07× 冗余展开），标记为**推导，非实测**。
3. **两个 Pareto 点并列**（ML-DSA-65，Vortex 与 host 口径分开列，勿混）：

| 方案 | 内存代价 | 批比例 | Keccak 步数加速 | 端到端 | 口径 |
|---|---|---|---|---|---|
| 现状（REDUCE_RAM 开） | 基准 | 3.1% | 1.02× | 基准 | Vortex 实测 |
| **+ `polyvec_lazy` 4 宽窗口** | **+6.6 KB** 峰值栈 | 55.1%（782 口径 59.3%） | **1.68×** | 标量机 +0.5% | host 实测 |
| **+ padded tail** | +6.7 KB | 77.3%（75.3%） | **1.99×** | 标量机 +14.6% | host 实测 |
| **关 REDUCE_RAM（eager）** | **+65.3 KB** arena | **83.2%** | **2.66×** | **−25.2% cycles** | **Vortex 实测** |

**中间那两行是明显更好的性价比点，而且它们与最后一行不互斥。** 论文应当把这四点画成一条曲线，而不是二选一。

#### 3.5d 单条 ML-DSA-65 消息的 Amdahl 下界

不可批的只有串行 sponge（OTHER 76.50 slots：H(pk)/μ/ρ''/c̃）+ SampleInBall（6.07）= **82.57 slots**。理想 4 路批一切 = 82.57 + (1399.27−82.57)/4 = 411.75 slots → **上限 3.40×** **[本轮新测]**。

端到端 = 1/((1−f) + f/S)：

| f（Keccak 占比） | S=1.68 | S=1.99 | S=2.49（+ExpandMask 窗口，投影） | S=3.40（上限） |
|---|---|---|---|---|
| 0.43（x86 实测） | 1.21× | 1.27× | 1.32× | 1.38× |
| **0.625（Vortex 实测）** | **1.36×** | **1.48×** | **1.56×** | **1.79×** |
| 0.80 | 1.48× | 1.66× | 1.81× | 2.13× |

⚠️ 草稿与 gap-fill 都用 f≈0.72 做 ML-DSA 的参数化。**Vortex 上 ML-DSA 的实测 f 是 0.625，不是 0.72**（0.72 是 ML-KEM 的）。上表已改用实测值。

### 3.6 计数器语义：`X1` 是总数，`X4` 是组数（极易误读）**[源码]**

`mlk_keccakf1600x4_permute`（`pqc/third_party/mlkem-native/mlkem/src/fips202/keccakf1600.c:179`）在无 native hook 时的 C fallback 是：

```c
void mlk_keccakf1600x4_permute(uint64_t *state)
{
#if defined(MLK_USE_NATIVE_FIPS202_X4)
  if (mlk_keccak_f1600_x4_native(state) == MLK_NATIVE_FUNC_SUCCESS) return;
#endif
  mlk_keccakf1600_permute(state + MLK_KECCAK_LANES * 0);
  mlk_keccakf1600_permute(state + MLK_KECCAK_LANES * 1);
  mlk_keccakf1600_permute(state + MLK_KECCAK_LANES * 2);
  mlk_keccakf1600_permute(state + MLK_KECCAK_LANES * 3);
}
```

而 x1 的 native hook 正好坐在 `mlk_keccakf1600_permute`（同文件 `:479-488`）里，`tests/pqc/mlkem_profile/mlk_prof_fips202.h:29` 的 `mlk_prof_counts[MLK_PROF_KECCAK_X1]++` 就挂在那里。**因此：**

> **`MLK_PROF_KECCAK_X1` = 总 permutation 数（已经包含每个 x4 组的四次内层 permutation）；`MLK_PROF_KECCAK_X4` = x4 组数。串行 permutation 数 = X1 − 4×X4。**
> mldsa 同构：`mld_keccakf1600x4_permute` 在 `pqc/third_party/mldsa-native/mldsa/src/fips202/keccakf1600.c:187-199`，x1 hook 在 `:490`，计数器在 `tests/pqc/mldsa_profile/mld_prof_fips202.h:29,41`。

据此，ML-KEM 156/27 → 串行 48；ML-DSA 782/6 → 串行 758；eager ML-DSA 582/121 → 串行 98。**不要把 X1 和 4×X4 相加。**

**另一个计数器陷阱**：`MLK_PROF_REJ_UNIFORM` 读到 **27**，与 `MLK_PROF_KECCAK_X4` 的 27 相同，纯属巧合——native `rej_uniform` hook 被 `offset == 0` 门控（`sampling.c:127`），只数**初始**采样调用 = 9 多项式 × 3 阶段 = 27，从不数补块调用。**两者不可互读。**

---

## §4 映射到 Vortex

### 4.1 三条必须先接受的硬件事实 **[源码]**

1. **一个 core 只有一套前端。** `hw/rtl/core/VX_core.sv` 只实例化一个 `VX_scheduler`/`VX_fetch`/`VX_decode`；`docs/designs/microarchitecture.md` 明说 "a single warp issued per cycle"；`VX_CFG_ISSUE_WIDTH = "expr: up($VX_CFG_NUM_WARPS / 16)"`（`VX_config.toml:55`）在 4 warp 下 = 1。**峰值 = 1 instr/cycle × SIMD_WIDTH lanes。** 推论：*增加 warp 只买延迟隐藏，永远不抬吞吐上限*——⚠️ 但 §0 的实测说明，**在 IPC = 0.10 的负载上，"只买延迟隐藏"恰恰就是那 3.97×**。仓库里的负面先例（RTU slot 1→4 在 rt_raycast 上慢 28.5%）适用于已饱和的前端，不适用于 IPC 0.10。
2. **跨 lane 通信在 Vortex 上近乎免费。** `vx_vote_{all,any,ballot}`（`sw/kernel/include/vx_intrinsics.h:382`…）、`vx_shfl_{up,down,bfly,idx}`（`:436`）、`vx_wgather`（`:523`）全部在 `hw/rtl/core/VX_alu_int.sv` 里组合实现（`:136-155` VOTE、`:179-199` SHFL）——**一条 ALU 指令，不走 shared memory**。SHFL lane 索引 6 bit（`VX_alu_int.sv:181` 的 `STATIC_ASSERT(LANE_BITS <= 6, …)`），协作组上限 64 lane。这让 NTT 最后两层的蝶形交换与 rejection sampling 的 ballot+compaction 变成 1–2 条指令，也说明 GOLF 对 `__shfl_sync` 开销的批评在 Vortex 上不直接适用。
3. **Divergence 深度硬顶 = `NUM_THREADS − 1` = 3（NT=4）**，SimX 溢出直接 abort；`SPLIT/JOIN` 是 `is_wstall`。嵌套的 per-lane accept/reject 循环放不下，必须改写成 ballot + compaction。

### 4.2 内存与栈：真正的分水岭，以及一个基线里潜伏的 bug

| 量 | 数值 | 出处 |
|---|---|---|
| ML-KEM-768 峰值栈（库默认，x4 API） | **18,040 B** | 0xA5 涂色低水位扫描 **[本轮新测]** |
| ML-KEM-768 峰值栈（`MLK_CONFIG_CUSTOM_ALLOC_FREE` arena） | **4,632 B**（arena 峰值 19,232 B） | 同上 |
| ML-KEM-768 峰值栈（arena + `SERIAL_FIPS202_ONLY`） | **1,928 B** | 同上 |
| **每 hart 栈槽** | **8,192 B**（`VX_MEM_STACK_LOG2_SIZE = 13`），按 hartid 索引、从 `0xFFFF0000` 连续向下 | `VX_types.toml:21-22`；`sw/kernel/src/vx_start.S:95-98` 的 `LOAD_IMMEDIATE64(sp, VX_MEM_STACK_BASE_ADDR); csrr t0, VX_CSR_MHARTID; sll t1, t0, VX_MEM_STACK_LOG2_SIZE; sub sp, sp, t1` **[源码]** |
| ML-DSA arena（`MLD_ARENA_BYTES = 128*1024`，单一 file-scope arena） | 峰值 21,568 B（REDUCE_RAM 开）/ **86,912 B**（关） | `tests/pqc/mldsa/mld_vortex_alloc.h:36`；**[本轮新测]** |
| L1 D$ / I$ / LMEM | 16 KB / 16 KB / 16 KB，L2 与 L3 关闭 | `VX_config.toml:182,193,253,16,17` |

> ### 🔴 CRITICAL：单-lane ML-KEM 基线已经越界，只是看不出来
>
> **峰值栈 18,040 B > 8,192 B 的槽 = 越界 2.20 倍（超出 9,848 B）。** 它之所以"能跑"，只是因为 hart 1..15 空闲，它们的槽吸收了溢出的涂写。**在同一条消息上点亮第二个 lane，立刻得到 `Error: misaligned memory access: addr=0x36e080b`** —— hart 0 的栈帧落在 hart 1 的活栈上。
> `tests/pqc/mldsa/` 已经撞过同一堵墙并绕开了（`mld_vortex_alloc.h`，`MLD_CONFIG_CUSTOM_ALLOC_FREE`，`vortex_mldsa_config.h:31`）；`tests/pqc/mlkem/` 还没有。
> **任何 T>1 的实验臂在修好之前都是静默损坏的。**

批宽的容量交叉点（每流实测斜率 **901.3 B**，结构核算 744 B/流 + 对齐）**[本轮新测]**：

| 构建 | peak(N) | 8,192 B 槽能放下的最大 N |
|---|---|---|
| 库默认（全在栈上） | 14,435 + 901·N | **不存在**（N=1 就要 15.3 KB） |
| `MLK_CONFIG_CUSTOM_ALLOC_FREE` arena | 1,027 + 901·N | **N\* = 7**（N=8 差 43 B；N=9 要 9,139 B） |
| 同上，`VX_MEM_STACK_LOG2_SIZE = 14` | | N\* = 17 |
| 同上，`VX_MEM_STACK_LOG2_SIZE = 15` | | N\* = 35 |

⚠️ **注意**：`VX_MEM_STACK_LOG2_SIZE` 在 `VX_types.toml` 而**不在** `VX_config.toml`，**不能**用 `CONFIGS=-DVX_CFG_*` 覆盖；改了之后必须从 `build/` 重跑 `../configure`，否则 SimX 静默使用陈旧的 `VX_config.h`。

⚠️ **修订**：草稿写"4 条并发消息会把 19 KB 工作集乘 4，撞上 8 KB/hart 栈和 16 KB D$，所以消息内并行更优"。**实测反过来**：2 warp × 1 lane 的两条独立消息只多花 0.008% 的时间（§0.1），因为两条消息在**两个不同 hart 的槽**里，各自 4,632 B（arena 构建）完全放得下；而**多-lane 的端到端臂反而因为冗余 SPMD 把工作集乘了 W，这才是 W=8 崩掉的原因**。真正的约束是 8 KB/hart 的槽，不是 16 KB D$，而 warp 轴恰好每加一条消息就多一个槽。

### 4.3 crux：per-lane 复制 vs per-core 共享的 Keccak 单元

Vortex 有三条已落地的先例 **[源码]**：

| 放置 | 机制 | 面积 | 先例 |
|---|---|---|---|
| **per-lane 复制** | `NUM_LANES = SIMD_WIDTH` | × SIMD_WIDTH | ALU / FPU / TCU；Adams et al. 的 AES S-box *"we duplicate this hardware for every thread"* |
| **per-core 共享 SFU PE** | `BLOCK_SIZE = 1`，扩 `PE_IDX_*` 列表（`hw/rtl/core/VX_sfu_unit.sv:73-88`：`PE_IDX_WCTL / CSRS / DXA / TEX / OM / RASTER / RTUW`） | ×1 | WCTL / CSRS / DXA / TEX / OM / RASTER / RTUW |
| **socket / cluster 共享** | 薄 SFU 前端 + 异步 handle + slot pool | ×1 / socket | RTU、DXA |

若 `NUM_LANES < SIMD_WIDTH`，`hw/rtl/core/VX_lane_dispatch.sv` 自动把一次 warp 拆成 `SIMD_WIDTH/NUM_LANES` 个 packet 串行——**这正是"窄的共享单元仍能服务整个 warp"的现成机制**。

**利用率算术**（用实测的 143 permutation/消息、9,064,844 cycles 非 Keccak 工作）**[本轮新测]**：

| 硬件 permutation 延迟 | 单消息 Keccak 总周期 | 占非 Keccak 时间的比例 |
|---|---|---|
| 24 cycles/perm | 3,432 | **0.038%**（99.96% 空闲） |
| 50 cycles/perm | 7,150 | 0.079% |
| 1,000 cycles/perm | 143,000 | 1.58% |

4 warp 各跑一条独立消息时仍然 99.85% 空闲。**在任何合理的硬件 permutation 延迟（24–1000 cycles）下，per-lane 与 per-core 的端到端差异 < 1%；要到约 5,600 cycles/perm 才到 5%。** 因为软件 Keccak 是 151,777 cycles 而硬件是两三个数量级更小，4× 的串行化惩罚在 3.4–3.6× 的 Amdahl 天花板前完全不可见。

> **推荐（保留草稿的结论，但换掉论据）：把 Keccak-f1600 做成 per-core 共享的 SFU PE（新增一个 `PE_IDX_*`），状态放加速器私有 SRAM，异步 launch→handle→wait；NTT 蝶形做成 per-lane ALU 形指令。**

论据现在是三条，其中第三条**目前空转**：

1. **性能上不可区分**（上表）——所以应由面积决定。**[本轮新测]**
2. **寄存器堆装不下状态**：每 lane 32 GP + 32 FP × 32 bit = 2048 bit；一个 1600-bit 状态 = 50 个寄存器，而 `NUM_SRC_OPDS = 3`。SoK 明确点名的反模式正是"用 set/get 指令填一个专用寄存器堆"。状态**必须**在加速器 SRAM 或 LMEM，per-lane 就要 4×200 B 状态 SRAM + 4× 数据通路。**[源码]+[全文，前序核]**
3. ⚠️ **面积（R11，空转）**：Li/Mentens/Picek 的 1/3/6 状态 = 7,323/24,789/48,180 slices 是**严格线性**的证据 **[全文，前序核]**，Adams et al. 在 Vortex 上复制一个 128 门 AES S-box 就让 16 核 ALM 从 80.24% 升到 85.78%、fmax 从 192 掉到 177 MHz（−7.8%）**[全文，前序核]**。但**本轮没有取得任何可引用的 Keccak-f1600 硬核基准（LUT/FF/BRAM + cycles/permutation + fmax）**。**在补上之前，论文不得写成"面积证据表明应当共享"，只能写成"性能上不可区分，因此应由面积决定；面积对比见 E6"。**

**per-lane 一方最强的反驳（必须正面回应，不能绕过）**：per-lane 4 路 stream-batching **不需要任何 lane 间交换**——`mlk_keccakf1600x4_permute` / `mld_keccakf1600x4_permute` 就是 4 个完全独立的 25-lane 状态，SIMT 下 4 个 thread 各跑一个。这和"25 threads 做一个 state"（θ/ρ/π 要 cross-lane shuffle）是完全不同的机制。而 §1 的 XOF 分类说明，**独立流 XOF 占 ML-DSA-65 单消息 slot 的 93.7%**，天然适合 per-lane。**反驳这一条的唯一依据就是利用率算术（0.04–1.6%）与面积**，不是"per-lane 做不到"。

**Fmax 风险**：V80 目标 300 MHz，unit-level baseline 已在 290–298 MHz **[本地实测]**。θ 是 5 输入 64-bit XOR 树 + 旋转 + 再 XOR（约 4–5 级逻辑），χ 2 级。单轮组合应可过，**但不要展开 2 轮**。

---

## §5 对当前单-lane 基线的评价

**可辩护，作为 ISA 的分母；但必须先修一个 bug、对齐一个构建、修一个消融缺陷。**

**它证明了什么**：在完全相同的 core、完全相同的软件、只差一个 config header（`tests/pqc/mlkem/vortex_mlkem_config.h`，经 `-DMLK_CONFIG_FILE` 这个库自己的 hook 进入）的前提下，指令的边际收益。这正是 RISQ-V、PQ.V.ALU.E、Alkim et al.、HORCRUX、OpenTitan、Ye et al. 全部采用的方法学，也是 SoK 的 Concept 18 里的 "kernel-level" 档。

**它没有证明什么，以及审稿人会怎么杀**：

1. **"你只用了 1/16 的机器。"** 而且这不是修辞——**单-lane 基线是内存延迟裸露的**，IPC ≈ 0.104 ≈ 峰值的 2.6%。⚠️ **修订**：草稿说"只要填满 4 个 warp，理论上就能拿到 ~9.35×，一行 ISA 都不用改"。**实测是 3.97×（4 warp，genmat_xn）与 2.000×（2 warp，完整 KEM）**，不是 9.35×——4 warp 就把槽用完了（8 CTA = 2 波 = 3.986×）。**论文里所有 speedup 的分母应当是 4W×1L 的吞吐口径，不是 1W×1L 的延迟口径。**
2. **软件基线太弱。** `-march=rv32imaf`：无 Zbb/Zbkb、无旋转指令、无向量。Zhang et al. 在 C908 上测得 reference-C RV32I = 15,779 cycles、手写 RV32I 汇编 = 7,808、RV32IB = 6,222——**基线选择本身有 2.5× 的杠杆** **[全文，前序核]**。Bolat et al. 更极端：同一条 `shatr` 指令对优化过的 RISC-V SHA-3 是 8.02×，对 Keccak 官方发行版是 46.31×（**5.8× 的差**）**[全文，前序核]**。
3. **simx-only 会被打回。** Matsumi & Mian 的 Vortex 自定义指令论文就是反面教材：simx-only、单配置、56.191× 微基准 → **1.024×** 应用 **[全文，前序核]**。而 Vortex 的 MICRO-54 原文扫了 5 组 warp×thread 与 6 个核数，每点都带 LUT/FF/BRAM/fmax。

**三个必须先修的缺陷（否则所有分母失效）**：

| 缺陷 | 现象 | 位置 |
|---|---|---|
| **D1：栈越界** | 峰值 18,040 B > 8,192 B 槽；T>1 立刻 misaligned memory access | `tests/pqc/mlkem/`（原型解法已在 `tests/pqc/mlkem_width/mlk_vortex_alloc.h`） |
| **D2：profile build ≠ baseline build** | `tests/pqc/mlkem_profile/mlk_prof_fips202.h` 自称 *"the code under measurement is byte-for-byte the code the baseline runs"*，实际不是：声明 `MLK_CONFIG_USE_NATIVE_BACKEND_FIPS202` + `MLK_USE_NATIVE_FIPS202_X4` 会抑制 `fips202.h` 里的 `FIPS202_X4_DEFAULT_IMPLEMENTATION`，把 `poly_k.c:364` 从串行 PRF 翻到 `mlk_prf_eta1_x4`。**baseline 152 perms / 21 组 / 68 串行 / 89 步 / 42.70% 利用率；profile 156 / 27 / 48 / 75 / 52.00%。** baseline 口径的 Keccak 占比是 **71.79%** | `tests/pqc/mlkem/vortex_mlkem_config.h` vs `tests/pqc/mlkem_profile/` |
| **D3：`ABLATE=keccak` 改变调用计数** | 同一个 header 自称 *"keeps the call counts identical to the baseline's"*，实测 **156 → 144，补块轮数归零**。原因：permutation 被 stub 掉后，squeeze 出来的是未置换的状态（seed‖0x1F‖0…‖0x80），几乎每个 12-bit 值都 < 3329，rejection 第一块就收满 256/256。消融 delta 因此还吞掉了 3 轮补块的 extract-bytes + `rej_uniform`。`ABLATE=ntt` 干净（156/27/15/9 不变） | `tests/pqc/mlkem_profile/Makefile:25-33` 的 `-DPQC_ABLATE_KECCAK` |

**最低补充评估清单（缺一即被杀）**：等线程数对比（分离 ISA 增益与并行增益）· 配置扫描 + 每点面积/fmax（照抄 Vortex MICRO-54 Table 3/4 格式）· 双列 speedup（各配置自身可达频率 + 归一到固定频率）· 批扫到饱和并报饱和点 · Alkim 六指标（cycle count / issued instructions / clock frequency / **wall-clock time** / area / **time-area product**）· 面积双尺度（SoK Pitfall #2：孤立 PE 与整核 delta 都报）· **ML-DSA 报分布而非均值**（Riou et al. 明确指出因 rejection sampling，*"min/average/max"* 对 ML-DSA 签名 *"unsuitable for migration planning"* **[摘要，前序核]**）。

**测量陷阱**：`mlkem_microbench` 里 `poly_rej_uniform` 报 576,795 cycles/call，27 次相乘 = 15.6M，**超过整个往返的一半**——它包含了 XOF squeeze，与 Keccak 表重复计数。**不要把 microbench 表相加**（`tests/pqc/mlkem_profile/main.cpp:44,47` 已经把 `keccak_f1600_x4` 与 `rej_uniform` 的成本置 0 并注释 "counted below the SHAKE it consumes"，正是这个原因）。同时 instrumented ML-KEM build 比 baseline 慢 8.1%（32.74M → 35.41M），任何跨 build 的比值都必须声明分母来自哪个 build。

---

## §6 具体实验矩阵（按优先级，已按实测重排）

配置记法 `C×W×T` = cores × warps × threads。★ = 论文必需；☆ = 加分。

| # | 实验 | 配置 | 负载形状 | 证明什么 | 成本 | 改 kernel？ |
|---|---|---|---|---|---|---|
| **E0 ★★** | **修 D1/D2/D3** | 1×1×1 与 1×1×2 | — | **阻塞一切。** 落地 `MLK_CONFIG_CUSTOM_ALLOC_FREE` + per-hart arena（原型：`tests/pqc/mlkem_width/mlk_vortex_alloc.h`）；对齐 profile/baseline 的 FIPS-202 配置；修 `ABLATE=keccak` | 低 | 是 |
| **E1 ★** | **二维扫描 (warps × lanes/message)** | 1×{1,2,4}×{1,2,4} | 每 warp 一条独立消息，warp 内 W lane 协作 | **本文的头条结果**（§0）。指标 = messages/cycle，**必附 IPC 与 cycles/Keccak-slot 两列**让饱和可见 | 低（launch 形状） | 否（E0 之后） |
| **E3 ★** | **Amdahl 消融三点** | 1×1×1 | `ABLATE=none/keccak/ntt` | 把 70.7–72.3% / 14.20% 从"计数×微基准"升级为直接测量 | **零**（harness 已有，但需先修 D3） | 否 |
| **E12 ★** | **seed 分布扫描** | 1×1×1 或 host | ≥1000 个随机 ρ | 验证 144 众数 (p=0.9275) / 156 (p=0.0627) / E=144.81；**论文主表报 144（+分布）** | 低（host 即可） | 否 |
| **E4 ★** | **共享 Keccak PE（SFU 新 `PE_IDX_*`）+ SimX 模型** | 1×{1,4}×4 | 1 条消息 | 主结果。验证 3.4–3.6× | 中高：RTL + SimX + `model_parity` gate | 是 |
| **E5 ★** | **per-lane vs per-core A/B（同一 SimX 模型两组参数）** | 1×{1,4}×4 | 1 与 4 条消息 | **crux。** 预测端到端差 <1%（24–1000 cycles/perm 全区间），面积差 ~4× | 低（SimX 侧只是参数）+ 中（两次综合） | 否 |
| **E6 ★** | **面积/fmax 扫描** | Vivado，{1,2,4,8} 核 × {4W4T, 4W8T} × {无扩展, per-core PE, per-lane PE} | — | SoK 的 area + Δfmax；time-area product。⚠️ **不需要 T=8/16 的软件臂**（见 E1 的负结果） | 中（综合时间） | 否 |
| **E7 ★** | **强软件基线** | 1×1×1 | 手写 RV32 汇编 Keccak（或 Zbb build） | 预先堵死 "your baseline was weak"；Bolat 的双基线纪律 | 中 | 是（仅基线侧） |
| **E11 ★** | **ML-DSA 四点 × 三参数集** | 1×1×4 | {REDUCE_RAM 上游, +x4 窗口, +x4 padded, eager} × {44, 65, 87} | Pareto 曲线（§3.5c）。报 (peak stack/arena, Keccak slots, 端到端 cycles)；**并在 Vortex 上实测各点的 Keccak 占比** | 中（patch 已就绪、已逐位验证；每点 ≈ 1.4–1.9 亿 cycle） | 是（应用 patch 或做 vendored fork） |
| **E2 ☆** | **4-lane x4 Keccak（纯软件）** | 1×1×4 | 1 条消息 | 已由 §3.3c 完成：**1.496×**（不是投影的 1.60×） | — | **已做** |
| **E8 ☆** | **NTT 蝶形指令 + 4-lane 映射** | 1×1×4 | 1 条消息 | 二阶项，1.17× 上界；验证最后两层 `vx_shfl_bfly` 开销 | 中 | 是 |
| **E9 ☆** | **跨消息批扫到内存墙** | 1×4×{1,2,4}，B ∈ {1,2,4,8,16} | B 条独立 ML-KEM | 找到内存墙的确切位置（预测 arena 构建 N\*=7 @ 8 KB 槽） | 中（E0 之后成本大降） | 否 |
| **E10 ☠️** | ~~rejection 尾部代价~~ | — | — | **杀掉或重新定义。** 没有 ~11% 的开销可测（期望 0.82% 组 / 0.55% perm），且它**对给定 ρ 是确定性的**，不是每次运行的随机量。若保留，正确提法是"`poly_rej_uniform_x4` 的 max-of-4 耦合触发一次要花 4 次 permutation 而非 1 次，即一个 p=0.83% 事件的 4× 放大"，测它需要**扫 ≥1000 个 ρ**（= E12），不是一次 ABLATE | — | — |

**优先级理由**：E0 阻塞一切且成本最低。E1 + E3 + E12 加起来几乎零硬件成本，却决定了整篇论文的分母是否站得住。E5 是唯一有真正体系结构新意的对照——**GPU 文献里没有任何人做过 per-lane vs per-core 加密单元的受控 A/B**（scalar 核没有 lane，wide-SIMD 核没有选择）。

---

## §7 风险与反例

**R1 —— 延迟隐藏吃掉大部分收益。** ⚠️ **本轮已部分实现：实测 4 warp = 3.97×（`genmat_xn`）、2 warp = 2.000×（完整 KEM），一行 ISA 都没改。** 这不是"风险"了，是**已发生的事实**，必须写进论文而不是回避。
*应对*：把论文重述为"在一个已经填满的机器上，ISA 还能拿多少"，所有 speedup 用 **4W 吞吐口径**做分母；Amdahl 上界（3.4–3.6×）不受影响，因为它是比例。

**R2 —— 批模式不可行。** ⚠️ **已被实测部分证伪**：2 warp × 1 lane 的两条独立消息只多 0.008% 时间。真正的天花板是 8 KB/hart 槽下 N\*=7（arena 构建）与 4 个 warp 槽。
*应对*：这不是失败而是**结果**——"在 16 KB D$、L2 关闭的 SIMT 核上，PQC 的 inter-operation 批不但可行，而且是主要收益来源；限制来自每-hart 栈槽容量与 warp 槽数，不是 D$ 带宽"。这与草稿的预期相反，是一个可发表的负结果反转。

**R3 —— 加速 Keccak 后瓶颈迁移到采样。** OpenTitan 实测：hash 从 70% 塌到 8% 后，**sampling 涨到 50–67%**；ML-DSA 签名的 Poly 涨到 52% **[全文，前序核]**。我们的 13.5%（ML-KEM）/ 22.3%（ML-DSA）残差极可能就是 rejection sampling + packing。**早期信号**：E3 的 `ABLATE=both` 之后立刻做一次剖面。
*应对*：**可预期且可写**——Ye et al. 已为此单独出了一条 rejection-sampling 指令。把它作为第三条指令，或诚实地把 7.41× 报成"上界"而非"目标"。

**R4 —— 25-lane Keccak 可能更好，而我们没试。** 已核证据只说 **shared-memory 版**的 25 线程输给 1 线程，且**低并行度时 25 线程反而赢**。Vortex 的 shuffle 是一条 ALU 指令而不是 shared memory 往返，机制前提不同。而 **5（一个 plane）才是自然宽度**。
*应对*：做成一张对照图。**这是文献真正的空白**：没有任何工作在 SIMT 宽度 4 上比较过"4 个独立状态"与"1 个状态拆 4 份"。⚠️ 但按 §0 的饱和曲线，**先验上它不太可能赢**：拆状态会引入 lane 间依赖，而 4 个独立状态已经零通信却只拿到 1.44×。

**R5 —— 共享 PE 在高核数下变成瓶颈。** 仓库自带的负面先例：RTU slot 1→4 慢 28.5%。
*应对*：§4.3 的算术给出 0.04–1.6% 占用率，余量极大——**但那是模型不是测量**。给 PE 加一个占用率计数器（MPM 有 RESERVED class 可用），若批模式下占用率 > 30% 就重新考虑。

**R6 —— Fmax 回退把 cycle 收益吃掉。** V80 目标 300 MHz，core 已在 290–298 MHz；Adams et al. 在 16 核时掉了 7.8%。**早期信号**：E6 的第一次综合。
*应对*：双列报 speedup（可达频率 + 固定频率），并报 time-area product。

**R7 —— ML-DSA 把结论翻过来。** ⚠️ **第二条腿已断（§3.5）**：ML-DSA 的 3.1% 是 `polyvec_lazy.h:427` 的单槽写法，不是算法约束；开窗口 → 55.1%/77.3%，关 REDUCE_RAM → 83.2%（Vortex 实测）。**ML-DSA-44 的 (k,ℓ)=(4,4) 甚至零 padding、ExpandA 100% 可批、2.07×。两个方案在 per-lane 上收益同向。**
*改写后的发现*：不对称不在 ML-KEM vs ML-DSA，而在 **XOF 类型**（§1）——独立流 XOF 占 ML-DSA-65 单消息 slot 的 **93.7%**，天然可批且零 lane 间通信；串行 sponge 占 **6.3%**，单消息内不可批。**这个 93.7 : 6.3 是本报告最可迁移的结论。**
*第一条腿仍在*：ML-DSA 的绝对 Keccak 占比（62.5%）低于 ML-KEM（72.3%），Amdahl 上界 2.67× vs 3.61×。

**R8 —— 反例：operation 足够大时 intra-op 会赢，我们的结论只对小 N 成立。** Liberati et al. 的 plain-LWE KEM（n 到 32,768）一次 encapsulation 就暴露 256×n ≈ 10⁶ 个工作项 **[全文，前序核]**。ML-KEM 的 n=256、k≤4 小四个数量级。**必须在 Related Work 里显式划出这条边界**，否则会被问"为什么不像 Liberati 那样做"。

**R9 —— 方法学地雷（完整清单）**：
(i) `mlkem_microbench` 的 `rej_uniform` 与 Keccak 重复计数，**不可相加** **[本地实测]**；
(ii) instrumented build 比 baseline 慢 8.1%，跨 build 比值必须声明分母 **[本地实测]**；
(iii) profiling 计数器是非原子 file-scope 数组，多线程 build 会竞争（ML-DSA arena 已踩过，peak 读回 0）**[本地实测]**；
(iv) 改 `VX_config.toml` 后必须从 `build/` 重跑 `../configure`，否则 SimX 静默用陈旧的 `VX_config.h` **[本地实测]**；
(v) `VX_MEM_STACK_LOG2_SIZE` 在 `VX_types.toml` 而非 `VX_config.toml`，**不能**用 `CONFIGS=-DVX_CFG_*` 覆盖 **[源码]**；
(vi) **profile build ≠ baseline build（152 vs 156）** **[本轮新测]**；
(vii) **`ABLATE=keccak` 改变调用计数（156→144，补块归零）** **[本轮新测]**；
(viii) **`MLK_PROF_REJ_UNIFORM` = 27 与 x4 组数相同是巧合，不可互读** **[本轮新测]**；
(ix) **`VX_CFG_NUM_THREADS=8/16` 隐含 `VX_CFG_SIMD_WIDTH=8/16`**（`VX_config.toml:56`），即 2×/4× 的 ALU/LSU/RF 宽度 **[源码]**；
(x) 多-lane 端到端臂目前用**冗余 SPMD**（每 lane 跑整条 KEM，只有 Keccak 批不同），工作集乘 W，这就是 W=8 崩掉的原因；`genmat_xn` 的数（每 lane ~1.2 KB）才是无此假象的 **[本轮新测]**；
(xi) **`MLK_PROF_KECCAK_X1` 是总数，`X4` 是组数，串行数 = X1 − 4·X4**（§3.6）**[源码]**；
(xii) **已知污染点**：完整 KEM 的 B=4 臂报 `fail=6`（per-hart arena 尺寸问题未追完），其周期数**不作为结果**；干净的 4-warp 数字来自 `genmat_xn` **[本轮新测]**。

**R10 —— 文献覆盖的两个已知空洞**，见 §8。

**R11 —— 面积论证空转 [未核实]。** §4.3 的推荐目前只由"利用率"与"性能不可区分"两条腿支撑；**第三条腿（面积）缺一个可引用的 Keccak-f1600 硬核基准（LUT/FF/BRAM、cycles/permutation、fmax），本轮未能取得**。在补上之前措辞必须收紧（§4.3 已收紧）。

**R12 —— seed / attempts 单点采样 [本轮新测]。** ML-KEM 的 156 是 p=0.063 的 seed（众数 144，E=144.81）；ML-DSA 的 782 是 attempts=2 的实例（host 侧期望 attempts 5.0708、1399.27 lane-perm）。**草稿所有主数字都建立在这两个乐观/罕见单点上。**
*应对*：ML-KEM 报 144（+分布），ML-DSA 报分布（Riou et al. 的方法学要求）。**一个按 FIPS 203 手数的审稿人会数到 144 + §7.3 校验检查，144 才是能过手检的数。**

---

## §8 已知文献缺口（开放条目，本轮未能填补）

这两块**不粉饰**：它们是本报告开篇论断的潜在反例来源，投稿前必须补检索。

### 8.1 SLH-DSA / SPHINCS+ 的 GPU 实现 —— 完全缺席 **[未核实]**

FIPS 205 在草稿全文一次都没有出现。SPHINCS+ 是本报告论点的**极端点**：

- **~100% Keccak**（与"论文首先必须是一个 Keccak 故事"同向，会强化推荐）；
- 但**消息内并行度极大**（WOTS+ 链、FORS 树、hypertree 层），GPU 上普遍走消息内并行——**这可能是"GPU 文献几乎一致收敛到 block-per-message + grid 做批"这一开篇论断的反例**。

*现状*：§0 与 §2 的收敛性论断**已被显式限定到格基方案**（§2 范围声明）。
*预期影响*：纳入 SPHINCS+ 之后它**很可能反而加强**本报告的推荐（Keccak 引擎优先、独立流批处理优先），并为 per-core 共享 PE 提供第三个用例。但**在检索完成前不能这么写。**

### 8.2 非 NVIDIA GPU（ROCm/HIP、SYCL/oneAPI、Mali/Adreno）的 PQC 工作 —— 未调研 **[未核实]**

全部 60+ 条参考文献里，唯一的非 CUDA 工作是 Liberati et al. 的 OpenACC。这对一篇"SIMT GPGPU 上的 PQC"论文是明显的覆盖缺口，尤其因为 **Vortex 本身就不是 NVIDIA**——最接近我们的先例可能恰好在这一片里。

### 8.3 可引用的 Keccak-f1600 硬核基准 —— 未取得 **[未核实]**（= R11）

需要的最小集合：LUT/FF/BRAM 或 GE、cycles/permutation、fmax，最好来自 FPGA。候选方向（**均未取得，不得直接引用**）：Keccak team 的 high-speed core、Xing & Li TCHES 2021、Beckwith et al. HPEC、Dang et al. 的 round-2 HW/HLS 基准中的 Keccak 子模块。**同时缺 FPGA-vs-GPU 的 PQC 跨平台对比。**

### 8.4 本轮未能复核的具体数字（引用前必须重新取得）**[未核实]**

| 数字 | 出处 | 状态 |
|---|---|---|
| "3090 Ti 上 ExpandA 4,805 µs / NTT 20–23 µs" | Shen et al. cuDilithium | eprint HTTP 429 + 搜索配额耗尽；**且 arXiv v2 与 TPDS 版数字差约 33%，引用必须注明版本** |
| cuPQC "`Block()` 是唯一支持模式"、"BlockDim 只接受 32/64/128/256"、ML-DSA-65 的 BlockDim 与吞吐 | NVIDIA 文档 | 文档页未确认 |
| Antao et al. "3,138 vs 1,413 op/s / 30.3 vs 305.0 ms" | The Computer Journal 55(5) | 正文从未取得（[摘要+二手]）；**本稿已降级，不承担任何 Vortex 结论** |
| Cayrel et al. "25 线程/状态、bank conflict、0.0025–0.2533 GB/s" | ISA 2011 | 正文未取得（[二手]，经 Lee et al. 2018 转述） |
| GRASP "5.5× 吞吐换 9.4× 延迟" | Ning et al. | **未能核实，勿引** |
| "mlkem-native 论文" | — | **不存在**：其 README 不引用任何描述 mlkem-native 本身的论文（只引 SLOTHY, ePrint 2022/1303）。**应替换为「仓库 + commit hash」** |

---

## 参考文献

**标记见文首。「前序核」** = 验证发生在前序会话，本轮因 eprint HTTP 429 与搜索配额耗尽未能复核。

### A. 一手规范与源码（本轮直接核实）

1. NIST, **FIPS 203** *Module-Lattice-Based Key-Encapsulation Mechanism Standard* (final), Aug 2024. https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.203.pdf — **[标准原文]**（本轮核对 §4.1 eq. 4.3–4.5；Alg 13 step 1；Alg 16 step 3；Alg 17 step 1；Alg 18 steps 6–8；**§7.3 item 3 Hash check 及其 "need not be performed … with every execution" 的措辞**；Table 2 ML-KEM-768 参数与 ek/dk/ct 长度）
2. NIST, **FIPS 204** *Module-Lattice-Based Digital Signature Standard* (final), Aug 2024. https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.204.pdf — **[标准原文]**（Table 1 ML-DSA-65 全参数含 **Repetitions = 5.1**；Table 2；Alg 29 SampleInBall；Alg 30/31；**Alg 32 ExpandA = k·ℓ = 30 条独立流**；Alg 33 ExpandS；Alg 34 ExpandMask）
3. pq-code-package, **mlkem-native** @ `1d7b486c`（`v2.0.0-19-g1d7b486c`，`git submodule status` 确认）. https://github.com/pq-code-package/mlkem-native — **[源码]**（本轮逐字核对 `mlkem/src/fips202/fips202.c:54-177` 的 absorb/squeeze 成本模型；`fips202.h` 的 `FIPS202_X4_DEFAULT_IMPLEMENTATION` guard；`poly_k.c:348,364-376`；`kem.c:80,104,423,443`；`sampling.c:127,155,161`；`fips202/keccakf1600.c:179-190,479-488`；`fips202/keccakf1600.h:10-11`；`fips202/native/api.h`；`indcpa.c:246,382,385,434,498,502-503,576,580,586-587,641,644`；`mlkem_native_config.h:476-508,635-653`）
   **注**：其 README **不引用任何描述 mlkem-native 本身的论文**。**若论文中出现「mlkem-native 论文」的引用，该引用无法核实** **[未核实]**
4. pq-code-package, **mldsa-native** @ `19d32614b342840e02010825fa9ff4c22b855653`（`v2.0.0-35-g19d32614`）. https://github.com/pq-code-package/mldsa-native — **[源码]**（`polyvec_lazy.h:427` 的 `mld_poly cur;` 单槽与 `:149-274` 的 REDUCE_RAM guard；`poly.{c,h}`、`fips202/fips202x4.{c,h}`、`symmetric.h` 的 6 条 guard；`fips202/keccakf1600.c:187-199,490`；`sign.c:224` 的 `0xFF /* irrelevant */` 哑流；`MLD_TOTAL_ALLOC_65_*`；README 与 `examples/basic_lowram/README.md`——**注意 README 未提及 lazy re-expansion 的代价**）
5. NVIDIA, **cuPQC SDK 文档**. https://docs.nvidia.com/cuda/cupqc/index.html ; https://docs.nvidia.com/cuda/cupqc/libraries/cupqc_pk/cupqc_pk_usage.html — **[全文，2026-09-05 访问]**（确认 `BlockDim<128>()`、`keygen_kernel<<<batch, BlockDim>>>`、*"each block computes a single public_key and secret_key"*、*"Tunability, options to adjust how many threads perform the operations (BlockDim)"*。三条**未确认**项见 §8.4）
6. Cloudflare CIRCL, `simd/keccakf1600`. https://pkg.go.dev/github.com/cloudflare/circl/simd/keccakf1600 — **[全文，前序核]**（`StateX4` = 四路交错 `[25]uint64`）
7. CRYSTALS-Kyber round-3 spec；NIST pqc-forum "Kyber decisions, part 1: Symmetric crypto". https://groups.google.com/a/list.nist.gov/g/pqc-forum/c/5HveEPBsbxY — **[全文，前序核]**（Schwabe: *"Performance of software implementations of Kyber is currently bottlenecked by Keccak permutations"*）

### B. GPU 上的 PQC 与非对称密码

8. N. Gupta, A. Jati, A. K. Chauhan, A. Chattopadhyay, "PQC Acceleration Using GPUs: FrodoKEM, NewHope, and Kyber," *IEEE TPDS* 32(3):575-586, 2021. DOI 10.1109/TPDS.2020.3025691；源码 https://github.com/nainag/PQC — **[摘要 + 源码全文，前序核]**（首作者是 **Naina** Gupta）
9. T. Ono, S. Bian, T. Sato, "Automatic Parallelism Tuning for MLWE-Based PQ Key Exchanges on GPUs," *ISCAS 2021*. https://eprint.iacr.org/2021/198.pdf — **[全文，前序核]**
10. W.-K. Lee, S. O. Hwang, "High Throughput Implementation of Post-Quantum KEM/D on GPU for IoT," *IEEE TSC* 15(6):3275-3288, 2021. DOI 10.1109/TSC.2021.3103956 — **[摘要，前序核]**（每操作线程数**未能核实**，勿引）
11. L. Wan et al., "A Novel High-Performance Implementation of CRYSTALS-Kyber with AI Accelerator," *ESORICS 2022*, pp. 514-534. https://eprint.iacr.org/2022/881.pdf — **[全文，前序核]**
12. X. Ji, J. Dong, P. Zhang, T. Deng, J. Hua, F. Xiao, "HI-Kyber," *IEEE TPDS* 35(6):722-736, 2024. https://eprint.iacr.org/2023/1194.pdf — **[全文，前序核]**（1,664 kops/s 是 Tesla V100 行；Titan V 最优点 1,358）
13. T. Zhou et al., "ConvKyber," *IACR TCHES* 2024(2). https://eprint.iacr.org/2024/095.pdf — **[全文，前序核]**
14. D. Römer, G. Knoblauch, A. Wiesmaier, "On GPU acceleration of PQC algorithms," ePrint 2025/1596. — **[全文，前序核]**（Dilithium batch 被 65,536 卡死、FrodoKEM 被 35,000 卡死，纯粹因为内存）
15. W.-K. Lee, H. Seo, S. O. Hwang, R. Achar, A. Karmakar, J. M. Bermudo Mera, "DPCrypto," *IEEE TCAS-I* 69(9):3591-3604, 2022. https://eprint.iacr.org/2021/1389.pdf — **[全文，前序核]**（124,418 KX/s 是 DPSaber@RTX3080；点积指令自身消融只有 1.09×）
16. R. Dai, J. Dong, M. Qiu, Z. Dong, F. Xiao, "GOLF," ePrint 2025/749 / *IEEE TIFS* 20:9441-9453, 2025. https://eprint.iacr.org/2025/749.pdf — **[全文，前序核]**
17. W. Wu et al., "Symphony of Speeds: Classic McEliece on GPU," ePrint 2025/748 / *IEEE TIFS* 20:8746-8759, 2025. — **[全文，前序核]**
18. S. Shen, H. Yang, W. Dai, H. Zhang, Z. Liu, Y. Zhao, "High-Throughput GPU Implementation of Dilithium," *IEEE TPDS* 35(11):1964-1976, 2024. https://eprint.iacr.org/2024/1365.pdf ; https://arxiv.org/pdf/2211.12265 — **[全文，前序核]**（**引用必须注明版本**；「3090 Ti ExpandA 4,805 µs」**[未核实]**，见 §8.4）
19. S. Shen, H. Yang, W. Li, Y. Zhao, "cuML-DSA," *IEEE TDSC* 22(3):2295-2307, 2025. https://eprint.iacr.org/2023/1522.pdf — **[全文，前序核]**
20. S. C. Seo, S. An, "Parallel implementation of CRYSTALS-Dilithium for autonomous driving," *ICT Express* 9(1):100-105, 2023. — **[摘要，前序核]**（32 线程/任务系 [18] 转述，非原文）
21. W.-K. Lee, R. K. Zhao, R. Steinfeld, A. Sakzad, S. O. Hwang, "High Throughput Lattice-based Signatures on GPUs: Falcon vs Mitaka," *IEEE TPDS* 35(4):675-692, 2024. — **[全文，前序核]**
22. Y. Gao, J. Xu, H. Wang, "cuNH," *IEEE TPDS* 33(3):551-568, 2022. — **[摘要，前序核]**（98% 延迟 / 86% 吞吐是两个不同负载区间的分别声明，**非同点双赢**）
23. Y. Ning, J. Dong, J. Lin, F. Zheng, Y. Fu, F. Xiao, "GRASP," ePrint 2024/1030 / *IEEE TC* 75(4):1579-1592, 2026. — **[摘要，前序核]**（1.09×–3.45×；「5.5× 吞吐换 9.4× 延迟」**[未核实]，勿引**）
24. J. Dong et al., "HIGH: Harnessing GPU Parallelism for Optimized HQC Performance," ePrint 2026/012. — **[全文，前序核]**
25. T. Liberati et al., "GPU Acceleration of LWE KEMs Using OpenACC," arXiv:2606.01211, 2026. — **[全文，前序核]**（intra-op 批的边界条件；**唯一的非 CUDA 工作**，见 §8.2）
26. Y. Bian, F. Zheng, Y. Wang, L. Lei, Y. Ma, J. Dong, J. Jing, "AsyncGBP," *ICPP 2023*, DOI 10.1145/3605573.3605620；"AsyncGBP+," *IEEE TC*, 2024. — **[摘要，前序核]**
27. Y. Zhou, Q. Wang, "HERO-Sign," arXiv:2512.23969, HPCA 2026. — **[摘要，前序核]**
28. S. Riou, J.-Y. Park, L. Anwar, A. Poschmann, M. Hutter, "Apples, Oranges, and Signatures: Pitfalls and Methodology in ML-DSA Benchmarking," ePrint 2026/1333, MAgiCS 2026 / Springer CCIS. — **[摘要，前序核]**
29. O. Harrison, J. Waldron, "Efficient Acceleration of Asymmetric Cryptography on Graphics Hardware," *AFRICACRYPT 2009*, LNCS 5580:350-367. — **[全文，前序核]**
30. R. Szerwinski, T. Güneysu, "Exploiting the Power of GPUs for Asymmetric Cryptography," *CHES 2008*, LNCS 5154:79-99. — **[全文，前序核]**
31. J. W. Bos, "Low-Latency Elliptic Curve Scalar Multiplication," *IJPP* 40(5):532-550, 2012. — **[全文，前序核]**
32. S. Antao, J.-C. Bajard, L. Sousa, "RNS-Based ECC Point Multiplication for Massive Parallel Architectures," *The Computer Journal* 55(5):629-647, 2012（会议版 *ASAP 2010*）. — **[摘要 + 二手，前序核]** ⚠️ **正文从未取得；草稿据此写的「机器太窄时 fine 两轴全赢」在本稿中已降级，不承担任何 Vortex 结论。**
33. D. J. Bernstein, T.-R. Chen, C.-M. Cheng, T. Lange, B.-Y. Yang, "ECM on Graphics Cards," *EUROCRYPT 2009*, LNCS 5479:483-501. — **[全文，前序核]**（"(parallel)" 行是 2 MAU 协作一条曲线的 fine-grained 配置，吞吐反而 +39–40%）
34. Q. Xiong et al., "gECC," arXiv:2501.03245, 2025. — **[全文，前序核]**

### C. Keccak 与 NTT

35. W.-K. Lee, R. C.-W. Phan, B.-M. Goi, L. Chen, X. Zhang, N. N. Xiong, "Parallel and High Speed Hashing in GPU for Telemedicine Applications," *IEEE Access* 6:37991-38002, 2018. DOI 10.1109/ACCESS.2018.2849439 — **[全文，前序核]**（GTX780+GTX295；**机制是 shared-memory 流量，非 occupancy；排序随并行度反转**）
36. P.-L. Cayrel, G. Hoffmann, M. Schneider, "GPU Implementation of the Keccak Hash Function Family," *ISA 2011*, CCIS 200:33-42. DOI 10.1007/978-3-642-23141-4_4 — **[二手，前序核]** ⚠️ **正文未取得，具体数字引用前必须取得原文。**
37. T. Nguyen Dat, K. Iwai, T. Matsubara, T. Kurokawa, "Implementation of high speed hash function Keccak on GPU," *IJNC* 9(2):370-389, 2019. — **[全文，前序核]**
38. C. Wang, X. Chu, "GPU Accelerated Keccak (SHA3) Algorithm," arXiv:1902.05320, 2019. — **[全文，前序核]**
39. J. Lowden, M. Lukowiak, S. Lopez Alarcon, "Design and performance analysis of efficient Keccak tree hashing on GPU architectures," *J. Computer Security* 23(5):541-562, 2015. — **[摘要，前序核]**
40. J. Zhang, Y. Yan, J. Huang, Ç. K. Koç, "Optimized Software Implementation of Keccak, Kyber, and Dilithium on RV{32,64}IM{B}{V}," *IACR TCHES* 2025(1):632-655. https://eprint.iacr.org/2024/1515 — **[全文，前序核]**（RV64 最优 1,770 cycles/permutation；**RV32 三档基线 15,779 / 7,808 / 6,222 是 E7 的直接依据**）
41. H. Li, N. Mentens, S. Picek, "Maximizing the Potential of Custom RISC-V Vector Extensions for Speeding up SHA-3," *DATE 2023*. https://eprint.iacr.org/2022/868.pdf — **[全文，前序核]**（EleNum 5/15/30 → 延迟恒定、吞吐线性、**面积线性 7,323/24,789/48,180 slices**）
42. H. Li, N. Mentens, S. Picek, "A scalable SIMD RISC-V based processor with customized vector extensions for CRYSTALS-Kyber," *DAC 2022*, pp. 733-738. — **[摘要，前序核]**
43. H. K. Rawat, P. Schaumont, "SIMD Instruction Set Extensions for Keccak," *HASP 2016*. DOI 10.1145/2948618.2948622 — **[幻灯片全文，前序核]**（4,658 GE @ UMC 90nm；5∤2 迫使加 chi2/chi3）
44. N. Zhang, F. Franchetti, "Code Generation for Cryptographic Kernels using Multi-word Modular Arithmetic on GPU," *CGO 2025*, arXiv:2501.07535. — **[全文，前序核]**
45. A. S. Ozcan, E. Savas, "Two Algorithms for Fast GPU Implementation of NTT," ePrint 2023/1410. — **[全文，前序核]**
46. S. Shen et al., "VeloFHE," *IACR TCHES* 2025(3):81-114. DOI 10.46586/tches.v2025.i3.81-114 — **[全文，前序核]**
47. O. Ozerk, C. Elgezen, A. C. Mert, E. Ozturk, E. Savas, "Efficient NTT Implementation on GPU for Homomorphic Encryption," ePrint 2021/124. — **[全文，前序核]**（单/多 kernel 分水岭实测在 n=2¹⁴）
48. S. Kim, W. Jung, J. Park, J. H. Ahn, "Accelerating NTT for Bootstrappable HE on GPUs," *IISWC 2020*, arXiv:2012.01968. — **[全文，前序核]**

> **本类缺口 [未核实]**：**没有任何可引用的 Keccak-f1600 硬核基准**，也**没有 FPGA-vs-GPU 的 PQC 跨平台对比**。这是 §4.3 面积论证的直接证据缺口（R11 / §8.3）。

### D. RISC-V / Vortex / ISE 方法学

49. A. Adams, P. Gupta, B. Tine, H. Kim, "Cryptography Acceleration in a RISC-V GPGPU," *CARRV 2021*. https://carrv.github.io/2021/papers/CARRV2021_paper_87_Adams.pdf — **[全文，前序核]**（**唯一的 GPU 上加密 ISE，且就在 Vortex 上**；per-thread 复制 AES S-box；16 核 ALM 80.24%→85.78%、fmax 192→177 MHz；Table 3 的双列 speedup）
50. B. Tine, K. P. Yalamarthy, F. Elsabbagh, H. Kim, "Vortex: Extending the RISC-V ISA for GPGPU and 3D-Graphics," *MICRO-54*, 2021. https://arxiv.org/pdf/2110.10857 — **[全文，前序核]**（配置扫描 + 每点 LUT/FF/BRAM/fmax 的体裁模板）
51. W. Matsumi, R.-U.-H. Mian, "Accelerating HDC-CNN Hybrid Models Using Custom Instructions on RISC-V GPUs," arXiv:2511.05053, 2025. — **[全文，前序核]**（反面教材：simx-only，56.191× 微基准 → 1.024× 应用）
52. T. Fritzmann, G. Sigl, J. Sepúlveda, "RISQ-V," *IACR TCHES* 2020(4):239-280. — **[摘要，前序核]**（细粒度消融表未取得正文，**勿引具体行**）
53. K. Miteloudi, J. Bos, O. Bronchain, B. Fay, J. Renes, "PQ.V.ALU.E," *CARDIS 2023*, LNCS 14530:190-209. https://eprint.iacr.org/2023/1505.pdf — **[全文，前序核]**（自陈是 NTT cycle *"reduce by more than 80%"*，**不是** 7.74×/4.12×）
54. H. Cheng, J. Großschädl, B. Marshall, D. Page, M.-J. O. Saarinen, "SoK: Instruction Set Extensions for Cryptographers," ePrint 2024/1323. — **[全文，前序核]**（全篇唯一的 GPU ISE 是 [49]；Pitfall #2 面积双尺度；Concept 18 的 kernel/system 两档）
55. A. Abdulrahman, F. Oberhänsl, H. N. H. Pham, J. Philipoom, P. Schwabe, T. Stelzer, A. Zankl, "Towards ML-KEM & ML-DSA on OpenTitan," *IEEE S&P 2025*. https://eprint.iacr.org/2024/1192.pdf — **[全文，前序核]**（瓶颈迁移的最佳模板；"<17% OTBN / <3% Earl Grey"）
56. E. Alkım, H. Evkan, N. Lahr, R. Niederhagen, R. Petri, "ISA Extensions for Finite Field Arithmetic," *IACR TCHES* 2020(3):219-242. — **[全文，前序核]**（六指标清单）
57. A. Bolat, S. Sezer, K. McLaughlin, H. Hui, "Microarchitecture Design and Benchmarking of Custom SHA-3 Instruction for RISC-V," arXiv:2508.20653, 2025. — **[全文，前序核]**（双基线纪律：8.02× vs 46.31×）
58. Z. Ye, R. Song, H. Zhang, D. Chen, R. C. C. Cheung, K. Huang, "A Highly-efficient Lattice-based PQC Processor for IoT," *IACR TCHES* 2024(2):130-153. — **[架构已核（5×64-bit SIMD = 一个 Keccak plane、14 指令/轮、rejection-sampling 指令），数值未核，前序]**
59. A. Dolmeta, V. Piscopo, G. Masera, M. Martina, M. Hutter, "HORCRUX," ePrint 2025/1934. — **[全文，前序核]**

### E. 系统与部署侧背景

60. G. Gómez-Cambronero, D. Munteanu, A. I. González-Tablas, "Layered Performance Analysis of TLS 1.3 Handshakes," arXiv:2603.11006v2, 2026. — **[全文，前序核]**
61. Cloudflare, "State of the post-quantum Internet in 2025." https://blog.cloudflare.com/pq-2025/ — **[全文，前序核]**（>50% 人类流量）
62. V. Rathi et al., "QORE: Quantum Secure 5G/B5G Core," arXiv:2510.19982, 2025. — **[全文，前序核]**
63. N. Gupta, H. Alimohammadi, M. Shojafar, D. Mi, M. N. M. Bhutta, "Energy-Aware Cryptographic Scheduling in Open RAN," arXiv:2602.11820v1, 2026. — **[全文，前序核]**

### F. 本地测量与产物

64. **本仓库既有测量与源码** — **[本地实测]** / **[源码]**：
   `tests/pqc/{mlkem,mlkem_microbench,mlkem_profile,mldsa,mldsa_microbench,mldsa_profile}`（`tests/pqc/Makefile:5-7` 的 `TESTS` 列表）；`tests/pqc/common.mk` → `tests/regression/common.mk`（`run-simx` / `run-rtlsim` / `run-aved`、`CONFIGS` 与 `OPTS` 处理）；`pqc/third_party/{mlkem-native,mldsa-native}`；`hw/rtl/core/{VX_core,VX_alu_int,VX_sfu_unit,VX_lane_dispatch}.sv`；`VX_config.toml`、`VX_types.toml`、`sw/kernel/src/vx_start.S`、`sw/kernel/include/vx_intrinsics.h`；`build32/tests/pqc/*/kernel.dump`；`ci/baselines/synthesis/xilinx/*.json`；`docs/designs/{microarchitecture,custom_accelerator_isa_extensions}.md`
65. **本轮新增的实现与测量** — **[本轮新测]**：
   - `/home/jiangbowang/aphdcode/vortex_v80/vortexPQC/tests/pqc/mlkem_width/` — 完整 ML-KEM-768，x4 Keccak 批摊到 W 条 lane（`mlk_simt_fips202.h` SIMT 后端、`mlk_vortex_alloc.h` per-hart bump arena、`kernel.cpp` 0xA5 涂色低水位扫描、`vortex_width_config.h`、`main.cpp -t lanes -b blocks`）。**submodule 未修改。**
   - `/home/jiangbowang/aphdcode/vortex_v80/vortexPQC/tests/pqc/genmat_xn/` — 真 xN（N ≤ `VX_CFG_NUM_THREADS`）gen_matrix + PRF 批，lane 私有 XOF 流；10 个宽度 × 所有块数校验和逐位一致（`817bf6c4` / `104bd412`）。
   - **两个目录均未注册进 `tests/pqc/Makefile:5-7` 的 `TESTS`**；构建方式是把各自的 `Makefile` 拷进 `build32/tests/pqc/<name>/`。两个 submodule 工作区仍 clean；SimX 与 runtime 已在扫描后恢复默认配置重建。
   - ML-DSA patch：`…/scratchpad/expand_entry_x4.patch`（6 文件 +148/−59）与 `…/scratchpad/expand_entry_x4_padded_tail.patch`（+12 行）；三棵插桩源码树 `…/scratchpad/{mldsa-base,mldsa-x4,mldsa-x4pad}`。正确性：50 次 keygen+sign+verify 的 pk/sk/sig FNV 哈希与未改动 build 逐位相同。
   - ML-KEM 逐调用点归属 harness：`…/scratchpad/{harness.c,harness_q.c,mlkem-native/,cfg_prof.h,cfg_base.h,mlk_site.h,fips203.txt}`。宿主端确定性复现 `156 / 27 / 15 fwd / 9 inv`。
   - **ML-DSA REDUCE_RAM 开/关的 Vortex 对照**（§3.5c）：注释掉 `tests/pqc/mldsa/vortex_mldsa_config.h:27` 的 `#define MLD_CONFIG_REDUCE_RAM` 后重跑 `tests/pqc/mldsa_profile`，读 `keccak_f1600_x1` / `keccak_f1600_x4` / `ARENA: peak=` 三行与三阶段 cycles。
   - **已知污染点**：完整 KEM 的 B=4 臂报 `fail=6`，其周期数**不作为结果**；干净的 4-warp 数字来自 `genmat_xn`。多-lane 端到端臂使用冗余 SPMD（R9-x）。

---

## 一句话结论

**在 Vortex 上做 PQC ISA 扩展，正确的主线是：先修基线的三个缺陷（per-hart arena 修 8 KB 栈越界 + profile/baseline 构建对齐 + `ABLATE=keccak` 计数漂移），再把 Keccak-f1600 做成 per-core 共享的 SFU PE，并把并行度拆成两个正交且优先级明确的轴 —— warp 级跨消息批做吞吐（实测 4 warp 3.97×，第二条消息只多花 0.008% 的时间），4-lane 消息内 Keccak 批做延迟（实测 1.436–1.496×，过 4 之后边际回报 ≤10% 且用真 x4 API 在 W=8 转负 46%），最好的实测组合点是 4 warp × 2 lane = 5.054×。** per-lane 复制不值得：端到端差异在任何合理的硬件 permutation 延迟（24–1000 cycles/perm）下都 < 1%，而面积代价约 4× 且随 `VX_CFG_SIMD_WIDTH` 线性增长；**但这个推荐目前只由「利用率」与「性能不可区分」两条腿支撑，面积那条腿缺一个可引用的 Keccak-f1600 硬核锚点。** Keccak 占 ML-KEM-768 往返 **70.7–72.3%**（区间源于 152/156 的构建分歧与 144/156 的 seed 分布），Amdahl 上界 **3.41–3.61×**，NTT 只有 1.17× —— **论文首先必须是一个 Keccak 故事，而且是一个「独立流 XOF 占 93.7%、串行 sponge 只占 6.3%」的故事**，这个划分对 ML-KEM 与 ML-DSA 同时成立。ML-DSA 的 3.1% 批宽不是内存约束的宿命，而是 `polyvec_lazy.h:427` 一个单槽字段：开 4 宽窗口 +6.6 KB 换到 55–77% 可批，直接关掉 `MLD_CONFIG_REDUCE_RAM` 则在 Vortex 上实测 83.2% 可批且快 25.2%，代价 65 KB —— **这是一条 Pareto 曲线，不是二选一。**

---

## 下一步

**1. 修掉三个让所有分母失效的缺陷（E0，成本最低、阻塞一切）。**
落地 `MLK_CONFIG_CUSTOM_ALLOC_FREE` + per-hart bump arena（原型已在 `tests/pqc/mlkem_width/mlk_vortex_alloc.h`），用 0xA5 涂色校验峰值栈从 18,040 B 降到 4,632 B、确认不再越界 8,192 B 槽；给 `tests/pqc/mlkem/vortex_mlkem_config.h` 与 `tests/pqc/mlkem_profile/` 对齐 FIPS-202 配置，消掉 152 vs 156 的构建分歧；修 `ABLATE=keccak`（stub 后未置换的状态使 rejection 恒收满，补块轮数归零、计数 156→144），使 E3 的消融 delta 干净。
**验收标准**：三个 build 的 permutation 计数表可复现且一致；`T=2` 的完整 KEM `fail=0` 且不再出现 `Error: misaligned memory access`。

**2. 跑二维扫描 + 两个分布扫描，把所有单点数字换成分布（E1/E3/E9/E12/R12）。**
二维 = (warps × lanes-per-message) ∈ {1,2,4}×{1,2,4}，指标 messages/cycle，**必附 IPC 与 cycles/Keccak-slot 两列**让饱和可见；同时对 ≥1000 个随机 ρ 扫 ML-KEM 的 permutation 计数分布（验证 144 众数 p=0.9275 / 156 p=0.0627 / E=144.81），并用 `ABLATE` 把 70.7–72.3% / 14.20% 从推导值升级为直接测量。
**验收标准**：论文主表报 **144（+分布）**而非 156；完整 KEM 的 4-warp 点 `fail=0`；`ABLATE=both` 之后立刻做一次残差剖面（回答 R3 的瓶颈迁移）。

**3. 落 ML-DSA 的 Pareto 四点 + 补三个证据缺口（E11/E6/R10/R11/§8）。**
把已就绪、已逐位验证的 `expand_entry_x4.patch` 应用到 `pqc/third_party/mldsa-native`（或做成 vendored fork 以保持 submodule pristine），跑 {上游, x4 窗口, x4 padded, eager} × {ML-DSA-44, -65, -87} 的十二点扫描，报 (peak stack / `ARENA: peak`, Keccak slots, 端到端 cycles)，并**在 Vortex 上实测每个点的 Keccak 占比**（已知两个端点：3.1% 批 / 62.5% Keccak / 189.8M cycles，与 83.2% 批 / 62.2% / 142.0M cycles）；同时用一次带搜索配额的检索补齐三个缺口——**(a) 至少一个可引用的 Keccak-f1600 硬核基准（LUT/GE + cycles/permutation + fmax），(b) SLH-DSA/SPHINCS+ 的 GPU 实现，(c) 非 NVIDIA GPU（ROCm/HIP、SYCL）的 PQC 工作**。
**验收标准**：ML-DSA-44 的零 padding、ExpandA 100% 可批、2.07× 在 Vortex 上可复现；§4.3 的面积论证不再空转；§2 的「文献收敛」论断能从「限定到格基方案」升级为有覆盖依据的判断，或被 SPHINCS+ 明确修正。
