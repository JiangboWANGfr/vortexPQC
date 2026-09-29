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
| 2 (end)--4 | Collective contract, Keccak Stage/Round, shared K/D NTT, native-library integration, and verification |
| 4 (end)--6 | Methodology, complete-request results, fusion and M16/M8/M4/M2/M1 tradeoffs, post-route cost, scope, and conclusion |
| 7 | References only |

## Figures and tables

All seven figures are native TikZ/PGFPlots sources with restrained labels and explanations in
the captions.

| Figure | Source | Purpose |
| --- | --- | --- |
| 1 | `figures/thesis.tex` | Single-column coefficient-index matrix and Keccak lane neighborhoods |
| 2 | `figures/architecture_v2.tex` | Full-width core context, expanded shared NTT datapath, and Keccak pipelines |
| 3 | `figures/datapath_detail.tex` | Single-column M2 product schedule and RV32 Round register dependencies |
| 4 | `figures/integration_v2.tex` | Common ML-KEM/ML-DSA software hooks and shared hardware |
| 5 | `figures/eval_requests.tex` | A--E ablations for the middle sets and A/E speedups for all six parameter sets |
| 6 | `figures/eval_tradeoffs.tex` | Fusion gap and the M16/M8/M4/M2/M1 performance-resource sweep |
| 7 | `figures/eval_scaling.tex` | Six-set occupancy and fixed-W8 batch throughput from the same board cohort |

The manuscript contains three tables: the collective interfaces, the experimental setup,
and matched post-route cost cohorts.

Figures 1--3 use 7-point sans-serif labels at their native drawing size. The
redraw follows the concrete layout and hierarchy of PacQ Fig. 3, SynGPU Fig. 6,
CHAM Figs. 3--4, and NTT-PIM Fig. 6: indexed data, connected functional blocks,
and explicit schedules replace sentence boxes. Figure 3 distinguishes pair
products from beat numbers and uses Round as the old-half dependency example;
the Stage chi/iota operation only reads its selected half.

## Reproducible numbers

`data/` contains archived measurement snapshots from `pqc/results/`. The core PPA
sweep is routed at 200 MHz with the board's write-through D-cache setting.
The complete-mapping board sweep measures 156 configurations with one warmup
and five timed repetitions each (780 measurements). It includes A--E at one
and eight requests plus A/E occupancy and fixed-W8 batches through 64 requests.
Its source record is
`pqc/results/v80_hw_validation/parameter_full_board_200mhz/`.
All arms enable the same software mapping; A/B contain no NTT instructions,
including Montgomery conversion. Zeroization uses warp cooperation for one
worker and leader execution otherwise, consistently across arms.

The primary A/E speedups are 2.314--2.365x / 2.004--2.126x for ML-KEM and
1.910--2.150x / 1.914--2.023x for ML-DSA (one/eight requests). Hardware NTT
adds 1.082--1.153x and 1.118--1.194x after Stage for KEM and DSA. Extra modular
arithmetic has limited KEM benefit; KEM-768 remains within 0.2% of D.
The occupancy curves gain 2.218--3.042x from one to eight workers. Fixed-W8
M64/M8 throughput is 0.998--1.047x; intermediate DSA batches peak at 1.119x
and include input-dependent signing-retry variation.

The manuscript validates exactly the completed 60 headline SimX/XRT pairs,
with identical instruction counts and a maximum cycle gap of 4.422%. Their
300 board repetitions also match the XRT instruction counts. Supplementary
simulation scans may continue in the background; their completion is neither
required nor claimed by the paper. The frozen headline CSVs are independent
of the live queue. `full_mapping_sources.json` records hashes and scope.
The older middle-set board JSONs remain historical snapshots, not headline
inputs. Separate fusion/bank timing controls retain their archived write-back
configuration; the primary board/model cohort and routed PPA use write-through.
`generate_results.py` validates matched binaries, correctness status, instruction
equality, parity bounds, configuration, and routing status before generating:

- `assets/numbers.tex` for reported values;
- `assets/board_kem.dat`, `assets/board_dsa.dat`, `assets/parameter_speedup.dat`,
  `assets/parameter_occupancy.dat`, `assets/parameter_batch.dat`,
  `assets/fusion_gain.dat`, and `assets/ntt_bank.dat` for plots;
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
