# Shared NTT primitive and joint Keccak evaluation

## Question and controls

Measure the shared K/D NTT unit against the actual W32 register/shuffle
implementation, and measure its incremental and joint contribution with Stage
Keccak in complete ML-KEM-768 and full-RAM ML-DSA-65 requests.

Use RV32IM, F/D disabled, one core, eight warps of 32 lanes, and two shared
multiplier lanes. Enable the same execution units in every simulation. Keep
cooperative polynomial arithmetic outside NTT in software in all four arms:

| Arm | Keccak | NTT/INTT |
| --- | --- | --- |
| A | Unrolled shuffle | Register/shuffle, software Montgomery |
| B | Unrolled Stage | Register/shuffle, software Montgomery |
| C | Unrolled shuffle | Shared NTTMUL/NTTBF |
| D | Unrolled Stage | Shared NTTMUL/NTTBF |

The headline comparisons are A/D (joint), A/B (Keccak first), B/D (NTT
after Keccak), and A/C (NTT first). Inputs, other arithmetic, runtime,
memory configuration, and measurement scope must match. Report M1 latency
and M8 batch makespan separately. Do not multiply ratios from older Pointer
or K-only cohorts into these results.

## Primitive measurement

Use the same transform functions as the complete applications. Test both
directions and five deterministic input patterns, including boundary and
sparse vectors. Keep input setup, reference execution, comparison, and validation-buffer
stores outside the measured transform interval. Synchronize all resident
warps before and after it. Report software/hardware ratios of median batch
intervals across the same inputs at one and eight warps. Scalar references
run in separate launches and check every coefficient.

## Verification and provenance

Run identical binaries through SimX and XRT/xrtsim. Require byte-exact
application KATs and exact retired instruction agreement; retain the existing
5% model-parity bound. Log interval parity separately from full-launch parity.
Archive commands, source/binary/runtime hashes, raw logs and derived CSVs.

Instrument separate M1 A/B/D application builds to measure permutation,
NTT/INTT and remaining work, and quantify probe overhead against headline
runs. ML-DSA absorb/squeeze remain part of the residual unless explicitly
instrumented. No synthesis is needed because this experiment changes software
and measurement only; existing M2 PPA remains a separate physical cohort.

## Interpretation

Use complete-request A/D as the headline acceleration relative to cooperative
software. A/B followed by B/D is a valid incremental decomposition within this
cohort: their product equals A/D. Primitive speedups describe the transform
interval itself, not the full cryptographic request. Report them alongside,
rather than substituting them for, the complete-request result.

A small complete-request NTT gain does not by itself imply a weak NTT unit.
For a software transform fraction p and isolated speedup s, Amdahl's estimate
is 1 / (1 - p + p/s). For example, p=7% and s=4 would save 5.25% of request
cycles. This example is illustrative; the measured A/B/D profiles must decide
whether NTT actually becomes dominant after Keccak acceleration. Library-hook
and warp-transition costs, cache effects, and instrumentation prevent treating
an isolated speedup as an exact prediction of the application result.

## Measurement implementation

`tests/pqc/shared_ntt_xn` builds either K or D and calls the same transform
functions used by the applications. The ML-DSA transform body was extracted
unchanged into `tests/pqc/mldsa_ntt_reg32.h`; its profile-specific dispatch
remains in `mldsa_profile/mld_ntt_reg32.h`. The primitive timing includes the
transform's input loads and output stores and a trailing CTA synchronization;
input generation, scalar reference, validation and diagnostic output are outside
it. The five inputs are correctness cases, not five independent statistical
trials; report their median and preserve every sample.

ML-KEM's complete-request harness now places global barriers before its first
timestamp and after its last timestamp, matching the existing ML-DSA harness.
Previously, setup and counter writeback in another resident warp could overlap
a timed request. The new measurements use the corrected boundary in every arm.
ML-DSA M1 uses input 0; M8 uses inputs 1 through 8. Compare configurations within
each batch size; do not interpret their ratio as scaling the same input.

Headline builds omit `PROFILE_PHASES`; existing library-hook call counters and
ML-DSA pointwise timers remain enabled identically in every arm. The reported
probe overhead is the additional cost of enabling `PROFILE_PHASES`, not the
cost of removing every existing measurement hook. Verify identical per-request
primitive call counts across all arms and both simulators.

The primitive log's legacy `speedup=` field compares its scalar reference
interval with its cooperative transform interval. It is **not** the hardware
ISE gain. `collect_ntt_joint.py` derives the reported ISE speedup by comparing
`coop` intervals from the separate W32 software and hardware builds.

### Compiler effects

The baseline is the current production W32 implementation, not a claim of an
optimal, spill-free assembly implementation. Disassembly of the standalone D
transform shows a 96-byte software frame and temporary stack accesses inside
the transform; the hardware build uses a 32-byte frame containing callee-save
registers. The observed ISE benefit therefore includes reduced register
pressure and resulting memory traffic, as well as fewer arithmetic/shuffle
instructions. Do not attribute the entire speedup solely to multiplier latency.
The archived disassemblies make this distinction inspectable.

## Local timing-model discrepancy

The D software inverse transform at M8 has median intervals of 148,099 SimX
cycles and 158,177 XRT cycles: SimX underestimates this interval by 6.371%.
All five XRT samples fall between 157,522 and 158,903 cycles; the difference
is not explained by taking a different median. The other 15 primitive interval
pairs are below 5%. This local discrepancy must not be hidden by the complete
launch's scalar references and setup work, whose aggregate parity passes.
Its root cause has not been traced. No timing-model tolerance was changed.

The collector archives the results and returns a nonzero status for this
interval outlier, even if every complete-launch parity check passes. Report
XRT measurements as the performance evidence; do not claim that the new
primitive intervals all meet the 5% timing-model bound.

## Reproduction

From the configured `build32_ntt_joint/` directory:

```sh
python3 ../pqc/results/run_ntt_joint.py build
python3 ../pqc/results/run_ntt_joint.py run --workers=12
python3 ../pqc/results/collect_ntt_joint.py ntt_joint
```

The runner records the exact build options and rebuilds both simulator cores.
It reuses the validated API wrapper libraries from
`build32_ntt_bank2/bank2/runtime`; the same three wrapper binaries are included
in the resulting source archive. Soft-float libc/libcrt are selected from the
sibling `toolchains-im` tree. Each arm has its own generated application build
directory, while every arm uses the same pair of simulator cores. Performance
is reported in cycles, without converting it using a synthesis clock or mixing
it with a different PPA configuration.

## Completed XRT results

All results below use the same RV32IM W8T32 M2 core. A/B/C/D are defined above.

### Complete-request cycles

| Scheme | Requests | A: software/software | B: Stage/software | C: software/NTT ISE | D: Stage/NTT ISE |
| --- | ---: | ---: | ---: | ---: | ---: |
| ML-KEM-768 | 1 | 8,283,170 | 5,658,327 | 7,972,346 | 5,343,430 |
| ML-KEM-768 | 8 | 22,892,221 | 13,761,563 | 21,616,943 | 12,570,125 |
| ML-DSA-65 | 1 | 42,742,735 | 31,157,580 | 40,866,316 | 29,301,732 |
| ML-DSA-65 | 8 | 155,708,685 | 99,230,027 | 141,011,148 | 84,184,362 |

### Matched incremental and joint speedups

| Scheme | Requests | Keccak first (A/B) | NTT after Stage (B/D) | Joint (A/D) |
| --- | ---: | ---: | ---: | ---: |
| ML-KEM-768 | 1 | 1.464x | 1.059x | 1.550x |
| ML-KEM-768 | 8 | 1.663x | 1.095x | 1.821x |
| ML-DSA-65 | 1 | 1.372x | 1.063x | 1.459x |
| ML-DSA-65 | 8 | 1.569x | 1.179x | 1.850x |

NTT reduces Stage-based request cycles by 5.565% / 8.658% for ML-KEM
(M1 / M8), and 5.956% / 15.162% for ML-DSA. These are cycle reductions,
not percentage increases in throughput.

### Isolated transform intervals

Values are median batch intervals across the five deterministic inputs.

| Scheme | Requests | Direction | W32 software cycles | Hardware cycles | Speedup |
| --- | ---: | --- | ---: | ---: | ---: |
| ML-KEM-768 | 1 | forward | 14,646 | 3,760 | 3.895x |
| ML-KEM-768 | 1 | inverse | 20,418 | 6,345 | 3.218x |
| ML-KEM-768 | 8 | forward | 60,818 | 17,683 | 3.439x |
| ML-KEM-768 | 8 | inverse | 76,536 | 26,373 | 2.902x |
| ML-DSA-65 | 1 | forward | 25,324 | 7,760 | 3.263x |
| ML-DSA-65 | 1 | inverse | 32,966 | 8,618 | 3.825x |
| ML-DSA-65 | 8 | forward | 119,628 | 45,572 | 2.625x |
| ML-DSA-65 | 8 | inverse | 158,177 | 46,652 | 3.391x |

### M1 profile and interpretation

| Scheme | Arm | NTT + INTT cycles | NTT + INTT share | Added profile overhead |
| --- | --- | ---: | ---: | ---: |
| ML-KEM-768 | A | 441,812 | 4.822% | 10.610% |
| ML-KEM-768 | B | 439,536 | 7.261% | 6.975% |
| ML-KEM-768 | D | 134,633 | 2.344% | 7.476% |
| ML-DSA-65 | A | 2,712,458 | 6.312% | 0.535% |
| ML-DSA-65 | B | 2,698,151 | 8.611% | 0.567% |
| ML-DSA-65 | D | 838,322 | 2.845% | 0.558% |

The transforms accelerate substantially, but do not become the dominant
component of the instrumented M1 request after Stage Keccak: their shares
are 7.261% and 8.611%. Hardware reduces these to 2.344% and 2.845%.
ML-DSA B-to-D transform time falls by 1,859,829 profiled cycles, consistent
with the 1,855,848-cycle reduction in the separate headline runs. The result
supports incremental acceleration of the remaining transform work, rather
than a claim that NTT necessarily becomes the primary bottleneck. M8 has
different contention and, for ML-DSA, different inputs; its larger gain must
not be explained solely using the M1 percentages.

### Validation and archived evidence

All 60 executions pass coefficient checks or complete-request byte-exact
reference checks. The primitive tests check 92,160 coefficients per simulator.
The 30 complete-launch SimX/XRT pairs retire identical instruction counts;
maximum device-cycle gap is 3.655%.
The separate primitive intervals pass 15/16 checks at 5%; the D software M8
INTT outlier remains open as documented above. The collector therefore
returns 1 after writing all results, rather than silently accepting that gap.

- [Raw run summary](../../pqc/results/ntt_joint_runs.csv)
- [Primitive results](../../pqc/results/ntt_joint_primitive.csv)
- [Joint ablation](../../pqc/results/ntt_joint_request.csv)
- [Stage profiles](../../pqc/results/ntt_joint_profile.csv)
- [Complete-launch parity](../../pqc/results/ntt_joint_parity.csv)
- [Primitive interval parity](../../pqc/results/ntt_joint_primitive_parity.csv)
- [Raw logs, source snapshots, binaries and reproduction scripts](../../pqc/results/ntt_joint_sources.zip)

The source archive can be unpacked and its collector rerun without either
simulator. Run `python3 source/pqc/results/collect_ntt_joint.py raw --output replay`
from the extraction directory; the expected exit status is 1 for the recorded
interval outlier, and the seven regenerated CSVs must match `tables/` byte-for-byte.
