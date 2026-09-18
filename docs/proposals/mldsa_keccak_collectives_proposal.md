# ML-DSA Keccak collective integration

## Scope

Connect the existing SG25 shuffle, Stage and Round permutations to the
ML-DSA-65 native FIPS-202 x1 hook. Keep the library's absorb/squeeze,
incremental contexts, polynomial arithmetic, signing rejection loop and
verification unchanged. A state is loaded into lane registers for one complete
permutation and stored back before returning. This is a permutation-hook
integration, not the full register-resident sponge used by ML-KEM.

All arms use one leader per request, one warp per CTA, a private arena and
private counters. The leader publishes its state pointer, expands to all
32 lanes through an explicit stack-safe trampoline, and restores leader
execution after the worker has completed. Lanes 25--31 participate with zero
state and do not access the caller's 25-word buffer.

The profile host gains a batch count and deterministic input seed. Input zero
preserves the historical seed/rnd/message. Subsequent input IDs generate
separate reproducible inputs; every request is compared with portable host C,
including the exact signature. No upstream library or RTL change is planned.

## Validation

1. Build strict RV32IM W8T32 with F/D disabled and software NTT. Disassemble
   the collective path and verify register dependencies and full-mask issue.
2. Compare C, shuffle, expanded Stage, Round and Pointer where measured on
   the same core configuration. Check return codes, pk/sk/signature bytes,
   call counts, arena/stack bounds and device backend stamps.
3. Run Stage/Round through XRT integration simulation on multiple input IDs
   and concurrent requests; compare identical ELFs with SimX without widening
   timing tolerances.
4. Archive exact commands, configuration, binary/runtime hashes and raw logs.
   Preserve prior CSVs and paper sources. Register functional integration
   coverage in the PQC test catalog.

Measurements belong to this new hook-level family; do not combine them with
historical W4T4 ML-DSA or ML-KEM's custom sponge measurements.

## Allocator defect exposed by multiple inputs

The old arena ignored every free. Input ID 1 fails in the C backend even with
one request: signing returns `MLD_ERR_OUT_OF_MEMORY`, three allocations fail,
and verification has no valid signature. The failure also occurred for IDs
2, 4, 5 and 7 in the first eight-request Stage run. It is independent of the
collective instructions and concurrency.

The pinned library allocates seven temporary objects inside
`mld_attempt_signature_generation` and frees them in reverse order on every
exit, including rejection. Its other custom allocation sites also use reverse
order. The arena now rounds each frame to 32 bytes and reclaims it on free;
an unexpected release order increments the failure counter. Arena capacity is
unchanged. The library still zeroizes each object before invoking the free hook.
The fixed-input cumulative allocation peak must not be described as the live
working set: full-RAM input 0 now peaks at 67,648 bytes, versus the old
86,912-byte cumulative peak.

A host harness compiles the actual allocator and pinned ML-DSA source with
mock hart IDs and AddressSanitizer/UndefinedBehaviorSanitizer. Both full- and
low-RAM modes pass 10,000 nested reuses, non-aligned sizes, allocation failure
and cleanup, and 64 keypair/sign/verify cases with byte-exact portable-C
references. Device runs independently validate the warp-indexed arena and the
real scalar-to-collective execution path.

## Measurement protocol

All performance arms use strict RV32IM, F/D disabled, one core, eight warps,
32 threads, full RAM, software polynomial arithmetic, and the same runtime
with Pointer, Stage, Round and NTT hardware enabled. NTT hardware is present
but unused by ML-DSA. Stage and shuffle unroll the 24-round loop; Round emits
24 explicit rounds. The C baseline uses the upstream portable permutation.

Each request occupies one warp and initializes all lane stacks before the
leader enters ML-DSA. A core-scoped barrier precedes timing and another follows
the last timestamp, preventing stack painting or result checks from overlapping
other requests. Per-request cycles cover keypair, signing and verification;
batch makespan is the latest end minus the earliest start. These are device
kernel intervals, not host transfer or wall-clock latency. Runtime `PERF`
counts cover the whole launch, including initialization and diagnostics.

`-b N -s S` supplies distinct deterministic inputs S through S+N-1. A mixed-input
M8 run is not a strong-scaling comparison against eight repetitions of input 0.
The small sample validates behavior across different rejection paths; it is not
a signing-latency distribution or tail-latency claim.

Functional Stage/Round cases are registered in `ci/testcases/pqc.yaml` for
SimX and XRT with `-b 2 -s 1`. Those catalog cases use the regular CI toolchain
and its default F setting; the strict-IM measurements use the local soft-float
libc/libcrt paths recorded below. No CI tolerance or golden baseline is changed.

<!-- generated-results -->

## Current validation results

Status: **complete**. Only completed, byte-verified runs enter the CSV.
XRT results are full AFU integration simulation; SimX rows are not RTL measurements.

| Backend | SimX M1 kernel cycles | XRT M1 kernel cycles | XRT speedup over C |
|---|---:|---:|---:|
| c | 145,738,149 | 148,328,852 | 1.0000x |
| shuffle | 69,244,167 | 68,837,420 | 2.1548x |
| stage | 57,623,356 | 57,215,163 | 2.5925x |
| round | 56,993,917 | 56,583,893 | 2.6214x |
| pointer | 55,049,083 | 54,666,985 | 2.7133x |

Stage and Round pass SimX M8 with input IDs 0--7, and both pass M2 with IDs 1--2.
Stage low-RAM M2 also passes; its arena peak is 14,624 B of 24,576 B.
The lane-request ML-DSA baseline passes its two-request SimX regression.
XRT runs completed: 7/7; pending: 0; failed: 0.

| Arm / batch / first input | Exact retired instructions | Launch cycle gap vs XRT |
|---|---|---:|
| c / 1 / 0 | True | 1.736% |
| pointer / 1 / 0 | True | 0.718% |
| round / 1 / 0 | True | 0.743% |
| round / 2 / 1 | True | 0.647% |
| shuffle / 1 / 0 | True | 0.607% |
| stage / 1 / 0 | True | 0.732% |
| stage / 2 / 1 | True | 0.650% |

All seven measured launch-cycle gaps are below the 5% parity threshold.

Results: `pqc/results/mldsa_keccak_collectives.csv`. Status, configuration, hashes and model comparisons: `pqc/results/mldsa_keccak_collectives_status.json`.
Raw logs, source snapshot, implementation patch, host allocator harness and reproduction commands: `pqc/results/mldsa_keccak_collectives_sources.zip`.
The original and DAC paper sources are unchanged. No synthesis, commit or push is performed by this experiment.
