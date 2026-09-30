# DAC paper: communication-matched warp collectives

Title: **Communication-Matched Warp Collectives for ML-KEM and ML-DSA on a RISC-V SIMT GPU**

This directory is the submission-oriented rewrite of the VortexPQC paper. It uses the
IEEE conference template and targets **six body pages plus one reference page**. The
current design-complete draft intentionally occupies seven body pages plus one reference
page; later editing will recover the submission limit without deleting the hardware
description. The source is independent of `../paper/` and does not modify the long IEEE
draft.

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
| 1--3 | Introduction, related work, shared primitives, software communication, architectural requirements, and the start of the collective ISA |
| 3--5 | Collective ISA, Keccak Stage/Round, shared K/D NTT, execution control, software integration, and the start of Evaluation |
| 5--7 | Complete-request results, fusion and M16/M8/M4/M2/M1 tradeoffs, concurrency, routed cost, scope, and conclusion |
| 8 | References only |

## Background and motivation

Section II connects the common computations to communication and ISA constraints:

1. PQC Workloads and Common Primitives: the roles of NTT/INTT and Keccak,
   coefficient representations, state sizes, and the historical diagnostic
   evidence in Table I.
2. Warp-Cooperative Execution and Communication: Figure 1's register layouts,
   register-local and XOR-paired NTT layers, and Keccak neighborhoods. Baseline A
   already uses these layouts and the same surrounding software optimizations;
   dependent shuffle and arithmetic instructions remain specialization targets.
3. Challenges for a Register-Collective ISE: a butterfly's three logical inputs
   and two outputs versus the target's two source register names and one
   destination; K/D representation differences; RV32 old-half dependencies;
   and participation of referenced producer lanes. Theta, rho/pi, and Round
   require both old halves, whereas chi/iota consumes its selected half.

The full A--E definitions and the distinction between portable-C references,
historical PQRV controls, and the headline baseline are in Evaluation Methodology.
Section II poses the interface constraints; Section III supplies the instruction
semantics and hardware mechanisms. Neither historical deletion diagnostics nor
the presence of shuffles establishes that communication dominates the current
baseline. The comparison measures combined communication-and-arithmetic
specialization, not an isolated routing gain. Granularity and multiplier sizing
remain design and evaluation questions rather than background requirements.

## ISA and hardware organization

Section III introduces the collective ISA before its implementation. Table II
defines explicit source operands and per-lane outputs for the four Keccak
families, NTTBF, and NTTMUL. Its three top-level subsections cover Collective
ISA, Hardware Implementation, and Software Integration. Hardware Implementation
then separates the Keccak datapath, shared K/D modular datapath, and execution
control/core integration. Verification methods and model-parity results remain
in Evaluation Methodology.

Table II fills one column at 9-point type with separate per-lane `rs1[l]` and
`rs2[l]` columns. `.L/H` and `.K/D` explicitly list instruction variants;
`Q` remains a modulus selector in equations. NTTBF CT and GS have separate
rows and lane-result symbols linked to their equations. Each lane supplies
`a_l` and `zeta_l`, but pair routing consumes only the pair-low `zeta_p`;
the pair-high twiddle is ignored. NTT operations have no L/H forms on RV32.
KCHII obtains its round number from `rs2`, while KROUND encodes the round as
an immediate and uses `rs2` for the old high half.

The interface and pipeline descriptions were checked against the kernel
intrinsics, `VX_decode.sv`, `VX_alu_ksg25.sv`, `VX_alu_kround25.sv`,
`VX_pqc_nttmul.sv`, and `VX_alu_unit.sv`. Stage/NTT use custom-0; Round uses
custom-2. KCHII consumes a selected half and a uniform register round number;
the other Keccak families consume both RV32 halves. K/D share the multiplier
bank but differ in operand preparation as well as reduction. K-GS requires
both Montgomery and Barrett products, explaining its doubled beat count.
The stated NTT `B+6` latency is the unstalled integrated-ALU acceptance-to-commit
measurement, not the isolated PE pipeline depth.

## Figures and tables

All six figures are native TikZ/PGFPlots sources with restrained labels and explanations in
the captions.

| Figure | Source | Purpose |
| --- | --- | --- |
| 1 | `figures/thesis.tex` | Single-column coefficient-index matrix and Keccak lane neighborhoods |
| 2 | `figures/architecture_v2.tex` | Full-width Vortex ALU integration, detailed shared K/D capture/reduction/assembly, and the two-step Keccak Stage paths |
| 3 | `figures/datapath_detail.tex` | Single-column M2 product assignment and overlapped instruction lifetime |
| 4 | `figures/eval_requests.tex` | Six-parameter headline speedups first; B--E middle-set ablations relative to the 1x software reference |
| 5 | `figures/eval_tradeoffs.tex` | 2x2 comparison of fusion, NTTBF test cycles, complete-request bank sensitivity, and NTT resources |
| 6 | `figures/eval_scaling.tex` | Six-set occupancy and fixed-W8 batch throughput from the same board cohort |

The manuscript contains four tables: the initial software cost diagnosis, collective
interfaces, experimental setup, and matched post-route cost cohorts.

Figures 1--3 use 7-point sans-serif labels at their native drawing size. The
redraw follows the concrete layout and hierarchy of PacQ Fig. 3, SynGPU Fig. 6,
CHAM Figs. 3--4, and NTT-PIM Fig. 6: indexed data, connected functional blocks,
and explicit schedules replace sentence boxes. Figure 2 retains the compact
core-integration panel and pairs it with the more detailed NTT and Keccak Stage
datapaths. Figure 3 maps 16 logical pair products onto two multipliers and shows
how the following instruction enters while its predecessor drains, separating
the eight-cycle initiation interval from the 14-cycle commit latency.

Figures 4--5 are authored at 178-mm width with 8-point labels. Figure 4
allocates approximately 45/27.5/27.5 percent of its plotting width to the
headline and the two ablations. It uses 5-point bars, horizontal parameter
labels, and a shared request/worker legend. Figure 5 highlights M2 in all
three bank panels and retains signed request changes, including small
negative values. Whole-core and AFU resources remain in the table.

Evaluation has six subsections: methodology, complete-request acceleration,
hardware contributions, granularity/capacity, concurrency/batches, and routed
cost/scope. Figure 6 and Table IV share one full-width float so the occupancy
curves and the selected routed design point are visible together. The current
draft preserves the complete Design section; final page-limit editing is deferred.

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
  `assets/fusion_scope.dat`, `assets/ntt_bank.dat`, and
  `assets/ntt_bank_requests.dat` for plots;
- `assets/cost_rows.tex` for the post-route table;
- `assets/source_manifest.json` with source hashes and derived values.

The motivation table derives its values from `ablation_mlkem.csv` and
`ablation_mldsa.csv`: historical single-lane RV32 SimX, one core, W4T4, no L2/L3.
Keccak and NTT/INTT independent ablation deltas are 65.65%/12.67% for a complete
ML-KEM-768 request and 74.44%/7.62% for full-RAM ML-DSA-65 key generation.
The generator recomputes each reduction as `(C0 - C_without_i) / C0`, using the
matching instrumented baseline and the run with only primitive `i`'s body removed.
Deleting a body can also change scheduling and memory behavior. These intentionally
incorrect diagnostic runs motivate the selected primitives;
they are not current cooperative-board phase fractions, additive exclusive timing,
or hardware speedups. DSA is limited to the full-RAM KeyGen diagnostic: replacing
hash computations invalidates signing rejection and verification behavior, so
extrapolated signing/verification shares are not used.

Section II defines the register-resident cooperative baseline A. Evaluation
Methodology distinguishes it from portable-C references and historical PQRV
scalar controls, and defines all five A--E modes. They use the same surrounding
software mapping; separate granularity and bank controls have their own matched
configurations. The introduction states the denominator before reporting gains.

The M16 rows include Stage, Round, and Pointer; M2 includes a directly routed
Stage+M2 core. The full board image
contains additional optional Keccak units; its AFU area is reported separately
from the lean core sweep. Power and energy are excluded because no
workload-activity-based measurement is available.

The Figure 5 timing controls are distinct from the main board cohort:

- Fusion uses XRT isolated-permutation intervals and SimX whole-KEM launch
  cycles. The eight-request KEM fusion point has no paired XRT run.
- NTTBF uses historical RV32IMF W4T32 processor RTL test-program cycles,
  including loads, stores, and loops; this is not saturated unit throughput.
- Bank request sensitivity uses RV32IM W8T32 write-back XRT with fixed
  Pointer Keccak for KEM-768 and DSA-65, at one/eight requests. The generator
  recalculates all 20 signed changes from raw makespans and verifies matching
  binaries, call counts, and instruction counts across the banks.
- NTT resources use separate 200-MHz write-through routed cores. The
  six-parameter Stage+M2 board study validates that implementation; it does
  not establish joint optimality or repeat the complete multiplier sweep.

## Literature coverage

The introduction is approximately 900 English words and extends into page 2.
The bibliography has 25 cited entries. Additions cover GPU Kyber/Dilithium
software, modular RISC-V extensions, unified lattice hardware, and hash-interface
cost. Two unrelated SIMT application references were removed from the manuscript;
their figure styles remain useful drawing references. `literature_audit.md`
records the local DAC sample and the claim/source checks. Reference count is a
coverage check, not a target to fill with unrelated work.

## Build

```sh
make -C pqc/docs/dac_paper2
```

The command regenerates derived assets, compiles in `build/dac_paper2/latex`, and writes
`vortex_pqc_dac2_draft.pdf`. The delivery also includes
`vortex_pqc_dac2_source.zip`, which is compiled independently during verification.
