# The configuration everything is measured at

Decided 2026-09-08 from the measurements below. **Every number the paper prints
must name this configuration or say which one it used instead.** The history in
this repo is a long argument about denominators; this file exists so that
argument does not have to be had again.

```
VX_CFG_NUM_CORES   = 1
VX_CFG_NUM_WARPS   = 8       # the M axis
VX_CFG_NUM_THREADS = 16      # build width; VX_CFG_SIMD_WIDTH follows it
launch block_dim   = 4       # the KEM's launch width
L2                 = on
```

**Build at 16, launch at 4.** Those are different decisions and they are made for
different reasons.

## Why 8 warps

`mlkem_MxL.csv`, `mldsa_MxL.csv`. At M=8, L=1 the M axis runs at **0.893
efficiency** (ML-KEM 7.145x, ML-DSA 7.139x) — near-ideal, and it is the axis that
actually delivers. `core_config_v80.csv` prices it: doubling warps costs
**1.15–1.25x** in LUTs. The axis that works is the cheap one.

## Why launch 4 lanes, not 16

`lane_axis_to_16.csv`. Keccak's lane axis is **capped at four by the library**:
`mlk_keccak_f1600_x4_native` permutes exactly four sub-states and mlkem-native has
no x8 or x16, so at a wider launch lanes 4 and above do nothing. The instruction
counts prove it — 2,440,519 at L=4 against 2,442,539 at both L=8 and L=16.

The idle lanes are not free. On identical instructions the full KEM takes
25.4M cycles at L=4, 36.0M at L=8 and **69.7M at L=16** — 2.75x slower than L=4
and 1.91x slower than a *single* lane, with IPC collapsing 0.096 → 0.035.

## Why build 16 anyway

Two things need it, and neither is KEM throughput:

- **SG5** (`proposals/cooperative_ise_proposal.md`) needs five lanes per Keccak
  state. At 16 threads that is three states per warp with one lane idle.
- **The cooperative NTT** reaches **14.853x at sixteen lanes**, 92.8% efficient,
  and only exists at this width.

The price, from `core_config_v80.csv`: quadrupling threads costs **3.50–3.80x**
in core LUTs, and it spends nearly all the timing margin (below). Building wide
and launching narrow costs **+3.0%** on the KEM, which is what makes the split
decision possible at all.

## The timing situation, which constrains everything after this

`core_config_v80.csv`, baseline core with no PQC extension, post-route at 300 MHz
on `xcv80-lsva4737-2MHP-e-S`:

| | LUT | FF | WNS | fmax |
|---|---:|---:|---:|---:|
| 4w x 4t | 33,970 | 32,115 | +0.343 ns | 334.4 MHz |
| 4w x 16t | 128,939 | 120,295 | +0.068 ns | 306.3 MHz |
| 8w x 4t | 42,553 | 39,283 | +0.004 ns | 300.4 MHz |
| **8w x 16t** | **148,827** | **134,982** | **+0.018 ns** | **301.6 MHz** |

**The chosen cell has eighteen picoseconds of margin.** Adding anything to this
core is a timing problem before it is an area problem. Integration must be
designed around that — pipelining the PE's request path, or accepting a lower
target — not checked for it afterwards.

Do not read a trend into the ordering of the last three rows; 4, 18 and 68 ps are
placement luck. What is real is that the core barely closes at any useful size,
which matches the repo's only other V80 datapoint: commit `5def82517`'s tinyGPU is
2 warps x 2 threads with all caches off and still needed phys_opt to reach
300.000 MHz from WNS -0.034.

## What this configuration is worth

Against the chosen cell's 148,827 LUTs (`keccak_pe_v80.csv`):

| | LUT | % of core |
|---|---:|---:|
| per-core PE, one engine | 3,223 | **2.17%** |
| per-lane PE, sixteen engines | 53,645 | **36.0%** |

## What is NOT yet measured at this configuration

Stated plainly, because quoting a number against a configuration it was not taken
at is the error this file exists to prevent:

- **The ablation and its 2.911x bound** is 4 warps x 4 threads
  (`ablation_mlkem.csv`).
- **The M x L sweeps** and their 8.280x / 9.307x are 8 warps x **4** threads.
- **The Keccak PE's area and fmax** are isolated flat-ported modules, not in-core.
- **ML-DSA's round-trip bound** is an extrapolation on a 35th-percentile seed
  (`ablation_mldsa.csv`).
