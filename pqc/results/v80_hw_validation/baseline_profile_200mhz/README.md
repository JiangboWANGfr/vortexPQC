# Baseline-A primitive intervals on V80 at 200 MHz

This cohort is actual V80 hardware execution through the AVED driver, collected
in `build32_pqc_board_profile/profile_board/20261002T032038Z`. The configured core
has eight 32-thread warps; measurements use one resident worker warp. Control
host/kernel binaries match the archived full-mapping A binaries byte-for-byte.
They use software Keccak and NTT, with the same cooperative software mapping as
the paper's evaluation baseline.

Both complete workflows are correct executions. ML-KEM-768 records KeyGen,
Encaps and Decaps separately for one KAT input. ML-DSA-65 records KeyGen, Sign and
Verify for input IDs 1–32, processed sequentially by one worker. Each control and
profile mode has one warmup and five measured repetitions. Public/secret keys,
ciphertexts/shared secrets or signatures pass byte-exact reference checks, and
primitive call counts agree across modes and repetitions for each input.

## Results and aggregation

| Complete workflow | Control time (ms) | Profile time (ms) | Keccak-f | NTT + INTT | Other |
| --- | ---: | ---: | ---: | ---: | ---: |
| ML-KEM-768 | 22.30 | 23.32 | 52.22% | 7.87% | 39.91% |
| ML-DSA-65 | 157.88 | 159.47 | 41.59% | 10.63% | 47.78% |

Times are the mean across inputs of each input's five-run median. Fractions use
the sum of each primitive's measured intervals divided by the sum of instrumented
request intervals across all inputs and all five repetitions. They are weighted
by cycles, not averaged percentages. Batch makespan is not the denominator: it
also includes setup and diagnostics between sequential requests. `Other` is each
request interval minus Keccak-f, NTT and INTT; the four buckets sum exactly to the
measured request. Per-operation results are in `operation_summary.csv`.

The incremental phase-probe overhead is **4.577075% for KEM** and
**0.902567–1.147302% across the 32 DSA inputs**, computed from paired five-run
medians. Controls already retain the baseline call counters and DSA pointwise
timers; these percentages measure the additional probes. No overhead is
subtracted from either numerators or denominators. Consequently, fractions
describe instrumented execution, not exact uninstrumented costs or Amdahl bounds.

Keccak boundaries follow the implementations: KEM times the non-inlined 24-round
body with state in lane registers; DSA times the cooperative permutation call,
including dispatch and state load/store. Both exclude sponge absorption and
extraction. NTT/INTT include their transform dispatch paths. These boundaries
support within-workflow attribution, not a direct comparison of per-permutation
costs between algorithms.

## Probe comparison

KEM's initial probe added about 1.36 ms to a 22.30 ms control (6.11%), whereas the
DSA probe adds about 1.58 ms to a 157.88 ms mean control (about 1.00%). Thus the
relative percentages have substantially different denominators. KEM's Keccak
probe also executes while the warp is active, unlike DSA's leader-lane wrapper.
Inlining only the KEM timer wrapper removed its separate save/restore frame while
keeping the 24-round body unchanged and non-inlined. An input dependency barrier
keeps absorb-side arithmetic before the start timestamp. The final KEM overhead
is about 1.02 ms, or 4.58%.

`probe_comparison.json` records both cohorts and their archive hashes. The earlier
6.11% cohort and original source ZIP remain in local build scratch at
`build32_pqc_board_profile/profile_board/20261002T030742Z`; the files in this
directory contain the final 4.58% cohort.

## Files and reproduction

- `manifest.json`: build commands, source/binary/runtime hashes, configuration,
  clock readbacks and measurement boundaries.
- `baseline_profile_sources.zip`: all raw logs and parsed records, measured
  binaries/configurations, runner and source snapshot.
- `intervals.csv`: 990 per-operation records, including control and profile runs;
  unmeasured control primitive fields are empty.
- `summary.json`: aggregated cycle shares and per-input probe overhead.
- `operation_summary.csv`: compact workflow/operation times and shares.

From the repository root, with the recorded toolchain, IM libraries and existing
V80 runtime/image available:

```sh
mkdir -p build32_pqc_board_profile
cd build32_pqc_board_profile
python3 ../pqc/results/v80_hw_validation/run_baseline_profile_200mhz.py --build
python3 ../pqc/results/v80_hw_validation/run_baseline_profile_200mhz.py --run
```

The run requires a functioning SLASH driver and VRTD for board `02:00`, with the
expected resident image and a user clock reading 200 MHz. The runner selects
`VORTEX_DRIVER=aved`, reuses the image with `VORTEX_AVED_NO_PROGRAM=1`, verifies
clock readbacks and binary hashes, and takes the board-run lock. It does not
program the FPGA. The local image file hash is recorded, but VRT metadata does
not independently attest the resident PDI identity. SimX trial results are not
included in this hardware cohort.
