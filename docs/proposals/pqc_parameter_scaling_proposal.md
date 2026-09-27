# Parameter coverage, occupancy, and fixed-W8 batch throughput

## Scope and controls

Extend the matched Stage/NTT experiment to ML-KEM-512/768/1024 and
ML-DSA-44/65/87. All runs use the same archived RV32IM W8T32 core with
F/D disabled and two shared NTT multiplier lanes. No RTL or synthesis change
is needed. This is a new software cohort; do not combine its cycles with
`ntt_joint_request.csv` because the request scheduler has changed.

Keep arms A/B/C/D from `ntt_joint_ablation_proposal.md`. Add arm E: Stage and
hardware NTT/INTT plus all currently implemented arithmetic mappings
(`ARITH_MUL=ise` for KEM, `POINTWISE=ise` for DSA). This is not a claim that
all polynomial arithmetic or the complete cryptographic request is hardware.
D/E isolates the additional arithmetic mapping; A/E is the combined result.

## Three measurements

1. Parameter coverage: all six sets, all five arms, one and eight requests.
2. Occupancy: keep the same eight input requests, assign them to 1/2/4/8
   resident warp workers on the unchanged W8 core. Also retain latency-style
   runs with 1/2/4/8 requests and the same number of workers.
3. Batch throughput: 8/16/32/64 requests, always eight resident warp workers,
   one kernel launch. Report requests per million device cycles and cycles
   per request, not host simulator execution time.

Each worker executes request IDs `worker + wave * workers`. Every wave is
bracketed by resident-worker barriers so setup and result writes cannot
contaminate another worker's per-request interval. The whole-batch interval
is earliest request start to latest request end: inter-wave synchronization,
setup and accounting are included. Initial setup and final diagnostics are
excluded. This is synchronous wave scheduling, not a dynamic work queue or
proof of maximum achievable throughput. Requests must be a multiple of the
worker count, which avoids partial-wave barrier deadlock.

DSA uses deterministic input IDs starting at 1 for every batch, consistently
across arms and simulators. The fixed-eight-input occupancy sweep separates
concurrency from changing signing-rejection inputs. Larger batches extend the
same prefix; preserve call counts to expose workload variation. KEM uses the
upstream parameter-specific known-answer vectors with fixed coins.

## Parameterization

The profile Makefiles accept `PARAM=512|768|1024` and `PARAM=44|65|87`.
KEM scratch offsets derive from the selected API sizes. Vector arithmetic
uses K=2/3/4 and L=4/5/7 rather than fixed middle-set dimensions. The DSA host
oracle selects the same parameter set but remains independent of device
hooks. Existing `POINTWISE_L5=w32` and counter labels are retained for script
compatibility; the mapped accumulation now uses the selected L, not always 5.

The 1024 KEM and 87 DSA arenas have separate larger budgets; default 768/65
budgets are unchanged. Verify allocation failures, stack watermarks, and
multiple signing inputs before making performance claims. Compare all output
bytes with the reference, not merely successful encapsulation or verification.

## Execution and provenance

Use the isolated `build32_pqc_parameters` tree. Reconfigure before building.
The simulator libraries are copied from the completed joint experiment and
verified against its manifest. Source snapshots, commands, kernel/host hashes,
runtime hashes, exit status, raw logs, and derived tables are retained under
`parameter_sweep`. The runner supports resumable jobs, accepting an old result
only when its command, binaries, runtime, and log hash match.

Example commands, from the configured build tree:

```sh
python3 ../pqc/results/run_pqc_parameters.py build
python3 ../pqc/results/run_pqc_parameters.py run --arms E --batches 1 --drivers simx
python3 ../pqc/results/run_pqc_parameters.py run --batches 1 2 4 8
python3 ../pqc/results/run_pqc_parameters.py run --batches 8 --resident 1
python3 ../pqc/results/run_pqc_parameters.py run --batches 8 --resident 2
python3 ../pqc/results/run_pqc_parameters.py run --batches 8 --resident 4
python3 ../pqc/results/run_pqc_parameters.py run --batches 16 32 64 --resident 8
```

The complete paper queue is `run_pqc_parameters.py run --suite`. It plans
156 runs per simulator: 60 parameter/arm runs (six sets, five arms, B1/B8),
24 additional B2/B4 A/E runs, 36 fixed-eight-input occupancy A/E runs, and
36 B16/B32/B64 A/E runs. Additional validation jobs remain in the raw table
but are counted separately from this planned coverage.

The detached supervisor in `build32_pqc_parameters/parameter_sweep/campaign.py`
waits for initial validation, then runs the SimX and XRT queues with four and
eight workers respectively. It refreshes `pqc/results/parameter_scaling/`
status and tables every minute, archives completed measurements, and produces
occupancy/batch plots only when every required XRT point exists. Simulator
host parallelism changes wall-clock execution time, not simulated hardware.

Use XRT/xrtsim for reported RTL performance. Check exact instruction parity
and the existing 5% full-launch cycle tolerance against SimX. Report any
outlier; never widen the tolerance. The older isolated D software INTT M8
outlier remains open and is not resolved by this parameter expansion.

## Status

The parameterized applications and resident-wave scheduler build for all
30 parameter/arm combinations, and the static instruction audit passes.
The 200-MHz V80 sweep has completed all 120 A/E parameter/concurrency cells:
one warmup and five timed runs per cell, byte-exact KATs, and matching
algorithm call counts across arms and repetitions. Its raw run archive,
medians, occupancy and batch plots, clock checks, and source hashes are in
`pqc/results/v80_hw_validation/parameter_board_200mhz/`. Across six sets,
A/E speedup is 1.454--1.552x for one request and 1.677--1.832x for eight.
The resident image was reused without programming; VRT metadata does not
independently attest its PDI identity.

The initial XRT/SimX queue completed all 312 planned runs, with 156 paired
instruction matches and no 5% timing-parity failures. Its archived software
does not include the later warp sampler, codec/noise/linear mapping for KEM,
or the SHAKE extraction and signing-arithmetic mapping for DSA. It must not
be called the final software implementation or combined with the separately
optimized middle-parameter board ablations.

## Complete-software-mapping cohort

Use `build32_pqc_parameters_full_wt` and `run_pqc_parameters.py --full-mapping`.
Every A--E arm enables KEM `SAMPLER=warp CODEC=all NOISE=warp LINEAR=warp
ZEROIZE=warp`, or DSA `SAMPLER=warp SHAKE_EXTRACT=warp SIGN_ARITH=warp
ZEROIZE=warp`. All other arm differences remain as defined above. The build
audits the required software macros as well as the custom opcodes, and
rebuilds both simulator runtimes with matching hardware flags, including
`VX_CFG_DCACHE_WRITEBACK=0` to match the board's write-through core.

The opcode audit found that the earlier KEM `LINEAR=warp` implementation
unconditionally used NTTMUL.K for conversion to Montgomery form, including
the nominal software arms. The complete-mapping cohort makes that operation
obey `ARITH_MUL=ise`; A/B must contain zero NTTMUL/NTTBF instructions, and
D/E now includes this conversion in its arithmetic comparison. Archived
middle-parameter binaries retain their original behavior for reproduction.

First replay the archived KEM-768 A/D and DSA-65 A/E board binaries at their
original one/eight-request inputs, including DSA input start 3. Then run
all six parameters and five arms at one/eight requests with input start 1,
followed by A/E occupancy and fixed-W8 batches through 64 requests. Each
board cell uses a warmup and five measurements at the read-back 200-MHz
clock. Byte-exact KATs, DSA arena checks and equal algorithm call counts
are required. Preserve input IDs, binary/runtime hashes and all raw logs;
DSA rejection variation prevents mixing different input cohorts.

Store the new simulation results in `pqc/results/parameter_scaling_full/`
and board results in `pqc/results/v80_hw_validation/parameter_full_board_200mhz/`.
The original results and manuscript numbers remain identifiable until the
new cohort has passed validation. No RTL or synthesis change is required.

The initial complete-mapping simulation in `build32_pqc_parameters_full`
inherited the older write-back cache configuration. K512 at eight workers
hit RTL scoreboard timeouts, while its one-worker B/D/E cases had identical
retired instruction counts but 7.1--7.5% model-cycle gaps. That queue was
stopped, and its data is retained in
`pqc/results/parameter_scaling_full_writeback_diagnostic/`. This is an open
write-back diagnostic, not a passed verification cohort; switching the main
experiment to the actual board configuration does not resolve that issue.

The fresh write-through runtime initially stalled as well. Tracing localized
this to XRT's AXI memory model sampling request signals after the active edge:
write-credit gating could remove a just-accepted write before the model saw
it, losing its completion. Capturing requests before the edge restores the
`fence` regression and K512 E single-request execution (identical instructions,
4.373% device-cycle gap). The independent CI case and failure evidence are in
`pqc/results/xrt_axi_sampling/`. The complete-mapping simulation rebuild uses
this correction; watchdog and parity thresholds are unchanged. The original
write-back measurements remain a failed diagnostic until separately rerun.

### Complete-mapping board results

The 156 board cells completed with 780 timed runs and 156 warmups, all
byte-exact KATs passing. The archived middle-parameter binaries reproduced
their earlier median cycles within 0.18%. The new six-set sweep uses input
start 1 and the corrected KEM arithmetic switch:

| Parameter | A/E, one request | A/E, eight requests |
| --- | ---: | ---: |
| ML-KEM-512 | 2.341x | 2.126x |
| ML-KEM-768 | 2.314x | 2.004x |
| ML-KEM-1024 | 2.365x | 2.013x |
| ML-DSA-44 | 1.966x | 1.966x |
| ML-DSA-65 | 1.910x | 1.914x |
| ML-DSA-87 | 2.150x | 2.023x |

The complete-mapping E arm gains 2.218--3.042x throughput from one to eight
workers on eight fixed inputs. With eight workers fixed, M64/M8 throughput
is 0.998--1.047x. D/E at K768 is 0.999x/0.998x, so enabling the additional
arithmetic instructions is not a universal improvement; retain D as a
control. DSA signing rejection varies with input, so the single-request
absolute latency ordering is not a general security-level scaling claim.

The write-through rebuild produces byte-identical host and kernel binaries
for all 30 board applications. Its separately rebuilt simulator runtimes
are being verified; board performance is independent of that pending model
validation. Full results and provenance are in
`pqc/results/v80_hw_validation/parameter_full_board_200mhz/README.md`.
