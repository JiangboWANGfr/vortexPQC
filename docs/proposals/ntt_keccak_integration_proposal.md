# NTT and Keccak integration

The integration combines Keccak `feature/pqc@97c435981` with the committed
NTT work at `claude/pqc-ntt@f6f56180d`. The destination is the local
`vortexPQC` repository on `feature/pqc`. Historical NTT measurements remain
attached to their original source/configuration; this document records the
combined design separately.

## Instruction and execution-unit integration

The two branches assigned the same CUSTOM0 `funct7=0x06` encodings to
Keccak SG25 stages and NTTBF. Preserve the Keccak encodings and assign
NTTBF CT/GS to `0x08`/`0x09`, respectively. The decoder, software definitions,
SimX, and raw-instruction RTL bench use the new allocation. XOR stage remains
in `funct3`; bit zero of `funct7` still selects GS. NTTMUL remains CUSTOM0
`funct7=0x05, funct3=2`. Whole-round Keccak uses opcode `0x5b`
(CUSTOM2, internally `EXT3`) and is unaffected.

RV64 preserves these opcode families but consumes one complete 64-bit state
word per lane. Stage uses `funct3=0/2/4` for THETA/RHOPI/CHII, and KROUND uses
`funct3=0`; the omitted RV32 high-half encodings are illegal. This changes no
NTT encoding: ML-KEM coefficients and NTT products retain their existing
narrow signed arithmetic in both XLEN configurations.

NTT kernels built before this integration must be rebuilt. There is no old
NTTBF alias because it would decode as an existing Keccak operation. NTT app
Makefiles now depend explicitly on `vx_pqc_defs.h`, including the end-to-end
profile app, so an incremental build observes the changed instruction ABI.

With all extensions enabled, the ALU has five execution units: integer,
multiply/divide, SG25 stage, SG25 whole round, and NTT. Their result channels
share the existing round-robin arbitration. NTT uses its arithmetic subtype;
the Keccak operations retain their separate subtype and operation numbers.
Optional-unit indexing accounts for every enabled combination.

The combined SimX path models the half-bank NTT unit with four internal beat
stages and a one-entry result channel. GS and NTTMUL reserve a second beat;
CT uses one. Backpressure freezes both the internal pipeline and a pending
second beat. The uncongested operation latencies remain six cycles for CT
and seven for GS/NTTMUL, with initiation intervals of one and two cycles.

The mixed regression also exposed a pre-existing warp-control timing error:
SimX resumed SPLIT/JOIN at SFU writeback, while RTL uses an earlier registered
control path. Traces show steady-state SPLIT dispatch-to-next-fetch intervals
of seven cycles in SimX versus three in RTL. A separate event link now models
the one-cycle SPLIT and two-cycle JOIN control paths (one cycle for JOIN
when the warp has only one thread), without changing SFU
writeback latency. Only the last beat releases the warp, and the old writeback
release is suppressed to avoid releasing it twice. This corrects the model;
it does not change processor RTL.

## Functional verification

The combined RV32 build uses one core, eight warps, 32 threads, 32 ALU lanes,
and all accelerator units:

```text
-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_EXT_KSG25_ENABLE
-DVX_CFG_EXT_KROUND25_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32
```

The archived measurements predate the independent NTT gate, when
`VX_CFG_EXT_PQC_ENABLE` instantiated both the pointer Keccak PE and the NTT
unit. Current builds must add `VX_CFG_EXT_NTT_ENABLE` whenever they execute
NTTMUL or NTTBF instructions. The split does not change either instruction's
encoding or timing.

Fresh binaries use the revised instruction encodings. All runs below pass
through XRT and the AFU, with zero output mismatches.

| Workload | Coverage | Retired instructions | Full-run cycles |
| --- | --- | ---: | ---: |
| `pqc_alu_mix` | Eight resident warps, eight synchronized bursts, 2,048 outputs | 1,552 | 7,805 |
| `nttmul_k` | 4,093 elements, 13 active lanes, 17 dependent iterations | 43,506 | 502,641 |
| `nttbf_k` | 1,044 vectors, 334,080 outputs across CT/GS XOR stages | 59,534 | 571,674 |
| `keccak_pe` | Eight requests, four permutations each | 13,480 | 30,744 |
| `keccak_sg25`, stages | THETA/RHOPI/CHII stage diagnostics, eight requests | 90,719 | 6,845,174 |
| `keccak_sg25`, whole round | SHAKE, eight requests | 103,155 | 3,215,506 |
| `mlkem_profile`, M1 | Complete KAT, register NTT and all cooperative arithmetic | 537,044 | 6,001,580 |
| `mlkem_profile`, M8 | Same binary, eight complete KAT requests | 4,296,352 | 8,994,270 |
| `mlkem_arith_xn`, M1 | Arithmetic patterns and all 65,536 signed16 reduction inputs | 1,646,854 | 20,609,521 |

The M1/M8 KEM makespans, excluding launch/setup/readback, are 5,999,744 and
8,992,026 cycles. Both exactly reproduce the final NTT-branch measurements.
These original end-to-end rows use pointer KECCAKF (`KECCAK=pe`). The later
unified experiment also connects SG25 software, SG25 stages, and whole-round
Keccak to the same complete KEM path while holding the NTT and arithmetic
configuration fixed:

| Keccak backend | SimX M1 cycles | SimX M8 cycles | M1 XRT cycles |
| --- | ---: | ---: | ---: |
| SG1 serial C | 27,739,317 | 44,824,668 | -- |
| SG25 shuffle | 7,900,300 | 19,176,807 | -- |
| SG25 stages | 5,409,748 | 12,246,844 | 5,409,552 |
| SG25 whole round | 5,198,475 | 11,947,257 | 5,203,321 |
| Pointer KECCAKF | 5,911,515 | 8,901,903 | -- |

Every row uses serial FIPS-202, register NTT, NTTBF/NTTMUL instructions, and
all three cooperative arithmetic replacements. Each request passes the exact
FIPS 203 KAT and records 140 Keccak permutations, 15 NTTs, and nine inverse
NTTs. The stage and whole-round XRT runs retire the same 436,262 and 415,822
instructions as SimX; their cycle gaps are 0.004% and 0.093%. The pointer PE
therefore retains the best eight-request throughput, while the GPR stage and
whole-round designs have lower single-request latency. The structured rows are
in [the unified KEM results](../../pqc/results/keccak_ntt_unified_mlkem.csv).

The NTT RTL unit bench passes 100 requests with measured CT II=1 and
GS/NTTMUL II=2. It passes both RV32 and RV64 with NTT enabled, including
decoder, mask, reset and backpressure checks.
Keccak stage and whole-round benches pass 288/192 RV32 and 144/96 RV64
transactions, respectively, plus invalid-contract diagnostics. The AFU AXI arbitration
bench passes six outstanding reads and six outstanding writes in separate
phases, with stalls on all five channels.
With all three extensions disabled, the default RV32 W4T4 SimX demo passes
with 2,444 instructions and 6,707 cycles.

After the warp-control model correction, existing `demo`, `diverge`, and
`dogfood -tbar` regressions also pass with all three extensions disabled
at both W4T4 and W8T32. These cover actual divergent masks and barriers
in addition to the mixed workload's uniform per-warp branches. A separate
W1T1 `diverge -n16 -d8` run passes with 6,218 instructions and 72,035 cycles,
covering the single-thread JOIN bypass.

The CI catalog registers functional SimX/XRT and model-parity cases for
`pqc_alu_mix`, plus full-tier end-to-end XRT cases for the stage and
whole-round KEM backends. Timing agreement uses the existing exact-instruction
and 5% cycle criteria. The mixed test keeps all four accelerated workloads in
one resident CTA and synchronizes the eight warps at every burst.

All ten final SimX/RTL comparisons pass with identical instruction counts
and kernel hashes. The largest cycle difference is the mixed regression:
7,986 versus 7,817 cycles, or 2.162%. Complete KEM M1 is 6,027,029 versus
6,001,634 cycles (+0.423%); M8 is 8,947,562 versus 8,999,482 cycles (-0.577%).
The new functional SimX/XRT cases and mixed model-parity case also pass
through the generated pytest/blackbox flow. The earlier failing mixed run
(8,263 versus 7,817) and its diagnostic traces remain in the raw evidence.
No tolerance or golden baseline was changed.

Detailed configuration, kernel/runtime/source hashes, and individual results:
[integration measurements](../../pqc/results/ntt_keccak_integration.json).

## Matched RV32/RV64 validation

The strict comparison uses RV32IM and RV64IM: F and D are disabled at both
XLENs. Both use one core, W8T32, the final half-bank NTT, all cooperative
ML-KEM arithmetic replacements, serial FIPS-202, and the same FIPS 203 KAT.
RV64 Stage consumes one 64-bit state word per lane, reducing THETA/RHOPI/CHII
from six to three instructions per round. RV64 KROUND consumes the same word
and reduces a complete round from two instructions to one. NTTMUL.K and NTTBF
remain narrow integer operations and need no RV64-specific duplicate.

| Backend / XLEN | M1 instructions | M1 cycles | M8 instructions | M8 cycles |
| --- | ---: | ---: | ---: | ---: |
| Stage RV32IM | 436,262 | 5,409,748 | 3,490,096 | 12,170,437 |
| Stage RV64IM | 405,716 | 5,189,658 | 3,245,728 | 12,225,892 |
| KROUND RV32IM | 415,822 | 5,198,475 | 3,326,576 | 11,937,913 |
| KROUND RV64IM | 391,996 | 5,043,599 | 3,135,968 | 11,970,704 |

Every row passes the complete KAT. Stage RV64IM retires 7.002% fewer
instructions, reduces M1 cycles by 4.068%, and raises M8 cycles by 0.456%.
KROUND RV64IM retires 5.730% fewer instructions, reduces M1 cycles by 2.979%,
and raises M8 cycles by 0.275%. The saved Keccak issues are only part of a
complete ML-KEM launch; NTT, sampling, encoding, memory traffic, and remaining
arithmetic do not halve when XLEN doubles. At M8, cycles per retired instruction
increase enough to offset the smaller instruction stream.

M1 SimX/RTL gaps are 0.059%/0.366% for Stage RV32IM/RV64IM and
0.061%/0.515% for KROUND. XRT/RTL gaps remain at or below 0.055%, with exact
instruction agreement. Direct RV64 NTTMUL, NTTBF, and forward/inverse
register-NTT checks also pass. ELF attributes and disassembly confirm soft-float
IM binaries with no floating-point or atomic instructions. The strict ABI used
a local LLVM 20.1.8 libc/libcrt build; it must be repackaged through
`vortex-toolchain-prebuilt` before enabling these exact F/D-off jobs in CI.
Structured data are in
[the integer-only XLEN comparison](../../pqc/results/rv32im_rv64im_keccak.csv).

## Matched integer-only RV32/RV64 synthesis

The XLEN comparison uses source commit `766b66a74` and keeps the V80 device,
one-core W8T32 geometry, final half-bank NTT, Vivado 2025.1, OPT3, and the
250 MHz target fixed. RV32IM and RV64IM both disable F and D. Each XLEN has an
independent NTT-only denominator plus Stage and KROUND implementations; all six
projects were built from scratch without incremental checkpoints.

The configured `build32_im` and `build64_im` trees used this command sequence:

```sh
source /data/Xilinx/2025.1/Vivado/settings64.sh
for xlen in 32 64; do
  (
    cd build${xlen}_im/hw/syn/xilinx/dut
    common="-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE \
            -DVX_CFG_EXT_NTT_ENABLE \
            -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32"
    for variant in only stage kround; do
      case "$variant" in
        only) feature="" ;;
        stage) feature="-DVX_CFG_EXT_KSG25_ENABLE" ;;
        kround) feature="-DVX_CFG_EXT_KROUND25_ENABLE" ;;
      esac
      make -C v80_rv${xlen}im_ntt_${variant}_core DUT=core \
        ROOT_DIR="$PWD/../../../.." DEVICE=xcv80-lsva4737-2MHP-e-S \
        CLK_FREQ_MHZ=250 OPT_LEVEL=3 CONFIGS="$common $feature"
    done
  )
done
```

| XLEN / build | LUT | FF | BRAM tiles | DSP | WNS | STA Fmax |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| RV32IM NTT-only | 220,950 | 131,798 | 133.0 | 112 | +0.004 ns | 250.3 MHz |
| RV32IM NTT + Stage | 223,444 | 133,512 | 133.0 | 112 | +0.004 ns | 250.3 MHz |
| RV32IM NTT + KROUND | 222,935 | 138,065 | 133.0 | 112 | +0.007 ns | 250.4 MHz |
| RV64IM NTT-only | 429,067 | 228,266 | 261.5 | 16 | 0.000 ns | 250.0 MHz |
| RV64IM NTT + Stage | 432,843 | 232,350 | 261.5 | 16 | -0.148 ns | 241.1 MHz |
| RV64IM NTT + KROUND | 431,004 | 234,240 | 261.5 | 16 | -0.013 ns | 249.2 MHz |

All six designs route with zero routing-error nets. The three RV32IM rows and
RV64IM NTT-only close 250 MHz. RV64IM Stage and KROUND complete post-route
physical optimization but remain setup-negative and are recorded as
`timing_unmet`. Their hold timing closes.

Relative to the same-XLEN NTT-only denominator, Stage adds 2,494 LUT/1,714 FF
(+1.129%/+1.300%) at RV32IM and 3,776 LUT/4,084 FF (+0.880%/+1.789%) at
RV64IM. KROUND adds 1,985 LUT/6,267 FF (+0.898%/+4.755%) at RV32IM and 1,937
LUT/5,974 FF (+0.451%/+2.617%) at RV64IM. None of these Keccak additions
changes BRAM or DSP count.

The NTT hierarchy remains at 16 DSP in every row and the FPU hierarchy is
absent. RV32IM's general multiply/divide unit uses 96 DSP, while the serial
RV64IM implementation uses none. This explains the full-core DSP decrease from
112 to 16 when XLEN changes; it is not an NTT resource reduction. RV64IM
NTT-only changes full-core LUT/FF/BRAM/DSP by +94.192%/+73.194%/+96.617%/
-85.714% relative to RV32IM NTT-only. Each row is one OOC seed, so these deltas
are specific to this device and placement run.

Structured measurements and exact report directories are in
[the integer-only XLEN PPA comparison](../../pqc/results/rv32im_rv64im_keccak_ppa.csv).
Vectorless power is archived in the build reports only for audit; it has neither
workload activity nor environmental constraints and does not support energy
claims.

## Physical verification and evidence

The physical run uses Vivado 2025.1, V80 `xcv80-lsva4737-2MHP-e-S`,
`VX_core_top`, 250 MHz (4 ns), and `OPT_LEVEL=3`, with the same combined
RV32 W8T32 configuration. This is a new combined-core implementation;
the earlier NTT-only results cannot establish timing for the extra Keccak
units and the larger shared result arbiter. The core wrapper does not
include the AFU, so it cannot establish physical timing for the AFU change.
The OOC wrapper leaves input/output delays unconstrained; closure covers the
core's internal clocked paths.

After routing and post-route physical optimization, all specified timing
constraints pass: setup WNS **+0.006 ns**, TNS **0 ns**; hold WHS
**+0.010 ns**, THS **0 ns**; pulse-width slack **+1.394 ns**, with no failing
endpoints. The worst setup path is from the issue dispatcher buffer to the
integer ALU response buffer. It does not pass through the NTT unit.
The setup-slack estimate is approximately 250.4 MHz, derived from static
timing analysis at the single 250 MHz target frequency.

| Final hierarchy | LUT | FF | RAMB36 | RAMB18 | DSP |
| --- | ---: | ---: | ---: | ---: | ---: |
| Complete core | 348,081 | 278,850 | 113 | 40 | 176 |
| NTT unit (included above) | 5,127 | 5,635 | 0 | 0 | 16 |
| Keccak SG25 stage unit (included above) | 855 | 2,025 | 0 | 0 | 0 |
| Keccak whole-round unit (included above) | 2,440 | 6,130 | 0 | 0 | 0 |

The NTT LUT count comprises 5,097 logic LUTs and 30 SRLs. No URAM is used.
These are combined-core results, with all three extensions enabled together.

Local raw evidence is archived under
`build_merge_ntt_keccak_20260911/`: `validation/` contains build/run commands,
logs, binaries, hashes, unit and compatibility results; `vivado/` contains
the source snapshot, project, constraints, reports and checkpoints. Historical
paths inside run manifests identify the isolated worktree used for testing.

Reconfigure a fresh build tree after checking out the merge; do not run old
NTTBF binaries against the merged decoder. No remote push is part of this
integration.

## Matched Keccak backend synthesis

`VX_CFG_EXT_NTT_ENABLE` is the common NTT hardware gate.
`VX_CFG_EXT_PQC_ENABLE`, `VX_CFG_EXT_KSG25_ENABLE`, and
`VX_CFG_EXT_KROUND25_ENABLE` independently select the pointer, stage, and
whole-round Keccak hardware. The paper PPA comparison therefore uses four
complete-core builds. All four keep RV32, W8T32, the device, clock constraint,
Vivado version, and optimization level fixed.

| Build | Enabled accelerator units | Purpose |
| --- | --- | --- |
| `ntt_only` | NTT | Common area/timing baseline; SG1 and SG25-shuffle need no Keccak unit |
| `ntt_stage` | NTT + SG25 stages | Stage-factored GPR design |
| `ntt_kround` | NTT + SG25 whole round | GPR whole-round comparator |
| `ntt_pointer` | NTT + pointer Keccak PE | Memory-pointer PE comparator |

Run each build synchronously from a freshly configured RV32 tree. The copied
per-DUT Makefile expects `ROOT_DIR` to name that build tree:

```sh
cd build32_pqc/hw/syn/xilinx/dut
source /data/Xilinx/2025.1/Vivado/settings64.sh

run_core () {
    name="$1"
    configs="$2"
    mkdir -p "v80_${name}_core"
    cp build.mk "v80_${name}_core/Makefile"
    make -C "v80_${name}_core" DUT=core ROOT_DIR="$PWD/../../../.." clean
    make -C "v80_${name}_core" DUT=core ROOT_DIR="$PWD/../../../.." \
        DEVICE=xcv80-lsva4737-2MHP-e-S CLK_FREQ_MHZ=250 OPT_LEVEL=3 \
        CONFIGS="$configs -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32"
}

run_core ntt_only    '-DVX_CFG_EXT_NTT_ENABLE'
run_core ntt_stage   '-DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_EXT_KSG25_ENABLE'
run_core ntt_kround  '-DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_EXT_KROUND25_ENABLE'
run_core ntt_pointer '-DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_EXT_PQC_ENABLE'
```

Use `post_impl_util.rpt`, `timing_summary.rpt`, `timing.rpt`, and
`qor_summary.json` from each directory. Report complete-core LUT, FF, BRAM,
DSP, setup/hold slack, and achieved frequency. Compute the three Keccak area
deltas against `ntt_only`; hierarchy rows are supporting evidence because
shared arbiter and routing costs only appear in the complete-core delta. Do
not combine the archived all-enabled core area with any new isolated result.

The four independent post-route runs completed on 2026-09-14 from hardware
commit `8798cb61a`:

| Build | LUT | Delta LUT vs. NTT only | FF | Delta FF vs. NTT only | DSP | WNS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `ntt_only` | 330,530 | -- | 267,838 | -- | 176 | 0.000 ns |
| `ntt_stage` | 334,561 | +4,031 (+1.219%) | 269,806 | +1,968 (+0.735%) | 176 | +0.006 ns |
| `ntt_kround` | 332,710 | +2,180 (+0.660%) | 274,237 | +6,399 (+2.389%) | 176 | +0.003 ns |
| `ntt_pointer` | 341,751 | +11,221 (+3.395%) | 271,058 | +3,220 (+1.202%) | 176 | +0.004 ns |

Every build uses 113 RAMB36 and 40 RAMB18, closes the 250 MHz target, and
has zero routing-error nets. The stage, whole-round, and pointer hierarchies
contain 902/2,025, 2,503/6,130, and 10,414/4,556 LUT/FF, respectively. These
hierarchy counts explain the local units; the complete-core deltas above are
the comparison metric because they also include decode, arbitration,
placement, and routing effects. The whole-round worst setup path runs from
the issue dispatcher to the integer ALU/shuffle response, not through the
whole-round unit.

The complete result manifest, including device, tool, configuration, hierarchy
counts, report directories, and derived deltas, is
[`pqc/results/ntt_keccak_backend_ppa.csv`](../../pqc/results/ntt_keccak_backend_ppa.csv).
Its power columns are vectorless Vivado estimates without workload activity or
environmental constraints and do not support energy claims.
