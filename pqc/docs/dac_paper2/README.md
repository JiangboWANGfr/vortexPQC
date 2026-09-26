# DAC paper: communication-matched warp collectives

Title: **Communication-Matched Warp Collectives for ML-KEM and ML-DSA on a RISC-V SIMT GPU**

This directory is the submission-oriented rewrite of the VortexPQC paper. It uses the
IEEE conference template and fixes the manuscript at **six body pages plus one reference
page**. The source is independent of `../paper/` and does not modify the long IEEE draft.

## Paper argument

The paper treats communication as the hardware boundary. Keccak and NTT state remain in
ordinary lane registers; stateless collectives implement only fixed lane neighborhoods and
return one architectural result per lane. The same serialized multiplier bank and routing
serve 16-bit ML-KEM, 32-bit ML-DSA, and ML-DSA pointwise multiplication. Complete-request
measurements, rather than primitive throughput alone, select Keccak granularity and the M2
NTT bank.

The page allocation is:

| Pages | Content |
| --- | --- |
| 1--2 | Introduction, communication graphs, prior work, and SIMT design requirements |
| 2 (end)--5 (upper) | Collective contract, Keccak Stage/Round, shared K/D NTT, native-library integration, and verification |
| 5 (lower)--6 | Methodology, complete-request results, fusion and M16/M8/M4/M2/M1 tradeoffs, post-route cost, scope, and conclusion |
| 7 | References only |

## Figures and tables

All seven figures are native TikZ/PGFPlots sources with restrained labels and explanations in
the captions.

| Figure | Source | Purpose |
| --- | --- | --- |
| 1 | `figures/thesis.tex` | NTT XOR partners, Keccak neighborhoods, and the stateless register contract |
| 2 | `figures/architecture_v2.tex` | Core integration, Keccak granularity, and the shared serialized NTT unit |
| 3 | `figures/datapath_detail.tex` | Paired butterfly, M2 beat schedule, and RV32 Keccak low/high dependency |
| 4 | `figures/integration_v2.tex` | Common ML-KEM/ML-DSA software hooks and shared hardware |
| 5 | `figures/eval_requests.tex` | Matched 200-MHz board speedups for complete KEM and DSA requests |
| 6 | `figures/eval_tradeoffs.tex` | Fusion gap and the M16/M8/M4/M2/M1 performance-resource sweep |
| 7 | `figures/eval_parameters.tex` | Six-parameter 200-MHz board A/E speedups |

The manuscript contains three tables: the collective interfaces, the experimental setup,
and matched post-route cost cohorts.

## Reproducible numbers

`data/` contains archived measurement snapshots from `pqc/results/`. The core PPA
sweep is routed at 200 MHz with the board's write-through D-cache setting.
The six-parameter board sweep measures 120 A/E configurations with five timed
repetitions each; its occupancy and fixed-W8 batch plots are archived under
`pqc/results/v80_hw_validation/parameter_board_200mhz/`.
`generate_results.py` validates matched binaries, correctness status, instruction
equality, parity bounds, configuration, and routing status before generating:

- `assets/numbers.tex` for reported values;
- `assets/board_kem.dat`, `assets/board_dsa.dat`, `assets/parameter_speedup.dat`, `assets/fusion_gain.dat`, and `assets/ntt_bank.dat` for plots;
- `assets/cost_rows.tex` for the post-route table;
- `assets/source_manifest.json` with source hashes and derived values.

The M16 rows include Stage, Round, and Pointer; M2 includes a directly routed
Stage+M2 core. The full board image
contains additional optional Keccak units; its AFU area is reported separately
from the lean core sweep. Power and energy are excluded because no
workload-activity-based measurement is available.

## Build

```sh
make -C pqc/docs/dac_paper2
```

The command regenerates derived assets, compiles in `build/dac_paper2/latex`, and writes
`vortex_pqc_dac2_draft.pdf`. The delivery also includes
`vortex_pqc_dac2_source.zip`, which is compiled independently during verification.
