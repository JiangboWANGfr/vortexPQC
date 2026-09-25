# Bottlenecks after the warp matrix sampler

These measurements use RV32IM, one core, W8T32, F/D disabled, two shared NTT
multiplier lanes, `KECCAK=sg25 UNROLL=1 NTT=reg32 NTTMUL=ise NTTBF=ise`.
ML-KEM also uses `SERIAL=1 ARITH=all ARITH_MUL=ise`; ML-DSA uses
`POINTWISE=ise POINTWISE_L5=w32 MLDSA_RAM=full`. The starting point is the
`SAMPLER=warp` implementation and paired results in
[`../sampler_warp/`](../sampler_warp/README.md). No hardware or paper source
was changed for this experiment.

The post-sampler probes use the archived library instrumentation in
[`../bottleneck_profile/probe_sources.zip`](../bottleneck_profile/probe_sources.zip)
and the current sampler/profile hooks. Raw logs are in this directory. All
probes and complete requests passed the portable-C byte-exact checks. Probe
timers change code generation, so diagnostic percentages below use each
instrumented request as denominator; complete-request performance comes only
from the uninstrumented [`request_cycles.csv`](request_cycles.csv).
The exact hook sources, probe kernel, and device binaries are archived in
[`sources_and_binaries.tar.gz`](sources_and_binaries.tar.gz), with hashes in
[`artifact_sha256.txt`](artifact_sha256.txt). The runtime library hashes are
the same as the preceding [sampler experiment](../sampler_warp/runtime_sha256.txt).

| Instrumented request | Total cycles | Measured interval | Cycles | Share |
|---|---:|---|---:|---:|
| KEM-768, warp sampler | 4,368,081 | matrix rejection excluding SHAKE | 736,315 | 16.86% |
| KEM-768, warp sampler | 4,368,081 | noise excluding SHAKE | 557,838 | 12.77% |
| KEM-768, warp sampler | 4,368,081 | pack/unpack | 625,230 | 14.31% |
| KEM-768, warp sampler | 4,368,081 | SHAKE absorb and squeeze excluding permutation | 704,740 | 16.13% |
| DSA-65/input 3, warp sampler | 22,519,186 | x4 SHAKE squeeze excluding permutation | 4,673,860 | 20.75% |
| DSA-65/input 3, warp sampler + warp extraction | 18,342,321 | x4 SHAKE squeeze excluding permutation | 464,632 | 2.53% |

The matched scalar-to-warp sampler probe reduces KEM matrix rejection outside
SHAKE from 2,028,562 to 736,315 cycles and DSA matrix uniform sampling
outside SHAKE from 6,399,187 to 1,468,590 cycles. This is why the x4 output
copy becomes visible in DSA after sampling is accelerated.

The DSA x4 squeeze copies four 136/168-byte outputs a byte at a time on the
leader. A native x4 extraction hook now distributes these copies across the
32 existing warp lanes while retaining the upstream state and byte order.
The matched diagnostic x4 interval falls by 90.06%. This is a software
mapping; it adds no instruction, RTL unit, or synthesis area. The hook is
selected with `SHAKE_EXTRACT=warp` and guarded by the device/host arm check.
The uninstrumented ML-DSA builds used the generated Makefile under
`build32_im/tests/pqc/mldsa_profile`, copied into separate parameter build
directories, with `PARAM=44|65|87 KECCAK=sg25 UNROLL=1 NTT=reg32`
`NTTMUL=ise NTTBF=ise POINTWISE=ise POINTWISE_L5=w32 MLDSA_RAM=full`
`SAMPLER=warp SHAKE_EXTRACT=warp`. `CONFIGS` enabled PQC, NTT, KSG25,
KROUND25, W8T32, two NTT multiplier lanes, and disabled F/D. The same
`parameter_sweep/runtime` libraries as the preceding sampler experiment were
used for SimX and XRT (`XRT_DEVICE=xrtsim`).

Complete-request SimX results with the existing warp sampler are:

| Request | Sampler only | Sampler + extraction | Incremental speedup |
|---|---:|---:|---:|
| DSA-44, input 3, M1 | 35,021,770 | 31,051,115 | 1.128x |
| DSA-65, input 1, M1 (8 signing attempts) | 39,130,764 | 34,053,694 | 1.149x |
| DSA-65, input 2, M1 (7 attempts) | 36,301,550 | 31,368,131 | 1.157x |
| DSA-65, input 3, M1 (2 attempts) | 22,306,946 | 18,092,872 | 1.233x |
| DSA-87, input 3, M1 | 29,602,319 | 21,630,264 | 1.369x |
| DSA-65, inputs 3–10, M8 makespan | 76,416,219 | 71,289,780 | 1.072x |

For the M8 pair, retired instructions fall from 23,716,844 to 20,219,488
(14.75%) while makespan falls 6.71%. The single-request speedup therefore
must not be substituted for batch throughput.

The DSA-65/input-3 M1 XRT run passes the same byte-exact reference and takes
17,954,231 request cycles. Its matched SimX run takes 18,092,872 cycles;
both retire exactly 1,510,539 instructions. The request-cycle difference is
0.772% of XRT and the whole-run PERF-cycle difference is 0.827%, within the
5% parity threshold. Against the preceding warp-sampler XRT result
(22,174,120 cycles), warp extraction gives 1.235x complete-request speedup.
The raw XRT log is [`d65_extract_m1_s3_xrt.log`](d65_extract_m1_s3_xrt.log),
and the paired counts are in [`model_parity.csv`](model_parity.csv).
The optimized DSA-65 M8 XRT run also passes all eight byte-exact requests:
71,775,799 makespan cycles, exactly 20,219,488 retired instructions in both
models, and 0.677% makespan / 0.652% PERF-cycle differences from SimX. Its
same-input warp-sampler baseline also passes all eight requests at 76,954,694
XRT cycles, so extraction improves RTL batch makespan by 1.072x (6.73%).
That baseline retires exactly 23,716,844 instructions in both models and has
a 0.700% SimX/XRT makespan difference. Both configurations satisfy the 5%
model-parity threshold; neither required a timing-model tolerance change.

After extraction, DSA-65/input 3 spends 1,469,356 instrumented cycles in
matrix uniform sampling excluding SHAKE (8.01% of its 18,342,321-cycle
probe). The exclusive sign-body intervals for decompose/pack `w1`, compute and
check `z`, and hint processing sum to 5,053,518 cycles (27.55%). For input 1
with eight signing attempts, those sign-specific intervals total 14,663,792
cycles (42.56% of its 34,451,601-cycle probe). Signing attempts are inclusive
intervals and must not be added to the exclusive intervals. This shifts the
remaining DSA priority to signing work, while KEM has several comparable
smaller costs. The evidence does not justify a new shared KEM/DSA instruction
or more NTT multipliers at this stage. Even a zero-cost x4 extraction would
improve the instrumented DSA-65/input-3 request by at most 1.026x from its
current 464,632-cycle interval; the selected serial KEM path does not call
the x4 extraction hook. Likewise, completely eliminating the remaining
non-SHAKE rejection/uniform-sampler interval would bound single-request
speedup at 1.203x for KEM-768 and 1.087x for DSA-65/input 3, before any
instruction's own cost or batch-throughput effects.
