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
and all three extensions:

```text
-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_EXT_KSG25_ENABLE
-DVX_CFG_EXT_KROUND25_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32
```

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
GS/NTTMUL II=2. It passes both the combined RV32 configuration and RV64
with only PQC enabled, including decoder, mask, reset and backpressure checks.
Keccak stage and whole-round benches pass 288 and 192 transactions,
respectively, plus invalid-contract diagnostics. The AFU AXI arbitration
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
