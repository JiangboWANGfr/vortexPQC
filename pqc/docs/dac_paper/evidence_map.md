# 数据与论断对应

当前正文按 Introduction、Background and Motivation、Design、Implementation and Evaluation、Conclusion 组织，共 8 幅图、2 张表，引用 14 条文献。13 份 `data/*.csv` 均从仓库 `pqc/results/` 原样复制，校验值见 `assets/source_manifest.json`。`generate_results.py` 从这些快照生成数值、表格和 PGFPlots 数据；图形由 TikZ/PGFPlots 编译。

## 实验论断与数据

| 正文对象 | 数据文件 | 采用行与计量口径 | 比较边界 |
| --- | --- | --- | --- |
| 摘要、II-B 动机、IV-B、图 5 `fig:kem` | `keccak_ntt_unified_xrt.csv` | 六后端 × M1/M8，`device_cycles`；同一 `xrtsim_sha256` | 一个全启用 RV32IM XRT 构建；NTT 和算术已固定加速。Stage looped，Round expanded。24.67→7.91 Mcycles 是完整请求中的 Keccak 后端替换，不是置换微基准 |
| IV-C、图 6 `fig:ablation` | `keccak_sg25_stages.csv` | `driver=rtlsim`，8 个 T/R/C arm，1/8 active warps；`[span(P8)-span(P4)]/(4*W)`，以各自 `000` arm 归一化 | 较早的 Stage-enabled W8T32 处理器 RTL 实验，所有 arm 启用同一套硬件，只改变指令使用。不是 XRT 主实验、P64 绝对区间成本或物理面积消融 |
| IV-D、图 7(a)/(b) 原语曲线 `fig:fusion` | `keccak_w32_controls.csv` | `driver=xrt`，P64、S1、1/8 active warps；`span_cycles/(64*batch)` | Stage/Round-enabled runtime；不是六后端主图的同一 runtime，也不是 packed-state 峰值吞吐。Expanded Stage/Round 比值为 3.168×/3.291× |
| IV-D、图 7(b) 完整 KEM 曲线 | `mlkem_w32_controls.csv` | SimX Stage expanded / Round expanded，`device_cycles` | 独立、匹配的 SimX 控制，不能标成 XRT；M1/M8 收益为 1.028×/1.033×。图 7(b) 的 1/8 对原语表示 active warps，对 KEM 表示并发请求数 |
| IV-D、图 8 `fig:phases` | `mlkem_phase_profile.csv` | `measurement=phase,driver=xrt,requests=1`；阶段周期/互斥 request interval | 三个独立启用后端；absorb/squeeze 排除嵌套 permutation。Other 含探针开销。6.6–8.3% 开销来自 matched SimX 控制；不可据此精确预测未插桩加速比 |
| IV-E、NTT SG2 通信收益 | `nttbf_sg2_validation.csv` | 同 6-cycle full-bank 的 SHFL+NTTMUL 与 SG2+NTTMUL；SimX KEM interval | 不用 legacy 4-cycle 行分解指令收益；M1/M8 降低 2.56%/8.23% |
| IV-E、较强 shared-memory NTT 对照 | `nttbf_sg2_validation.csv` | XRT `smem32+NTTMUL` 与 full-bank SG2；KEM interval 为 7.792/11.049 与 7.711/10.523 Mcycles | 较早的 pointer-enabled、含 FPU 配置；不是最终主图的 whole-launch 数值，也不是单独 NTT 原语周期 |
| IV-E、half-bank 实现节省 | `ntt_v80_ppa.csv` | reducepipe / halfbank：356447→341680 LUT，272430→270865 FF，192→176 DSP | 历史配置含 FPU；两行配置匹配且均闭合 250 MHz，仅作家族内部比较 |
| IV-E、half-bank 周期保持 | `ntt_halfbank_validation.csv` | 同 variant、backend、请求数和 ELF，不同 bank；`measured_cycles` | 各匹配检查最大绝对变化约 0.041%；不宣称 half-bank 带来应用性能飞跃 |
| IV-F、表 II `tab:ppa` | `rv32im_rv64im_keccak_ppa.csv` | xlen32、NTT-only/Stage/Round；独立 post-route | 严格 RV32IM、F/D 关闭。无匹配 IM Pointer 行；不能与旧含 F 的 Pointer 混算面积效率，也不能将全启用性能构建视为这些独立面积构建的板测吞吐 |
| IV-F、RV64 指令数和 M1 周期 | `rv32im_rv64im_keccak.csv` | 匹配 backend/request 的 retired instructions；M1 周期采用 XRT 行 | Keccak collective issues 减半不等于完整应用 instructions 减半；两个 XLEN 均为 IM |
| IV-F、RV64 M8 周期 | `rv32im_rv64im_keccak_xrt_m8.csv` | 四组 XRT `device_cycles` | 两种 XLEN 的 F/D 均关闭；不是直接板上时间 |
| IV-F、RV64 面积与时序 | `rv32im_rv64im_keccak_ppa.csv` | xlen64 相应独立后端 | 单次物理实现；Stage/Round 在 250 MHz 目标下分别 WNS -0.148/-0.013 ns，不是多 seed 稳定 Fmax |
| NTT 软件分母的归档背景 | `ntt_w32_e2e.csv` | direct shuffle 与 shared-transpose 软件 | 支持选择较强软件分母；不额外绘制为主图 |
| P8 微基准归档补充 | `keccak_w32_matched_xrt.csv` | 全后端核心、一 warp 一 state、8 连续 permutations | 正文 P64 控制的 3.291× 不替换为该 P8 实验的 3.251× |

表 I `tab:setup` 描述最终主实验配置；历史 stage-use 和 NTT 选择实验在正文对应小节单独限定。所有数据行保留原始状态，没有调整 model-parity 阈值、修改 golden baseline 或重跑硬件。

## 机制图与实现

| 图 | 展示内容 | 关键限制 |
| --- | --- | --- |
| 图 1 `fig:keccak` | Keccak 列归约、固定旋转/置换、行邻居；lane=`x+5y` | RHOPI 示例 lane 1→lane 10；图表示连接关系，不是整核时序 |
| 图 2 `fig:core` | 整核寄存器接口，以及 Stage θ 两级、Round 四级流水 | 寄存器竖线对应流水存储；latency/II 是直接单元属性，不是 warp 所见的完整依赖延迟。固定路由在新单元中实现，未声称物理复用通用 shuffle 网络 |
| 图 3 `fig:halves` | RV32 Stage 六次与 Round 两次 issue 的源/结果依赖 | 不表示同时发射。THETA/RHOPI 的 L/H 读取同一旧状态；CHII 分别读取对应半字和 uniform round |
| 图 4 `fig:ntt` | `a[l+32k]` 布局，distance 32 寄存器伙伴、distance 16 的 lane 5/21 伙伴，以及 CT 数据流 | pair-low 提供 twiddle；一个 Montgomery product 同时服务两路结果。图中 16 pairs/16 multipliers 是硬件结构，GS/NTTMUL 的两相调度在正文说明 |

实现依据：

- `hw/rtl/pqc/VX_pqc_nttmul.sv`：half-bank、GS 的 sum×20159 第二产品、有限位宽行为。
- `hw/rtl/core/VX_alu_ksg25.sv`：Stage 两级流水、full mask、uniform round、padding lanes。
- `hw/rtl/core/VX_alu_kround25.sv`：Round 四级流水、L/H 选择、literal round。
- `sw/kernel/include/pqc/vx_ntt.h`、`vx_ksg25.h`、`vx_kround25.h`：软件接口。
- `docs/proposals/ntt_acceleration_proposal.md`、`keccak_sg25_proposal.md`、`ntt_keccak_integration_proposal.md`：完整实验命令和历史来源。
