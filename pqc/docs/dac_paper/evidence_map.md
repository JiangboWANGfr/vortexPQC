# 数据与论断对应

当前正文按 Introduction、Background and Motivation、Design、Implementation and Evaluation、Conclusion 组织，共 9 幅图、3 张表，引用 15 条文献。`data/*.csv` 均从仓库 `pqc/results/` 原样复制，校验值见 `assets/source_manifest.json`。`generate_results.py` 从这些快照生成数值、表格和 PGFPlots 数据；图形由 TikZ/PGFPlots 编译。

## 实验论断与数据

| 正文对象 | 数据文件 | 采用行与计量口径 | 比较边界 |
| --- | --- | --- | --- |
| 摘要、II-B 动机、IV-B、图 5 `fig:kem` | `keccak_ntt_unified_xrt.csv` | 六后端 × M1/M8，`device_cycles`；同一 `xrtsim_sha256` | 一个全启用 RV32IM XRT 构建；NTT 和算术已固定加速。Stage looped，Round expanded。24.67→7.91 Mcycles 是完整请求中的 Keccak 后端替换，不是置换微基准 |
| IV-C、图 6 `fig:ablation` | `keccak_sg25_stages.csv` | `driver=rtlsim`，8 个 T/R/C arm，1/8 active warps；`[span(P8)-span(P4)]/(4*W)`，以各自 `000` arm 归一化 | 较早的 Stage-enabled W8T32 处理器 RTL 实验，所有 arm 启用同一套硬件，只改变指令使用。不是 XRT 主实验、P64 绝对区间成本或物理面积消融 |
| IV-D、图 7(a)/(b) 原语曲线 `fig:fusion` | `keccak_w32_controls.csv` | `driver=xrt`，P64、S1、1/8 active warps；`span_cycles/(64*batch)` | Stage/Round-enabled runtime；不是六后端主图的同一 runtime，也不是 packed-state 峰值吞吐。Expanded Stage/Round 比值为 3.168×/3.291× |
| IV-D、图 7(b) 完整 KEM 曲线 | `mlkem_w32_controls.csv` | SimX Stage expanded / Round expanded，`device_cycles` | 独立、匹配的 SimX 控制，不能标成 XRT；M1/M8 收益为 1.028×/1.033×。图 7(b) 的 1/8 对原语表示 active warps，对 KEM 表示并发请求数 |
| IV-D、剩余开销文字 | `mlkem_phase_profile.csv` | `measurement=phase,driver=xrt,requests=1`；阶段周期/互斥 request interval | 三个独立启用后端；absorb/squeeze 排除嵌套 permutation。Other 含探针开销。6.6–8.3% 开销来自 matched SimX 控制；不可据此精确预测未插桩加速比 |
| IV-E、图 8 `fig:sharedntt` | `shared_ntt_xrt.csv` | 同一 Pointer Keccak、同一剩余算术下，各算法 W32 软件 NTT 对共享 K/D 指令，`keypair` / 第二阶段 / 第三阶段 / total 的 XRT 周期差 | `total` 是三个设备阶段区间之和，不是图 5 的完整 launch 周期。两种方案各自比较相同应用和同一 XRT runtime；并非旧 K-only 六后端数据或共享单元面积。Stage+共享 NTT 另由 SimX 端到端 KAT 验证，不进入图中 |
| IV-E、图 8 的 D PW1 | `mldsa_pointwise.csv` | 完整矩阵 ML-DSA-65，C/C 与 `NTTMUL.D`+W32 L5 对照；M1 使用匹配 XRT 的三阶段 `total_cycles`，减少 12.538% | 固定 Pointer Keccak 与共享 K/D NTT，通过字节级校验。L5 是保留单次 Montgomery 约减的软件 lane 并行，没有新增 RTL；不叠加到 NTT-only 收益 |
| 摘要、IV-E、图 8 的 D PW8 | `mldsa_pointwise_m8.csv` | 两种 pointwise 映射 × SimX/XRT × 8 个输入；正文采用 XRT `makespan_cycles`：77,071,268→68,063,840，减少 11.687% | 输入 ID 1–8，full-RAM ML-DSA-65，2–11 次签名尝试由 L5 调用数推算；32 次完整请求通过，逐输入 primitive counts 一致，跨模型 instructions 完全一致，launch/makespan 最大差 0.593%。M8 新编译的 C/C 与旧 M1 的 C/C 二进制不同，各自仅与匹配对照比较；不构成同输入 M1→M8 缩放曲线 |
| IV-E、表 II `tab:finalphases` | `final_phase_profile.csv` | `driver=xrt`，KEM roundtrip、DSA keypair/sign/verify 求和；阶段周期除以 instrumented `request_total`，由原始周期求和后统一舍入 | 固定 Pointer Keccak、共享 K/D NTT、最终算术映射。Products=KEM mulcache+basemul 或 DSA ordinary+L5 pointwise；Sponge=absorb+squeeze；KEM Rest=residual+reduce；DSA absorb/squeeze 未隔离，仍在 Rest。探针开销来自同构建匹配未插桩请求，KEM/DSA 为 6.398%/0.666%；不据此认定 DSA 剩余瓶颈或预测八路乘法器的吞吐 |
| IV-F、NTT SG2 通信收益 | `nttbf_sg2_validation.csv` | 同 6-cycle full-bank 的 SHFL+NTTMUL 与 SG2+NTTMUL；SimX KEM interval | 不用 legacy 4-cycle 行分解指令收益；M1/M8 降低 2.56%/8.23% |
| IV-F、较强 shared-memory NTT 对照 | `nttbf_sg2_validation.csv` | XRT `smem32+NTTMUL` 与 full-bank SG2；KEM interval 为 7.792/11.049 与 7.711/10.523 Mcycles | 较早的 pointer-enabled、含 FPU 配置；不是最终主图的 whole-launch 数值，也不是单独 NTT 原语周期 |
| IV-F、half-bank 实现节省 | `ntt_v80_ppa.csv` | reducepipe / halfbank：356447→341680 LUT，272430→270865 FF，192→176 DSP | 历史配置含 FPU；两行配置匹配且均闭合 250 MHz，仅作家族内部比较 |
| IV-F、half-bank 周期保持 | `ntt_halfbank_validation.csv` | 同 variant、backend、请求数和 ELF，不同 bank；`measured_cycles` | 各匹配检查最大绝对变化约 0.041%；不宣称 half-bank 带来应用性能飞跃 |
| 摘要、IV-F、图 9(a) `fig:nttbank` 性能 | `ntt_multiplier_bank_nttbf.csv` | M16/8/4/2/1 相同 NTTBF.K、59,534 instructions；RTL cycles 567,302/568,327/571,357/584,110/709,372 | 独立 processor RTL parity case；所有配置通过 1,044 vectors，SimX/RTL 最大差 0.959%。这是独立 NTTBF kernel，不是完整 KEM/DSA 请求 |
| IV-F、图 9(b) `fig:nttbank` 资源 | `ntt_multiplier_bank_ppa.csv` | 五组 RV32IM、W8T32、F/D off、Vivado 2025.1、250 MHz、OPT3 独立 post-route；正文采用 NTT hierarchy LUT/FF/DSP | 同一 serializer RTL，仅 bank 参数变化；均为 133 BRAM、零 routing error、正 WNS。不是 Stage/Keccak 合并 PPA，也不使用 vectorless power |
| 摘要、IV-F、M2 完整请求保持 | `ntt_multiplier_bank_performance.csv` | scheme={ML-KEM,ML-DSA} × requests={1,8} × driver={SimX,XRT}；M2 相对 M16 的 XRT `makespan_cycles` 最大增加 0.143% | 所有 40 行 KAT PASS、instructions/calls 对应一致；微小负变化视为调度/模型波动，不宣称更少乘法器加速计算 |
| IV-G、表 III `tab:ppa` 的 K-only 三行 | `rv32im_rv64im_keccak_ppa.csv` | xlen32、NTT-only/Stage/Round；独立 post-route | 严格 RV32IM、F/D 关闭。无匹配 IM Pointer 行；不能与旧含 F 的 Pointer 混算面积效率 |
| IV-G、表 III `tab:ppa` 的共享 K/D 两行 | `shared_ntt_ppa.csv` | shared NTT-only 与 shared NTT+Stage；独立 Vivado 2025.1 post-route，W8T32、250 MHz、F/D 关闭 | 两组均为整核实现，133 BRAM tiles、176 DSP、零 routing error、WNS +0.018 ns；同一半宽 32-bit 乘法器组，不是两套 K/D 模乘器。全启用 XRT 性能构建与独立面积构建不能组成板测吞吐/面积比 |
| IV-G、RV64 指令数和 M1 周期 | `rv32im_rv64im_keccak.csv` | 匹配 backend/request 的 retired instructions；M1 周期采用 XRT 行 | Keccak collective issues 减半不等于完整应用 instructions 减半；两个 XLEN 均为 IM |
| IV-G、RV64 M8 周期 | `rv32im_rv64im_keccak_xrt_m8.csv` | 四组 XRT `device_cycles` | 两种 XLEN 的 F/D 均关闭；不是直接板上时间 |
| IV-G、RV64 面积与时序 | `rv32im_rv64im_keccak_ppa.csv` | xlen64 相应独立后端 | 单次物理实现；Stage/Round 在 250 MHz 目标下分别 WNS -0.148/-0.013 ns，不是多 seed 稳定 Fmax |
| NTT 软件分母的归档背景 | `ntt_w32_e2e.csv` | direct shuffle 与 shared-transpose 软件 | 支持选择较强软件分母；不额外绘制为主图 |
| P8 微基准归档补充 | `keccak_w32_matched_xrt.csv` | 全后端核心、一 warp 一 state、8 连续 permutations | 正文 P64 控制的 3.291× 不替换为该 P8 实验的 3.251× |

表 I `tab:setup` 描述主实验配置；历史 stage-use、NTT 选择和 K-only PPA 在正文对应小节单独限定。共享 K/D 的两组 Vivado 结果另有原始汇总、利用率、时序、布线报表和哈希；首次未闭合的实现不进入表 III。所有数据行保留原始状态，没有调整 model-parity 阈值或修改 golden baseline。

## 机制图与实现

| 图 | 展示内容 | 关键限制 |
| --- | --- | --- |
| 图 1 `fig:keccak` | Keccak 列归约、固定旋转/置换、行邻居；lane=`x+5y` | RHOPI 示例 lane 1→lane 10；图表示连接关系，不是整核时序 |
| 图 2 `fig:core` | 整核寄存器接口，以及 Stage θ 两级、Round 四级流水 | 寄存器竖线对应流水存储；latency/II 是直接单元属性，不是 warp 所见的完整依赖延迟。固定路由在新单元中实现，未声称物理复用通用 shuffle 网络 |
| 图 3 `fig:halves` | RV32 Stage 六次与 Round 两次 issue 的源/结果依赖 | 不表示同时发射。THETA/RHOPI 的 L/H 读取同一旧状态；CHII 分别读取对应半字和 uniform round |
| 图 4 `fig:ntt` | `a[l+32k]` 布局，distance 32 寄存器伙伴、distance 16 的 lane 5/21 伙伴，以及 CT 数据流 | pair-low 提供 twiddle；一个 Montgomery product 同时服务两路结果。图中 $M$ 个 32×32 multipliers 是可序列化的共享 K/D bank，beat 数与 latency/II 在正文说明 |
| 图 9 `fig:nttbank` | M16/8/4/2/1 性能—DSP Pareto，以及 NTT hierarchy LUT/FF/DSP 归一化曲线 | 金色 M2 是已测 knee；M1 的 FF 回升来自更深的 serializer。图不表示完整 Keccak+NTT 核面积或板上吞吐 |

实现依据：

- `hw/rtl/pqc/VX_pqc_nttmul.sv`：K/D 共用 half-bank 与配对网络、两种 Montgomery 约减、K GS 的 sum×20159 第二产品。
- `hw/rtl/core/VX_alu_ksg25.sv`：Stage 两级流水、full mask、uniform round、padding lanes。
- `hw/rtl/core/VX_alu_kround25.sv`：Round 四级流水、L/H 选择、literal round。
- `sw/kernel/include/pqc/vx_ntt.h`、`vx_ksg25.h`、`vx_kround25.h`：软件接口。
- `tests/pqc/mldsa_profile/mld_prof_arith.h` 与 `mld_pointwise_w32.h`：普通 pointwise 复用 `NTTMUL.D`，L5 保留五项原始乘积之和后单次约减。
- `docs/proposals/ntt_acceleration_proposal.md`、`keccak_sg25_proposal.md`、`ntt_keccak_integration_proposal.md`：完整实验命令和历史来源。
