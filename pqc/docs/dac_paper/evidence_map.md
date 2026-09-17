# 数据与论断对应

所有 `data/` 文件均从仓库 `pqc/results/` 原样复制；校验值见 `assets/source_manifest.json`。
全文刻意保留实验族边界，避免把旧数据、新核心、SimX 与 XRT 混成同一组。

| 正文对象 | 数据文件 | 采用行 / 数值口径 | 限制 |
| --- | --- | --- | --- |
| 摘要、图 5、IV-B 完整 KEM | `keccak_ntt_unified_xrt.csv` | 六后端 × M1/M8，`device_cycles`，同一 `xrtsim_sha256` | NTT/算术均固定且已加速；Stage loop、Round expanded |
| 图 6 同展开原语 | `keccak_w32_controls.csv` | `driver=xrt`，P64，S1，batch 1/8；`span_cycles/(64*batch)` | Stage/Round-enabled runtime；不是全后端主图同一 runtime，也不是 packed peak throughput |
| 3% 完整 KEM 融合收益 | `mlkem_w32_controls.csv` | SimX Stage expanded vs Round expanded，`device_cycles` | 明确标 SimX，不能冒充统一 XRT；M1/M8 为 1.028×/1.033× |
| 图 7 profile | `mlkem_phase_profile.csv` | `measurement=phase,driver=xrt,requests=1`；阶段周期/互斥 interval | 三个独立启用的后端；6.6–8.3% overhead 来自 matched SimX 控制 |
| NTT SG2 通信收益 | `nttbf_sg2_validation.csv` | 同 6-cycle full-bank，SHFL+NTTMUL vs SG2+NTTMUL；SimX KEM interval | 不用 legacy 4-cycle 行进行贡献分解 |
| 表 II 强 NTT 对照 | 同上及 `ntt_halfbank_validation.csv` | XRT `smem32+NTTMUL`、full/half bank；KEM interval | 旧 pointer-enabled 核心；不是当前主图 whole-launch |
| half-bank 实现节省 | `ntt_v80_ppa.csv` | reducepipe vs halfbank，356447→341680 LUT，272430→270865 FF，192→176 DSP | 历史配置含 FPU；两组同配置，可内部比较 |
| half-bank 周期保持 | `ntt_halfbank_validation.csv` | 同 variant、同 backend、同请求数、不同 bank；`measured_cycles` | 同 ELF；最大绝对变化约 0.041%，没有宣称性能飞跃 |
| 表 III 严格 RV32IM PPA | `rv32im_rv64im_keccak_ppa.csv` | xlen32，NTT-only/Stage/Round；独立 post-route | 无匹配 IM Pointer；不可与旧含 F 的 Pointer 算面积效率 |
| RV64 指令数 | `rv32im_rv64im_keccak.csv` | whole-launch retired instructions，匹配 backend/request | custom Keccak issues 减半 ≠ 完整应用 instructions 减半 |
| RV64 M8 周期 | `rv32im_rv64im_keccak_xrt_m8.csv` | 四组 XRT `device_cycles` | 两种 XLEN 的 F/D 均关闭；不是直接板上时间 |
| RV64 面积与时序 | `rv32im_rv64im_keccak_ppa.csv` | xlen64 相应独立后端 | 单次物理实现，不等于多 seed 稳定 Fmax |
| 软件基线选择背景 | `ntt_w32_e2e.csv` | direct shuffle vs shared-transpose 软件 | 用于避免将较弱 SHFL 软件误称最强软件 |
| 新近 P8 微基准快照 | `keccak_w32_matched_xrt.csv` | 全后端核心，一 warp 一 state，8 连续 permutations | 作为归档补充；正文 3.291× 来自 P64 控制，不替换成这里的 3.251× |

源文件里的所有数据行保持各自结果状态；没有调整 model-parity 阈值、修改 golden baseline 或重跑硬件。

机制可追溯到：

- 图 1：NTT 索引分解 `a[l+32k]`、FIPS-202 的 $5\times5$ 状态和坐标变换；图中的 RHOPI 示例是 lane1→lane10，CHII 示例为 lane22/23/24。
- 图 3：CT 每 pair 一个 Montgomery 产品；GS 另需 sum×20159，NTTMUL 按上下半 warp 使用同一 bank。phase 是乘法器输入阶段，不等于完整指令延迟。
- 图 4：展示架构操作依赖，不表示两个硬件单元、同时发射或者固定周期时间线。THETA/RHOPI 的 L/H 读取同一旧状态；CHII 每次读取单个半字及轮索引。

- `hw/rtl/pqc/VX_pqc_nttmul.sv`：half-bank、GS 两产品、有限位宽。
- `hw/rtl/core/VX_alu_ksg25.sv`：Stage、full mask、uniform round、padding lanes。
- `hw/rtl/core/VX_alu_kround25.sv`：Round pipeline、L/H 输出、literal round。
- `sw/kernel/include/pqc/vx_ntt.h`、`vx_ksg25.h`、`vx_kround25.h`：软件接口。
- `docs/proposals/ntt_acceleration_proposal.md`、`keccak_sg25_proposal.md`、`ntt_keccak_integration_proposal.md`：完整实验命令和历史来源。
