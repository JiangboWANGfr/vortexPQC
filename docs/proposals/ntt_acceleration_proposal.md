# NTT acceleration: measured scope and selected W32 path

The first decision was whether accelerating NTT still matters after KECCAKF,
and whether a scalar modular-multiply instruction covers enough of that work.
The measurements below complete that characterization for ML-KEM-768 and
validate both `NTTMUL.K` and a cross-lane SG2 butterfly. On the measured W32
configuration, register-resident `SG2+NTTMUL.K` is now the selected functional
and performance prototype. The half-width multiplier bank with static XOR-stage
routing closes 250 MHz on V80, reduces the NTT hierarchy to 5,079 LUT and
16 DSP, and preserves measured complete-KEM performance. It is the selected
implementation baseline for the remaining arithmetic paths.

## Matched end-to-end experiment

Measured on 2026-09-09 from `d82b2a24f` plus the profiling changes in this
branch: SimX, RV32, one core, eight warps, 32 threads, L2/L3 disabled,
PQC enabled with one Keccak engine. Each request uses one active lane and the
same upstream KAT `d`, `z`, and `m`. Each of the four compiled arms is reused
unchanged for both one and eight requests.

`mlkem_profile` now gives each request separate scratch space and per-hart
counters. It returns its compiled arm identity and absolute phase start/end
timestamps. Batch time is `max(end) - min(start)` on the shared core counter,
from the first request's KEM start to the last request's KEM end. It does not
cover full kernel-launch latency, output verification, or host readback.
Summing concurrent request durations would not measure batch latency.

| Arm | One request, cycles | Eight requests, batch cycles |
| --- | ---: | ---: |
| C Keccak, real NTT | 34,418,365 | 47,829,440 |
| KECCAKF, real NTT | 11,968,032 | 15,940,118 |
| KECCAKF, NTT/INTT skipped | 7,644,507 | 9,667,744 |
| C Keccak, NTT/INTT skipped | 30,092,054 | 41,927,273 |

After KECCAKF, skipping NTT/INTT removes **36.13%** of single-request time and
**39.35%** of eight-request batch time. The corresponding real/no-op ratios
are **1.566x** and **1.649x**. Before KECCAKF, the reductions are 12.57% and
12.34%. NTT therefore remains a material target at both measured operating
points.

These are whole-transform no-op sensitivities, not physically attainable
accelerator speedups. Removing transforms also changes code, cache traffic,
and contention. The NTT cycle difference before versus after KECCAKF differs
by only 2,786 cycles for one request, but by 370,207 cycles for eight requests;
the batch contributions are not exactly additive.

All 18 requests in the two real arms match the complete upstream known-answer
`pk`, `sk`, `ct`, and both shared secrets. Every request in all eight runs has
zero phase status and identical counts: 144 Keccak x1, 24 Keccak x4, 15 NTT,
9 INTT, 27 rejection-sampler hooks, 12 mulcache, 12 basemul, and 18 reductions.
The skipped-transform arms deliberately produce invalid cryptographic output.
Their status/count checks establish that the measured KEM phases did not exit
early; they do not establish cryptographic correctness.

Data: [run summaries](../../pqc/results/ntt_share_after_keccak.csv) and
[all 36 request timings](../../pqc/results/ntt_share_requests.csv).
Raw logs, arm binaries, kernel hashes, input hash, and source/build manifest
are under `build32_ntt_target/ntt_share/`. Earlier four-warp measurements under
`build32/ntt_share/` predate the per-hart counter extension and are not the
denominator for this table. The old `mlkem_width` grid also used different
coins and cache settings, so its numbers are not combined with these results.

## Controlled modular-multiply experiment

`mlkem_ntt_cost` compares real `fqmul` with two dependent XORs that return the
first operand while keeping both inputs live. Both arms disable loop unrolling
and share the same butterfly loops, twiddle table, and polynomial storage.
Input setup and reference checks are outside timing. One warmup is discarded;
eight samples alternate the order of the two arms.

| Transform, one active lane | Real cycles/call | Identity cycles/call | Reduction |
| --- | ---: | ---: | ---: |
| Forward NTT | 152,862.5 | 119,509.5 | 21.82% |
| Inverse NTT | 274,771.5 | 234,370.5 | 14.70% |

Real outputs match upstream C coefficient by coefficient on nine inputs for
each transform. Disassembly confirms the same coefficient/twiddle loads,
stores, and loop back edges in each pair. Inverse Barrett reduction remains
real. The compiler otherwise unrolled the inverse identity arm much more
aggressively; that uncontrolled comparison is excluded from this table.

The residual includes indexing, loop control, butterfly add/subtract, memory
accesses, inverse Barrett reduction, the XOR stand-in, and scheduling effects.
It is not a measurement of memory cost alone. These deliberately controlled
loops also differ from the production library's optimized loops. Do not
multiply these percentages into the end-to-end no-op delta to predict an ISA
speedup.

Data: [arithmetic sensitivity](../../pqc/results/ntt_fqmul_sensitivity.csv).
Method and commands: [probe README](../../tests/pqc/mlkem_ntt_cost/README.md).
Logs and disassembly are under `build32_ntt_target/ntt_cost/`.

## Initial forward-only cooperative control

The original `mlkem_ntt_xn` test passed at 1, 4, and 32 lanes on the same
target configuration. At 32 lanes, the forward transform takes 7,955 cycles
against its own measured scalar reference of 135,811 cycles: **17.072x**.
At four lanes it takes 54,149 cycles, or 2.508x. All 256 coefficients match.

This measures an isolated forward transform. It does not include inverse NTT,
changing active lanes inside a KEM request, or an end-to-end integration cost.
Data: [cooperative control](../../pqc/results/ntt_cooperative_target.csv).

## Matched cooperative forward/inverse verification

The extended `mlkem_ntt_xn` now checks five input patterns in each direction at
one/eight requests and 1/4/32 lanes. Every request has a separate polynomial
and distinct input. Forward inputs include the signed `q - 1` boundaries;
inverse boundary inputs cover the full signed 16-bit range. Boundary,
alternating, and impulse patterns supplement the deterministic mixed input.
All **69,120 coefficients** match upstream C in both host and device checks.

Each sample runs the cooperative transform and scalar reference in separate
launches. All resident warps synchronize after input/stack preparation and
again after their end timestamps, before copying output or inspecting stacks.
Without those barriers, the first M8/32-lane run reported 5,346,900 forward
batch cycles because early transforms overlapped other warps' stack painting.
That contaminated run is retained only under
`build32_ntt_target/ntt_cooperative_full_pre_barrier/`.

The corrected mixed-input sample reports the following batch intervals;
speedups use the scalar reference measured in the same run. These are
individual samples, not averages over the five different input patterns.

| Requests | Lanes | Forward cycles | Forward speedup | Inverse cycles | Inverse speedup |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 214,104 | 0.636x | 356,496 | 0.719x |
| 1 | 4 | 54,552 | 2.494x | 91,290 | 2.808x |
| 1 | 32 | 8,519 | **15.973x** | 18,416 | **13.922x** |
| 8 | 1 | 313,755 | 0.618x | 568,758 | 0.699x |
| 8 | 4 | 79,056 | 2.454x | 144,722 | 2.746x |
| 8 | 32 | 16,967 | **11.433x** | 66,987 | **5.933x** |

The eight-request inverse result shows why a single isolated forward speedup
does not characterize concurrent transform throughput. These measurements
include butterfly computation, coefficient traffic, layer barriers, and the
measurement barriers; the KEM dispatch bridge is measured separately below.
Data: [all 60 transform batches](../../pqc/results/ntt_cooperative_full.csv).
Current logs, hashes, and disassembly are under
`build32_ntt_barrier/ntt_barrier_validation/`. The updated kernel retains
compiler split/join lowering and has both cross-request barriers in its
disassembly.

## Cooperative NTT/INTT inside ML-KEM

The software integration now uses the same shared butterfly implementation in
the standalone transform test and in `mlkem_profile NTT=coop`. The latter
launches one warp per request with the requested number of lanes, so KMU
initializes each participating lane's stack and ABI state. A naked entry
reduces the active mask to lane zero for the scalar KEM body. Each native NTT
hook publishes its polynomial and direction in a per-warp descriptor, executes
a CTA barrier, activates the participating lanes, and calls the cooperative
transform. The transform executes a CTA barrier after each layer; the return
bridge synchronizes and reduces the mask to the leader before restoring its
caller frame.

This avoids assuming that previously unlaunched lanes have valid C stacks or
that a lane's pointer argument automatically broadcasts across 32 threads.
The bridge currently supports RV32 and one core, with power-of-two widths up
to 32 and no more requests than resident warp slots. The host rejects other
widths, a multi-lane scalar arm, and incompatible no-op/bridge build options.

The following measurements reuse one cooperative binary for all six request/
width combinations. The scalar library arm was rebuilt and remeasured after
the integration; its cycle counts match the first experiment. Both arms use
KECCAKF, the same KAT coins, and the same 8-warp, 32-thread SimX configuration.

| NTT implementation | One request, cycles | Speedup | Eight requests, batch cycles | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Scalar library | 11,968,032 | 1.000x | 15,940,118 | 1.000x |
| Cooperative, 1 lane | 14,081,418 | 0.850x | 18,839,804 | 0.846x |
| Cooperative, 4 lanes | 9,289,997 | 1.288x | 11,958,821 | 1.333x |
| Cooperative, 32 lanes | 8,066,367 | **1.484x** | 11,378,105 | **1.401x** |

At 32 lanes the measured KEM phases use **32.60% fewer cycles** for one request
and **28.62% fewer cycles** for eight requests. Every measured NTT call includes
descriptor publication, mask changes, transform work, and synchronization.
KMU/stack initialization precedes each request's own start timestamp; host
work and verification are outside the measurement. The requests start at
slightly different times, so the batch span may overlap other requests' setup
or result export. These are KEM-phase batch cycles, not whole-launch latency.
The cooperative one-lane path is about 15% slower than the production scalar
library; its indexing and dispatch costs cannot be ignored when interpreting
isolated speedups.

All 36 requests across the eight runs match the complete `pk`, `sk`, `ct`,
`ss_enc`, and `ss_dec` KAT. All phase statuses are zero and all primitive counts
match the first experiment, including 15 NTT and nine INTT calls per request.
The compiled cooperative arm reports flags 9 versus scalar flags 1. A
cooperative host deliberately paired with a scalar kernel is rejected despite
passing the numerical KAT, which checks arm selection independently of output.

Data: [batch results](../../pqc/results/ntt_cooperative_e2e.csv) and
[all request timings](../../pqc/results/ntt_cooperative_requests.csv).
The L1/L4 SimX logs and hashes are under
`build32_ntt_barrier/ntt_barrier_validation/`. The canonical L32 SimX/RTL
logs, matching binaries, configuration stamps, and manifest are under
`build32_ntt_parity/ntt_barrier_e2e/`.

## Layer synchronization correction

The first cooperative implementation used RISC-V `fence` after every layer.
That is stronger than the same-core handoff requires on Vortex: RTL turns it
into a write-back cache flush, while SimX only waits for its pending LSU table.
The new `lsu_model_probe` separates empty loop work, a clean synchronization,
and four stores per lane before synchronization. Its L32 dirty case writes the
same 512 B, or eight 64-byte lines, as one polynomial layer.

At M8/L32 with 177 rounds per request, matching the number of full-warp stage
syncs in one KEM request, the per-dynamic-operation costs are:

| Operation | SimX cycles | RTL cycles |
| --- | ---: | ---: |
| Clean `fence` above empty | 1.516 | 96.971 |
| Four stores above clean `fence` | 10.681 | 94.786 |
| Clean CTA barrier above empty | 0.136 | 0.198 |
| Four stores above clean CTA barrier | 11.444 | 10.453 |

All 40 SimX/RTL probe points pass their data checks and retire identical
instruction counts. A CTA barrier waits for the LSU scheduler to drain but
does not flush valid cache lines, so it supplies the needed layer ordering.
The NTT code now uses that barrier for layer boundaries, descriptor publication,
and final result handoff. Under the same probe, this removes almost all of the
synchronization-specific model difference. The M8 empty kernels still differ
by roughly 13--14%. The probe has no PQC traffic, so this residual is a generic
high-concurrency timing question; it does not measure PQC-client arbitration.

Data: [synchronization probe summary](../../pqc/results/ntt_lsu_model_probe.csv).
The 40 raw logs, per-request rows, disassembly checks, and hashes are under
`build32_lsu_probe/results/lsu_model_probe_barrier/`. Results before this change
remain useful for diagnosing the old model gap but are no longer the software
denominator.

## W32 register-resident controls

Two W32 software controls now test whether eliminating the seven shared-array
round trips is sufficient on the existing Vortex datapaths. Both keep eight
coefficients per lane and use the upstream arithmetic and twiddle table:

- `reg32` starts from `r[lane + 32*k]`. Its first three layers are lane-local;
  the remaining four use `SHFL.BFLY` at XOR distances 16, 8, 4, and 2.
- `smem32` adapts the GPU literature's radix-8 schedule to ML-KEM's seven
  layers as 3+3+1. It fuses three lane-local layers, performs a padded 288-word
  LMEM transpose, fuses three more layers, transposes again, and executes the
  final layer locally.

The table reports the median of five input patterns in the standalone test.
Each implementation also passed all five patterns in both directions at one
and eight requests; each row has zero coefficient mismatches.

| Implementation | M1 forward | M1 inverse | M8 forward | M8 inverse |
| --- | ---: | ---: | ---: | ---: |
| Shared-array cooperative | **8,537** | 18,416 | **16,952** | 66,040 |
| `reg32` direct shuffle | 14,175 | 19,812 | 53,025 | 69,248 |
| `smem32` 3+3+1 transpose | 8,772 | **13,429** | 44,617 | **63,068** |

`reg32` keeps the coefficient state in GPRs without body spills, but each
cross-lane layer has eight sequential register groups. Its forward and inverse
functions contain 32 shuffle instructions apiece. The shared implementation
needs only four butterfly sequences per lane per layer. Removing memory traffic
therefore increases the amount of serial arithmetic on the current scalar lane
ISA. `smem32` restores the ideal 28 modular multiplies per lane. Its forward and
inverse functions contain no shuffle and two transpose barriers apiece; the
coefficient state remains in GPRs. The transpose traffic still makes its M8
forward batch slower than shared, while its inverse path is the fastest of the
three. Standalone batch intervals are therefore not additive predictors of the
complete KEM schedule.

The same ordering holds in the complete KECCAKF-assisted ML-KEM KAT at the
intended eight-request operating point:

| NTT implementation | M1 KEM cycles | Delta vs shared | M8 batch cycles | Delta vs shared |
| --- | ---: | ---: | ---: | ---: |
| Shared-array cooperative | 8,066,367 | 0.00% | 11,378,105 | 0.00% |
| `reg32` direct shuffle | 8,074,507 | +0.10% | 11,392,888 | +0.13% |
| `smem32` 3+3+1 transpose | **7,942,788** | -1.53% | **11,110,749** | -2.35% |

Every arm matches `pk`, `sk`, `ct`, `ss_enc`, and `ss_dec` byte for byte at M1
and for all eight M8 requests. The arm flags are 9, 25, and 41 respectively,
and every request records 15 NTT and nine INTT calls. `smem32` is the fastest
complete SimX KEM arm at both request counts, including a 2.35% reduction at
the target concurrency. RTL repeats that result: `smem32` reduces the M1 and
M8 batch intervals by 1.54% and 2.58% relative to shared. All six SimX/RTL
pairs retire identical instruction counts; their cycle differences are below
0.73% at M1 and below 4.23% at M8. `smem32` is therefore the strongest pre-ISE
software denominator.

This result does not reject register-distributed hardware. It shows that the
existing scalar shuffle cannot express a paired butterfly cheaply enough.
It motivated the SG2 collective measured below, which removes both the
shuffle sequence and duplicated pair work. The standalone `fqmul` sensitivity
also motivated measuring narrow modular multiply both separately and together
with SG2.

Data: [standalone W32 results](../../pqc/results/ntt_w32_standalone.csv) and
[end-to-end W32 results](../../pqc/results/ntt_w32_e2e.csv). Standalone logs
and disassembly are under `build32_ntt_barrier/ntt_barrier_validation/`.
Canonical BAR-synchronized SimX/RTL KEM logs, binaries, configuration stamps,
and formulas are under `build32_ntt_parity/ntt_barrier_e2e/`.

## Constraints from the local literature set

A scan of the 63 PDFs in the local top-100 bundle found no paper that maps an
NTT butterfly directly across SIMT lanes with warp shuffle. This supports SG2
as a distinct experiment within this corpus, but is not a general novelty
claim. The GPU papers instead support local layer fusion followed by explicit
data-layout changes:

- P146, *High-Throughput GPU Implementation of Dilithium Post-Quantum Digital
  Signature* (2024), keeps eight coefficients per thread, fuses radix-8 groups,
  and uses two padded shared-memory exchanges. Its forward and inverse winners
  use different mappings, so occupancy and synchronization have to be measured
  independently. `smem32` reproduces its central 3+3 schedule for ML-KEM.
- P141, *HI-Kyber* (2024), obtains its GPU gains through SLM and depth-first
  traversal that reuse contiguous register blocks. It supports further study
  of schedule and locality, but does not predict a win for a fixed
  `r[lane + 32*k]` shuffle layout.
- P017, *RISQ-V* (2020), uses 16 register-held Kyber coefficients and a 3+3+1
  decomposition. Its fused unit directly accesses a centralized register file;
  that mechanism is not equivalent to distributed SIMT lane state.

The arithmetic papers prevent selecting SG2 by default. P184, *Accelerating
CRYSTALS-Kyber and Dilithium via a Single Montgomery Reduction ISE on RISC-V*
(2025), reports that its small Montgomery instruction reduces Kyber's total
cycles by about 16%, versus about 9--12% for its fused butterfly unit, while
adding 6.2% rather than 25.3% LUTs on Ibex. P024's scalar NTT ISE (2021) also
shows that a shared multiply/reduce path covers `fqmul`, reduction, forward and
inverse butterflies, while an internal twiddle LUT and packed result solve its
three-input/two-output encoding problem. These CPU results do not predict
Vortex performance, but they require a side-by-side `NTTMUL`/SG2 evaluation.

The modular arithmetic itself remains a design variable. P014 supports Barrett
reduction selected around the available high-half multiply; P273 supports a
general narrow multiply/MAC with software Montgomery; P281 supports a
fixed-modulus Plantard alternative. P150 shows one physical multiplier layout
that can serve two ML-KEM lanes or one ML-DSA lane. A fixed Montgomery opcode
must therefore beat a general narrow-product control before its semantics are
frozen. P160 keeps a full NTT engine competitive for repeated ML-DSA transforms
with persistent local state, so that design remains a later workload-specific
upper bound rather than the first implementation.

## XRT validation and model parity

The current BAR-synchronized binaries use the same RV32, one-core, eight-warp,
32-thread configuration in SimX and RTL. Direct comparisons give:

| NTT implementation | Requests | SimX batch cycles | RTL batch cycles | Absolute gap | Instructions in each model |
| --- | ---: | ---: | ---: | ---: | ---: |
| Shared array | 1 | 8,066,367 | 8,009,266 | 0.713% | 744,162 |
| Shared array | 8 | 11,378,105 | 11,879,796 | **4.223%** | 5,953,296 |
| `smem32` | 1 | 7,942,788 | 7,885,868 | 0.722% | 733,887 |
| `smem32` | 8 | 11,110,749 | 11,573,280 | 3.997% | 5,871,096 |

All four pairs retire exactly matching instruction counts and remain within the
default 5% cycle threshold. In particular, the current shared M8 gap is
**4.223061%**. The earlier `fence` binary's 7.985151% M8 gap is retained only as
historical data and is not the current denominator. These are direct logged
SimX/RTL comparisons; the catalogued model-parity CI case was not rerun for the
BAR refresh.

The full XRT simulation path (`driver=xrt`, `target=xrtsim`) was rerun for the
current `smem32` binary and a freshly rebuilt scalar-library denominator:

| XRT NTT implementation | One request, cycles | Speedup | Eight requests, batch cycles | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Scalar library | 11,875,328 | 1.000x | 16,333,647 | 1.000x |
| `smem32`, 32 lanes | 7,908,765 | **1.501540x** | 11,726,329 | **1.392904x** |

All 18 requests across the four XRT runs pass the complete ML-KEM `pk`, `sk`,
`ct`, `ss_enc`, and `ss_dec` KAT with zero phase status. Every request records
15 NTT and nine INTT calls; the scalar and `smem32` arms report flags 1 and 41.
The `smem32` XRT runs retire 733,887 instructions at M1 and 5,871,096 at M8;
the scalar runs retire 1,158,142 and 9,265,136. The archived `smem32` kernel and
host binary hashes exactly match the SimX pair above. Relative to those SimX
batch intervals, XRT differs by -0.428351% at M1 and +5.540401% at M8. No
synthesis or catalogued pytest CI was run for this refresh.

Data: [RTL/XRT and parity summary](../../pqc/results/ntt_rtl_validation.csv).
Current SimX/RTL logs and manifests are under
`build32_ntt_parity/ntt_barrier_e2e/`. Current XRT logs, binaries, source hashes,
request rows, and `build_manifest.json` are under
`build32_ntt_barrier/ntt_barrier_validation/xrt_smem32/`. Historical `fence`
logs remain under `build32_ntt_parity/ntt_barrier_e2e/baseline/old_fence/` and
`build32_ntt_rtl/ntt_rtl_validation/`.

## Implemented `NTTMUL.K` full-bank baseline

Unless explicitly marked as legacy below, the NTTMUL/SG2 implementation and
measurements through the V80 PPA section describe the 32-multiplier full-bank
design preserved in commit `9e8284986`. Its SimX NTT latency is six cycles and
its ready/valid path accepts one request per cycle.

`NTTMUL.K` uses an R-type encoding in `CUSTOM-0` (`opcode=0x0b`,
`funct7=0x05`, `funct3=0x2`). Its fixed match value is `0x0a00200b` with
mask `0xfe00707f`; `funct3=0` in the same row remains `KECCAKF`, and
`funct3=1` remains reserved. The two source operands and one destination fit
the ordinary R-type register fields.

The instruction implements the exact ML-KEM signed Montgomery operation, not
a general modular product. For `q=3329` and `QINV=62209`, its architectural
semantics are

```text
a = signed16(rs1[15:0])
b = signed16(rs2[15:0])
p = a * b
m = signed16((unsigned16(p) * QINV) mod 2^16)
r = signed16((p - m * q) / 2^16)
rd = sign_extend_XLEN(r)
```

The numerator in the division is an exact multiple of `2^16`. The semantics
cover every signed 16-bit operand pair, which is wider than the coefficient and
twiddle ranges exercised by ML-KEM.

RTL decodes the instruction as `EX_ALU`, `ALU_TYPE_ARITH`, and
`INST_ALU_NTTMUL_K`. The ALU PE switch sends each active lane to a separate
narrow unit. Each unit uses a signed 16-by-16 `VX_multiplier` followed by fixed
shift/add networks for 62209 and 3329. The multiplier and header use the same
`LATENCY_IMUL` pipeline depth, three cycles in the measured configuration, and
the ready/valid path supports one independent request per cycle. One registered
reduction stage and one registered result stage separate the multiplier from
the ALU response path, so SimX models this operation at six cycles. This path
does not depend on `EXT_M`, does not use an LSU or SFU client, and does not
stall the whole warp.

The W32 RTL unit test covers decode, partial masks, consecutive requests,
backpressure, and result-header preservation as part of a 48-request combined
NTTMUL/NTTBF sequence with 36 consecutive issues. It passes at both XLEN=32
and XLEN=64. A separate 13-lane program checks 4,093 input pairs, including the
14-by-14 signed boundary cross-product, followed by deterministic full-range
inputs. It chains 17 dependent `NTTMUL.K` operations per pair. SimX and RTL
both report zero mismatches and retire 43,506 instructions. Their run cycles
are 497,744 and 502,658, a **0.977603%** gap.

A separate single-lane timing probe places 64 contiguous `NTTMUL.K`
instructions in one inline-assembly block with `rd=rs1`. Disassembly confirms
all 64 encodings are adjacent. Its measured interval is 920 SimX cycles versus
917 RTL cycles, a **0.327154%** gap. Together with the RTL pipeline structure,
this is the calibration evidence for the six-cycle SimX latency.

The controlled transform probe then replaces every C `fqmul` with
`NTTMUL.K` while retaining the same loops, loads, stores, twiddles, and
butterfly add/subtract operations. Eight measured samples follow one warmup:

| Transform | C `fqmul`, cycles/call | `NTTMUL.K`, cycles/call | Cycle reduction | Speedup | Instructions/call, C to ISE | Instruction reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Forward NTT | 152,873.8 | 111,455.6 | **27.093%** | **1.371612x** | 15,842.8 to 11,226.6 | 29.138% |
| Inverse NTT | 274,782.8 | 227,468.6 | **17.219%** | **1.208003x** | 28,406.8 to 22,509.6 | 20.760% |

The C and ISE checksums match for both transforms, and both have zero
coefficient mismatches against upstream C. The forward path contains 896
modular multiplies per call; inverse contains 1,152 including final scaling.
The speedups above use the C arms from the same six-arm run, avoiding a
cross-run denominator.

The direct W32 transform test reports these medians over its five input
patterns; the scalar comparison is measured in the same run:

| Requests | Forward `smem32+NTTMUL.K` | Scalar reference | Speedup | Inverse `smem32+NTTMUL.K` | Scalar reference | Speedup |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 4,992 | 136,018 | **27.247x** | 10,276 | 256,330 | **24.945x** |
| 8 | 23,758 | 194,533 | **8.188x** | 53,621 | 395,492 | **7.376x** |

All 23,040 checked coefficients across these four rows have zero mismatches.
These same-run scalar references characterize the standalone test and are not
the end-to-end KEM denominator.

The complete ML-KEM-768 experiment uses KECCAKF, the `smem32` 3+3+1 schedule,
and `NTTMUL.K` in forward and inverse butterflies. The table compares it with
the matching `smem32` C-arithmetic binary from the preceding section:

| Backend | M1 C cycles | M1 `NTTMUL.K` cycles | M1 reduction | M8 C cycles | M8 `NTTMUL.K` cycles | M8 reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SimX | 7,942,788 | 7,849,906 | **1.169388%** | 11,110,749 | 10,616,622 | **4.447288%** |
| RTL | 7,885,868 | 7,791,162 | **1.200958%** | 11,573,280 | 11,070,636 | **4.343142%** |
| XRT/xrtsim | 7,908,765 | 7,791,578 | **1.481736%** | 11,726,329 | 11,049,301 | **5.773572%** |

Every one of the 27 requests across the six runs matches `pk`, `sk`, `ct`,
`ss_enc`, and `ss_dec`, has zero phase status, and records 15 NTT and nine
INTT calls. SimX, RTL, and XRT retire exactly 729,849 instructions at M1 and
5,838,792 at M8, reductions of 4,038 and 32,304 instructions, or 0.550221%,
from the C-arithmetic binaries. SimX versus RTL cycle gaps are **0.753983%** at
M1 and **4.101065%** at M8, both below the 5% parity threshold. XRT differs
from RTL by +0.005339% and -0.192717%, respectively. Against the current XRT
scalar-library denominator, the complete instruction-enabled path is
**1.524124x** faster at M1 and **1.478252x** at M8.

The RV64 W32 arithmetic unit passes the same combined NTTMUL/NTTBF unit test.
RV64 software, the test kernel, and SimX driver also build successfully, but
the full-system run did not produce RV64 correctness or timing evidence. Both
`nttmul_k` and an unrelated RV64 `demo` control stall at command-processor
sequence 26 while
waiting for target 27, so the SimX failure is not specific to this instruction.
The RV64 RTL build stops earlier because the generated rtlsim source list does
not provide `VX_tlb_pkg` before `Vortex.sv` imports it. These are infrastructure
blockers; RV64 parity must be rerun after they are fixed.

Data: [NTTMUL.K validation](../../pqc/results/nttmul_k_validation.csv).
RV32 logs are under `build32_nttmul/nttmul_validation/`; the RV64 build and
blocker evidence are under `build64_nttmul/nttmul_rv64_validation/`. No
synthesis-only measurement isolates `NTTMUL.K`; the post-route result below
measures the combined NTTMUL/SG2 unit integrated into the full core.

## Implemented full-bank W32 cross-lane SG2 butterfly

`NTTBF.CT.K.XORs` and `NTTBF.GS.K.XORs` use `CUSTOM-0` with
`funct7=0x06` and `0x07`. `funct3=s` selects XOR distance `2^s` for
`s=0..4`. For each pair, `a` and `b` come from the pair-low and pair-high
lanes of `rs1`, while the pair-low lane's `rs2` supplies `zeta`. CT computes
`Mont(b*zeta)` once and returns wrapped `a+t` and `a-t`. GS returns
`Barrett(a+b)` to the low lane and `Mont((b-a)*zeta)` to the high lane.
Each lane writes only its own destination register.

The supported contract is W32: `VX_CFG_NUM_ALU_LANES=32`, a converged warp,
and equal active-mask bits within every selected XOR pair. SimX aborts an
unsupported configuration or unmatched pair mask; RTL decode only creates an
NTTBF operation in W32 and its simulation assertion checks the pair mask. The
W4 diagnostic is not a parity case: RTL's generic illegal-instruction path is
transparent rather than trapping after decode suppresses NTTBF.

The ML-KEM register layout remains `r[lane + 32*k]`, with eight coefficients
per lane. Its seven levels use three lane-local levels followed by SG2 at XOR
16, 8, 4, and 2. ML-KEM therefore emits only `s=4,3,2,1`; XOR1 is implemented
and tested for the generic primitive but is not used by ML-KEM. Forward and
inverse functions each contain 32 SG2 instructions, eight at each used
stage.

The standalone program exercises CT and GS at all five distances on 980
boundary vectors and 64 deterministic full-range vectors. Per-lane twiddles
are deliberately different, making accidental use of the pair-high twiddle
observable. SimX and RTL both check 334,080 outputs with zero mismatches and
retire 59,534 instructions. Their run cycles are 578,255 and 571,661, a
**1.153481%** gap. The combined RTL unit also covers consecutive issue,
backpressure, result headers, pair-low twiddle selection, and paired masks at
XLEN=32 and XLEN=64.

The direct transform ablation uses medians over the same five input patterns.
The `SG2+C` rows marked `legacy latency 4` predate the registered reduction and
result pipeline. They remain useful correctness evidence, but their cycles are
excluded from comparisons with the six-cycle full-bank rows.

| W32 implementation | M1 forward | M1 inverse | M8 forward | M8 inverse |
| --- | ---: | ---: | ---: | ---: |
| `reg32+SHFL+C` | 14,175 | 19,812 | 53,025 | 69,248 |
| `reg32+SHFL+NTTMUL.K` | 10,368 | 16,156 | 38,749 | 59,052 |
| `reg32+SG2+C` (legacy latency 4) | 4,012 | 9,073 | 13,370 | 37,981 |
| `reg32+SG2+NTTMUL.K` | **3,648** | **6,213** | **15,499** | **22,803** |
| `smem32+NTTMUL.K` | 4,992 | 10,276 | 23,758 | 53,621 |

All 115,200 coefficient checks represented by these 20 rows match upstream C.
Within the six-cycle full-bank measurements, the combined SG2/NTTMUL path is
the fastest instruction-enabled arm in all four direct cells. The legacy row
cannot establish the incremental cost or benefit of replacing its C arithmetic
with NTTMUL.

The complete KECCAKF-assisted ML-KEM-768 ablation is:

| SimX W32 arm | M1 KEM cycles | Reduction vs `SHFL+C` | M8 batch cycles | Reduction vs `SHFL+C` |
| --- | ---: | ---: | ---: | ---: |
| `reg32+SHFL+C` | 8,074,507 | 0.000% | 11,392,888 | 0.000% |
| `reg32+SHFL+NTTMUL.K` | 7,977,927 | 1.196% | 10,999,052 | 3.457% |
| `reg32+SG2+C` (legacy latency 4) | 7,803,698 | legacy | 10,234,674 | legacy |
| `reg32+SG2+NTTMUL.K` | **7,773,390** | **3.729%** | **10,093,661** | **11.404%** |
| `smem32+NTTMUL.K` | 7,849,906 | 2.782% | 10,616,622 | 6.814% |

The legacy `SG2+C` rows are excluded from contribution decomposition. In the
matched six-cycle data, adding SG2 to `SHFL+NTTMUL.K` reduces complete KEM
cycles by 2.564% at M1 and 8.232% at M8. NTTMUL alone over `SHFL+C` reduces
them by 1.196% and 3.457%, respectively. This paired comparison identifies SG2
as the larger contribution without mixing timing models. The combined arm is
the fastest complete arm at both request counts. All 45 requests across the
five SimX arms match `pk`, `sk`, `ct`, `ss_enc`, and `ss_dec`, and every
request records 15 NTT and nine INTT calls.

The final combination also passes RTL and XRT/xrtsim:

| Backend | M1 final | Reduction vs `smem32+NTTMUL.K` | M8 final | Reduction vs `smem32+NTTMUL.K` |
| --- | ---: | ---: | ---: | ---: |
| SimX | 7,773,390 | 0.974738% | 10,093,661 | 4.925870% |
| RTL | 7,711,801 | 1.018603% | 10,482,710 | 5.310680% |
| XRT/xrtsim | 7,711,455 | 1.028328% | 10,522,959 | 4.763577% |

SimX and RTL retire exactly 728,166 instructions at M1 and 5,825,328 at M8.
Their cycle gaps are **0.798633%** and **3.711340%**, below the 5% parity gate.
XRT differs from RTL by -0.004487% and +0.383956%, and uses the same instruction
counts. Against the current XRT scalar-library denominator, the final path is
**1.539959x** faster at M1 and **1.552191x** at M8. All 27 requests across the
six final SimX/RTL/XRT runs pass the complete KAT. Archived SimX/RTL and XRT
kernel and host binaries have identical SHA-256 hashes.

The catalog now contains a W32 NTTBF model-parity case, a direct SG2 transform
case, M1 and M8 final-combination parity cases, and an XRT case. YAML lint
reports 696 cases in 36 categories. The focused smoke cases, M8 full parity,
direct transform, and XRT case pass; the standalone gap is 1.15%, M1 KEM gap
0.80%, and M8 KEM gap 3.71%.

The full-bank RTL preserved in `9e8284986` instantiates one signed 16-by-16
multiplier per ALU lane.
CT gives only the pair-low multiplier useful work, while GS uses the pair-high
multiplier for its Barrett constant product. The implementation establishes
collective semantics and performance, but it does not yet realize a half-width
physical multiplier bank. The post-route results below show the area and timing
cost of this organization at the target operating point.

Data: [SG2 validation and ablation](../../pqc/results/nttbf_sg2_validation.csv).
Current standalone logs are under `build32_nttmul/nttbf_validation/`; direct,
complete KEM, RTL, and XRT logs and matching binaries are under
`build32_nttmul/sg2_validation/`.

## V80 post-route PPA of the full-bank baseline

The combined NTTMUL/SG2 RTL was implemented as part of the complete RV32 core
on `xcv80-lsva4737-2MHP-e-S` with Vivado 2025.1. All measurements use one
core, eight warps, 32 threads, `EXT_PQC_ENABLE`, the static Keccak AGU, a
250 MHz constraint, and the aggressive performance flow. The matched baseline
predates the NTT changes, and no relevant RTL changed between that baseline and
`d82b2a24f`.

The final implementation command was run from its dedicated Vivado DUT
directory:

```sh
source /data/Xilinx/2025.1/Vivado/settings64.sh
DEVICE=xcv80-lsva4737-2MHP-e-S CLK_FREQ_MHZ=250 \
CONFIGS='-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32' \
make DUT=core
```

| Variant | LUT | FF | BRAM36/18 | DSP | WNS | Estimated Fmax | Vectorless dynamic power |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Keccak/PQC baseline | 334,917 | 265,187 | 113/40 | 160 | 0.000 ns | 250.0 MHz | 8.251 W |
| Baseline + NTTMUL/SG2, DSP Barrett | 355,302 | 271,944 | 113/40 | 224 | -0.669 ns | 214.2 MHz | 8.615 W |
| Baseline + NTTMUL/SG2, shift-add Barrett | 356,768 | 270,242 | 113/40 | 192 | -0.481 ns | 223.2 MHz | 9.122 W |
| Baseline + NTTMUL/SG2, registered reduction/result | 356,447 | 272,430 | 113/40 | 192 | **0.000 ns** | **250.0 MHz** | 8.952 W |
| Final integrated delta | +21,530 (+6.428%) | +7,243 (+2.731%) | 0/0 | +32 (+20.000%) | 0.000 ns | target met | +0.701 W (+8.496%) |

The first two NTT rows retain the structural evidence that led to the final
pipeline. The original DSP Barrett version has 14,884 LUT, 5,115 FF, and 64 DSP
inside the NTT hierarchy. Replacing `quotient * 3329` with the exact
`quotient * (1 + 2^8 + 2^10 + 2^11)` shift-add removes 32 constant-multiply
DSPs but still leaves reduction and the 32-lane ALU result switch on one timing
path. It improves WNS from -0.669 ns to -0.481 ns without closing 250 MHz.

Registering the reduction and result stages closes the complete core at
250 MHz after post-route physical optimization. The final NTT hierarchy uses
15,480 LUT, including 15,450 logic LUT and 30 SRL, 6,319 FF, and 32 DSP. The
full-core delta includes the widened ALU result switch and placement effects;
logic LUT rises by 21,500 (+6.665%), while BRAM is unchanged. The baseline only
establishes closure at 250 MHz, so this is a target-frequency comparison rather
than an unconstrained maximum-frequency sweep.

Before the final post-route `AggressiveExplore` pass, setup WNS was -0.019 ns
and TNS was -4.525 ns; the final setup and hold WNS/TNS are all 0.000. Of the
reported worst 100 setup paths, 20 traverse `nttmul_unit`, 38 traverse local
memory, 26 traverse the dispatcher, eight traverse `pqc_agu`, and four traverse
CSR logic; categories may overlap. The first NTT path is fifth rather than the
global worst path. It runs from `reduction_pipe` to `result_pipe`, has 14 logic
levels, and a 3.928 ns data delay split into 1.295 ns logic and 2.633 ns routing.
The NTT unit therefore no longer monopolizes the core timing endpoints.

Vivado reports 8.952 W vectorless dynamic power and 30.989 W total on-chip
power for the final design, increases of 0.701 W (+8.496%) and 0.703 W
(+2.321%) over the matched baseline. No workload activity file or environmental
constraints were supplied, so these estimates support only a relative
comparison under the same flow.

Data: [V80 post-route PPA](../../pqc/results/ntt_v80_ppa.csv). The final raw
reports and checkpoint are under
`build32_ntt_vivado/hw/syn/xilinx/dut/v80_ntt_reducepipe_250_core/`; the two
failed intermediate structures remain in their separately named directories.

## Decision and verification gates

The measured W32 software route is register-resident state plus SG2, with
`NTTMUL.K` retained for the three lane-local levels and inverse scaling. SG2 is
the main performance contribution, while NTTMUL is the smaller reusable
arithmetic contribution. The half-width experiment below preserves complete
KEM performance and closes 250 MHz with 5,079 LUT and 16 DSP inside the NTT
hierarchy. It is the preferred implementation baseline. Any subsequent
SG4/8/32 fusion should be justified by profiling this lower-area baseline.

1. **Complete:** add the cooperative inverse transform and verify every
   coefficient against upstream C at 1, 4, and 32 lanes, including independent
   concurrent requests and signed boundary inputs.
2. **Complete:** integrate the matched forward/inverse path into the KEM
   experiment, verify complete KATs at one and eight requests, and measure
   batch cycles including per-call lane activation and synchronization.
3. **Complete:** verify BAR-synchronized shared and `smem32` binaries directly
   in SimX/RTL at M1/L32 and M8/L32 with exact instruction parity and cycle gaps
   below 5%, then verify current `smem32` and scalar binaries through XRT. The
   direct measurements satisfy the numeric gate; the catalogued CI case was not
   rerun for this refresh.
4. **Complete:** build direct-shuffle and 3+3+1 W32 register-resident
   forward/inverse controls, verify their generated code and exact KATs, and
   select the 3+3+1 `smem32` implementation as the pre-ISE software
   denominator.
5. **Complete:** the narrow ML-KEM Montgomery opcode, intrinsic, SimX model,
   RTL datapath, unit test, standalone parity test, controlled NTT/INTT
   comparison, complete KAT, and XRT integration are directly validated with
   six-cycle SimX timing.
6. **Complete:** implement CT/GS SG2 at XOR distances 1--16 with explicit W32,
   pair-mask, and pair-low-twiddle rules; verify standalone arithmetic, direct
   forward/inverse transforms, complete M1/M8 KATs, model parity, and XRT.
7. **Complete:** synthesize the current per-lane multiplier bank in the full
   V80 core. It adds 20,385 LUT and 64 DSP, reaches 214.2 MHz, and fails the
   250 MHz setup requirement by 0.669 ns.
8. **Complete:** replace the Barrett constant multiplier with an exact shift-add
   and repeat the full-core run. This removes 32 DSP and reaches 223.2 MHz, but
   still fails the 250 MHz target by 0.481 ns.
9. **Complete:** register the reduction and result stages, update SimX to six
   cycles, and repeat unit, standalone, direct-transform, M1/M8 complete KAT,
   RTL/XRT parity, and matched V80 implementation. The full core closes
   250 MHz with WNS 0.000 ns.
10. **Complete:** statically bank each XOR stage and evaluate the half-width
   SG2 multiplier bank. The NTT hierarchy falls from 15,480 to 5,079 LUT and
   from 32 to 16 DSP; the complete core retains 250 MHz timing closure and
   complete-KEM cycles change by at most 0.041% across the matched backends.

These ML-KEM results do not yet characterize ML-DSA's different coefficient
width and modulus. An instruction intended for both needs that second data
point before its arithmetic format is fixed.

## Reproduction and completed checks

The earlier ablation, arithmetic-sensitivity, and standalone-transform sections
retain their own command logs and manifests. To reproduce the current
BAR-synchronized denominator in a fresh W32 build, initialize the dependencies
from the worktree root:

```sh
git submodule update --init --recursive third_party/softfloat third_party/ramulator \
  pqc/third_party/mlkem-native pqc/third_party/mldsa-native
mkdir -p build32_ntt_repro
cd build32_ntt_repro
../configure --xlen=32 --tooldir=/home/jiangbowang/aphdcode/vortex_v80/toolchains
make -C ../third_party -j4 softfloat ramulator
export CONFIGS="-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32"
make -j4 -C sw/runtime simx stub
```

Rebuild each application arm before comparing SimX and RTL:

```sh
for ntt in coop smem32; do
  make -C tests/pqc/mlkem_profile clean
  make -j4 -C tests/pqc/mlkem_profile KECCAK=pe NTT="$ntt" all
  for requests in 1 8; do
    make -C tests/pqc/mlkem_profile run-simx \
      KECCAK=pe NTT="$ntt" ABLATE= OPTS="-b $requests -t 32"
    make -C tests/pqc/mlkem_profile run-rtlsim \
      KECCAK=pe NTT="$ntt" ABLATE= OPTS="-b $requests -t 32"
  done
done
```

Build the XRT/xrtsim driver once, then rebuild and run the two XRT arms:

```sh
TARGET=xrtsim make -j4 -C sw/runtime/xrt

make -C tests/pqc/mlkem_profile clean
make -j4 -C tests/pqc/mlkem_profile KECCAK=pe NTT=smem32 all
for requests in 1 8; do
  make -s -C tests/pqc/mlkem_profile run-xrt TARGET=xrtsim \
    KECCAK=pe NTT=smem32 OPTS="-b $requests -t 32"
done

make -C tests/pqc/mlkem_profile clean
make -j4 -C tests/pqc/mlkem_profile KECCAK=pe all
for requests in 1 8; do
  make -s -C tests/pqc/mlkem_profile run-xrt TARGET=xrtsim \
    KECCAK=pe NTT= OPTS="-b $requests -t 1"
done
```

The final SG2 combination is built and run with:

```sh
make -C tests/pqc/mlkem_profile clean
make -j4 -C tests/pqc/mlkem_profile \
  KECCAK=pe NTT=reg32 NTTBF=ise NTTMUL=ise all
for requests in 1 8; do
  make -C tests/pqc/mlkem_profile run-simx \
    KECCAK=pe NTT=reg32 NTTBF=ise NTTMUL=ise \
    OPTS="-b $requests -t 32"
  make -C tests/pqc/mlkem_profile run-rtlsim \
    KECCAK=pe NTT=reg32 NTTBF=ise NTTMUL=ise \
    OPTS="-b $requests -t 32"
  make -C tests/pqc/mlkem_profile run-xrt TARGET=xrtsim \
    KECCAK=pe NTT=reg32 NTTBF=ise NTTMUL=ise \
    OPTS="-b $requests -t 32"
done
```

The earlier BAR refresh used the first two groups of direct generated targets.
The final SG2 catalog cases were then linted and run through focused smoke,
M8 full model-parity, direct-transform, and XRT checks. Every supported W32
run passes its arithmetic or complete-KAT check. Exact configuration stamps,
source hashes, binaries, and checksums are in the canonical archive directories
named above. No synthesis flow was part of those functional checks. The V80
synthesis command and its matched result are recorded separately in
[V80 post-route PPA](../../pqc/results/ntt_v80_ppa.csv).

Earlier negative checks reject zero requests, nine requests on eight warp slots,
invalid lane widths, and mismatched host/kernel arms. The deliberately corrupted
arithmetic and inverse-output kernels also fail their expected comparisons.
Those checks were not rerun for this synchronization-only refresh and are not
counted as current BAR CI coverage.

## Half-width multiplier-bank experiment

The full-bank implementation is preserved in local commit `9e8284986`. The
half-width implementation shares 16 signed 16×16 multipliers across W32. CT
uses one beat; GS uses one Montgomery beat and one Barrett-constant beat;
NTTMUL uses one beat per half warp. Static five-way XOR-stage routing replaces
general lane indexing. One saved second beat and one partial-result register
preserve operation order; output backpressure freezes both with the arithmetic
and header pipelines. Instruction encodings and arithmetic are unchanged.

| Operation | Full bank latency / II | Half bank latency / II |
| --- | ---: | ---: |
| W32 CT | 6 / 1 | 6 / 1 |
| W32 GS | 6 / 1 | 7 / 2 |
| W32 NTTMUL | 6 / 1 | 7 / 2 |
| Physical single-lane NTTMUL | 6 / 1 | 6 / 1 |

SimX reserves the second beat separately for each ALU block, without blocking
other ALU PEs solely because the NTT bank is busy. The W32 integrated ALU unit
test now checks 100 mixed requests at both XLEN=32 and XLEN=64. It verifies
CT II=1, GS/NTTMUL II=2, latency 6/7, paired and half-warp masks, complete
headers, and stable outputs under backpressure. A coverage assertion requires
backpressure to occur while `pending_second` is set. Build-local scalar-only
checks also pass 48 requests each with physical one- and four-lane ALUs.
These tests do not establish full-system RV64 support.

The dependent RAW64 probe, running one active lane on the physical W32 bank,
increases from 920/917 SimX/RTL cycles to 984/981. Both retire 146 instructions;
the exact 64-cycle increase confirms the extra cycle for each NTTMUL. The
4,093-pair arithmetic program and 334,080-output butterfly program retain zero
mismatches and their previous instruction counts. Their SimX/RTL cycle gaps
remain 0.978% and 1.153%.

Direct transform intervals are medians of five input patterns:

| Implementation | M1 forward | M1 inverse | M8 forward | M8 inverse |
| --- | ---: | ---: | ---: | ---: |
| Full bank SG2+NTTMUL | 3,648 | 6,213 | 15,499 | 22,803 |
| Half bank SG2+NTTMUL | 3,648 | 6,213 | 15,486 | 22,934 |
| Half bank SG2+C | 3,999 | 8,976 | 13,423 | 38,222 |

The combined half-bank arm changes M8 forward/inverse by -0.084%/+0.574%
relative to the full bank. Within the half-bank model, C arithmetic still wins
the M8 forward-only interval; NTTMUL wins the other three cells. Thus physical
bank savings and the software choice of where to use NTTMUL are separate
questions. The new SG2+C row uses the same CT6/II1 and GS7/II2 model as the new
combined row; it supersedes the legacy latency-four row for this comparison.

Complete KECCAKF-assisted KEM tests use an identical combined-arm kernel binary
in the full- and half-bank runs (SHA-256
`aacef153910fdd99e9d87392011f90ed4242f9bd8fd19bbee09324f026a0bf47`).
The current half-bank SimX/RTL runs match every byte of pk/sk/ct/ss_enc/ss_dec
at M1 and M8. Both retire 728,166 instructions at M1 and 5,825,328 at M8;
cycle agreement is 0.799% and 3.724%, inside the unchanged 5% gate.

The complete-KEM results are:

| Arm / backend | M1 makespan | M8 makespan |
| --- | ---: | ---: |
| Full bank SG2+NTTMUL / SimX | 7,773,390 | 10,093,661 |
| Half bank SG2+NTTMUL / SimX | 7,773,392 | 10,091,206 |
| Full bank SG2+NTTMUL / RTL | 7,711,801 | 10,482,710 |
| Half bank SG2+NTTMUL / RTL | 7,711,801 | 10,481,554 |
| Full bank SG2+NTTMUL / XRT | 7,711,455 | 10,522,959 |
| Half bank SG2+NTTMUL / XRT | 7,711,455 | 10,518,691 |
| Half bank SG2+C / SimX | 7,803,698 | 10,229,210 |

All listed half-bank KATs pass. XRT/RTL makespan differences are -0.004487%
at M1 and +0.354308% at M8. The half bank has effectively unchanged complete
KEM performance: its largest absolute change from the matching full-bank run
is 0.041%. Within the half-bank model, adding NTTMUL to SG2+C reduces KEM
cycles by 0.388% at M1 and 1.349% at M8.

Data: [half-bank functional comparison](../../pqc/results/ntt_halfbank_validation.csv).

Vivado 2025.1 post-route physical optimization closes the half-bank design on
`xcv80-lsva4737-2MHP-e-S` with the same 250 MHz constraint, configuration, and
optimization strategy as the full-bank baseline. Final setup WNS/TNS are
0.000/0.000 ns and hold WHS/THS are +0.010/0.000 ns. The implementation has
zero failed, unrouted, partially routed, or overlapping nets.

| Resource | Full bank | Half bank | Change |
| --- | ---: | ---: | ---: |
| Complete-core LUT | 356,447 | 341,680 | -14,767 (-4.143%) |
| Complete-core FF | 272,430 | 270,865 | -1,565 (-0.574%) |
| Complete-core DSP | 192 | 176 | -16 (-8.333%) |
| Complete-core RAMB36 / RAMB18 | 113 / 40 | 113 / 40 | unchanged |
| NTT hierarchy LUT | 15,480 | 5,079 | -10,401 (-67.190%) |
| NTT hierarchy FF | 6,319 | 5,040 | -1,279 (-20.241%) |
| NTT hierarchy DSP | 32 | 16 | -16 (-50.000%) |

These savings reflect both multiplier sharing and static XOR-stage routing.
The whole-core delta includes integration and placement effects. Relative to
the Keccak/PQC core without NTT instructions, the half-bank design adds 6,763
LUT (+2.019%), 5,678 FF (+2.141%), 16 DSP (+10%), and no BRAM. All reported
PPA covers the single-core DUT. The frequency result establishes closure at
the 250 MHz target; it is not a maximum-frequency sweep.

None of the final 100 worst setup paths traverses `nttmul_unit`, compared
with 20 paths in the full-bank report. The global first path runs from the
Keccak PQC AGU base register to the LSU scheduler request RAM and has 3.913 ns
data delay. The report does not separately measure the NTT-specific worst path.

Vectorless dynamic power is 8.297 W, down 0.655 W (-7.317%) from the full bank;
total on-chip power is 30.332 W, down 0.657 W (-2.120%). These are relative
estimates from the same flow without a workload activity file. Final data is
in [V80 post-route PPA](../../pqc/results/ntt_v80_ppa.csv).

The half-width bank with static stage routing is retained as the implementation
baseline: it materially reduces resources, preserves 250 MHz timing closure,
and leaves measured complete-KEM performance effectively unchanged.

Functional logs, binaries, source hashes, and the exact working-tree patch are
under `build32_nttmul/halfbank_validation/`. The separate scalar RTL checks are
under `build32_ntt_bank_audit/scalar_l{1,4}_rtl.log`. The new physical reports
are under `build32_ntt_vivado/hw/syn/xilinx/dut/v80_ntt_halfbank_250_core/`.
