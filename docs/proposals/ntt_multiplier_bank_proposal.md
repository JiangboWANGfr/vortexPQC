# Shared K/D NTT multiplier-bank experiment

## Scope

Compare 16 and eight signed 32x32 multipliers in the existing shared ML-KEM /
ML-DSA NTT unit. Keep the ISA, pair-low twiddle convention, software binaries,
reduction arithmetic, and pipeline stages unchanged. The default remains the
16-multiplier W32 implementation until the measured area/performance tradeoff
justifies changing it. This experiment does not modify either paper.

## Scheduling

`VX_CFG_NTT_MUL_LANES` selects a divisor of the ALU lane count, at most half
that count (one multiplier for a single-lane ALU). Each instruction owns the
multiplier input for consecutive beats. Capture its remaining operands at the
first handshake, then feed one bank per advancing cycle. Carry the beat index,
last flag, instruction header, and coefficients through the existing pipeline.
Assemble partial lane results in order; publish one complete result on the last
beat. Output backpressure freezes the serializer and every pipeline stage.

| W32 operation | Products | 16 multipliers | Eight multipliers |
| --- | ---: | ---: | ---: |
| CT K/D, GS D | 16 | 1 beat | 2 beats |
| Scalar MUL K/D | 32 | 2 beats | 4 beats |
| GS K: Montgomery then Barrett | 32 | 2 beats | 4 beats |

At the ALU interface without backpressure, latency is six cycles plus the beat
count: CT K/D and GS D change from seven to eight cycles; scalar MUL K/D and
GS K change from eight to ten. Initiation interval equals the beat count. Thus
halving the bank halves peak product throughput, but does not double the
latency of an individual instruction. The unit tests check the K-operation
latencies and intervals directly; mixed K/D traffic exercises shared state.

SimX uses the same beat count and output-backpressure behavior. Warp scheduling
may cover dependencies, but cannot restore the bank's lost product throughput.

## Verification and decision

1. Check RV32/RV64 arithmetic, masks, headers, all XOR distances, mixed K/D
   requests, initiation intervals, latency, and output stalls at both widths.
2. Run identical final ML-KEM and ML-DSA binaries on each bank, M1 and M8,
   including byte-exact KAT, exact SimX/XRT retired-instruction agreement, and
   the existing 5% cycle tolerance. Preserve raw logs and binary/runtime hashes.
3. Run independent Vivado 2025.1 post-route builds at 250 MHz, RV32IM (F/D off),
   one core, W8T32, OPT3. Use the same revised RTL for both bank sizes so that
   serializer changes are not confused with multiplier-count savings. Report
   core and NTT-hierarchy LUT/FF/DSP, WNS, and routing errors.
4. Decide whether eight multipliers are worthwhile from measured area and
   complete-request performance. Existing 16-bank results remain archived;
   neither timing closure nor performance preservation is assumed.

## Reproduction

Use the generated trees `build32_ntt_bank` and `build64_ntt_bank`, configured
with `--xlen=32` / `--xlen=64` and
`--tooldir=/home/jiangbowang/tools-pqc-v3.0.1`. Unit tests run from each tree:

```sh
make -C hw/unittest/pqc_unit run-nttmul ALU_LANES=32 THREADS=4 \
  CONFIGS="-DSIMULATION -DVX_CFG_NTT_MUL_LANES=8" OBJ_DIR=obj_ntt_bank8
make -C hw/unittest/pqc_unit run-nttmul-d ALU_LANES=32 THREADS=4 \
  CONFIGS="-DSIMULATION -DVX_CFG_NTT_MUL_LANES=8" OBJ_DIR=obj_ntt_d_bank8
```

Repeat with `16` in both the define and object directory. The mixed ALU test
checks 100 requests, all five XOR distances, partial masks, headers, sign
extension, initiation intervals, latency, and output stability. The eight-bank
case additionally requires a pending input beat to encounter output
backpressure. The D-only test covers another 88 arithmetic vectors.

The application/runtime flags are identical except for the bank define:

```text
-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE
-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_EXT_NTT_ENABLE
-DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_EXT_KROUND25_ENABLE
-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32
-DVX_CFG_NTT_MUL_LANES=8
```

Use `LIBC_PATH=/home/jiangbowang/aphdcode/vortex_v80/toolchains-im/libc32` and
`LIBCRT_PATH=/home/jiangbowang/aphdcode/vortex_v80/toolchains-im/libcrt32`.
The generated application Makefiles are copied to separate siblings
`tests/pqc/{mlkem,mldsa}_bank{8,16}`. KEM uses `KECCAK=pe SERIAL=1 NTT=reg32
NTTMUL=ise NTTBF=ise ARITH=all ARITH_MUL=ise`; DSA uses `KECCAK=pe MLDSA_RAM=full
NTT=reg32 NTTMUL=ise NTTBF=ise POINTWISE=ise POINTWISE_L5=w32`.

Both application binaries, including their host executables, are byte-identical
across bank sizes. Their kernels also match the prior final mapped applications:
KEM `3c5c64b2e2a6aa874db0f7656dc8ec52e467ebc52cccd26e7d5726139d82a69d`,
DSA `ce5ee86e8218b1303c12f13c380243788c37732765e7056f24604a9eba76c622`.
`bank{8,16}/runtime` contains independently rebuilt SimX/XRT models and the
unchanged archived runtime wrappers. Set `LD_LIBRARY_PATH` to that bank's
runtime, `VORTEX_DRIVER=simx` or `xrt`, `XRT_DEVICE=xrtsim`, and
`VORTEX_PROFILING=0`. Run KEM with `-b1 -t32` / `-b8 -t32`; DSA with
`-b1 -s0` / `-b8 -s1`. Logs and exit status are kept under `bank{8,16}`;
`run_manifest.json` records the source, binary, and runtime hashes.

Independent synthesis directories are
`hw/syn/xilinx/dut/v80_rv32im_ntt_bank{8,16}_core`. Each uses a copy of the
generated `hw/syn/xilinx/dut/build.mk` as its Makefile. From that directory:

```sh
XILINX_VIVADO=/data/Xilinx/2025.1/Vivado CLK_FREQ_MHZ=250 OPT_LEVEL=3 \
make DUT=core DEVICE=xcv80-lsva4737-2MHP-e-S MAX_JOBS=4 OPT_LEVEL=3 \
  CONFIGS="-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE -DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32 -DVX_CFG_NTT_MUL_LANES=8" build
```

Repeat independently for 16. These isolate the NTT core's area; the complete
application runs enable the Keccak backends as above. The original 16-bank
PPA directories and performance data remain untouched. Once every run finishes,
archive with `python3 ../pqc/results/collect_ntt_bank.py all` from
`build32_ntt_bank`. The collector rejects missing KATs, mismatched binaries or
instructions, and model cycle errors exceeding 5%.

## Verification so far

RV32/RV64, each with 16 and eight multipliers, pass the mixed 100-request ALU
test and the 88-vector D test: 752 requests total. The standalone eight-bank
CI parity cases also pass with exact retired-instruction counts:

| Case | SimX cycles | RTL cycles | Gap |
| --- | ---: | ---: | ---: |
| NTTMUL.K | 484,112 | 489,160 | 1.032% |
| NTTBF.K | 573,759 | 568,327 | 0.956% |

Command from the isolated default-F build `build32_ntt_bank_ci`:
`python3 -m pytest ci -k 'model_parity-nttmul_k_bank8 or model_parity-nttbf_k_bank8' -s`.
This exercises the NTT-only SimX scheduling path; the integer-only complete
application tests exercise the shared ALU arbitration with all Keccak backends.
The CI catalog lint and software/simulator boundary check pass.

All eight complete SimX runs pass. Full XRT and independent post-route results
are pending; the default bank remains 16.

## Interpretation limits

The initial SimX makespans are 5,916,106 / 5,916,108 cycles for KEM M1
(16 / eight multipliers), 8,866,146 / 8,899,611 for KEM M8,
25,829,676 / 25,829,818 for DSA M1, and 68,096,270 / 68,080,108 for DSA M8.
These are functional/timing-model observations, pending XRT confirmation.
The M1 differences are already very small, so the result cannot be attributed
solely to hiding latency with multiple warps. The experiment does not separately
isolate front-end issue, dependencies, memory stalls, or shared-unit arbitration.
A slight makespan improvement after reducing resources is not evidence of
higher peak arithmetic throughput. Do not extrapolate this comparison to one
multiplier, or use vectorless power to claim energy efficiency.
