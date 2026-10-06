# DAC paper: communication-matched warp collectives

Title: **Communication-Matched Warp Collectives for ML-KEM and ML-DSA on a RISC-V SIMT GPU**

This directory is the submission-oriented rewrite of the VortexPQC paper. It uses the
IEEE conference template with **10-point body text, six body pages, and one separate
reference page**. The source is independent of `../paper/` and does not modify the
long IEEE draft.

## Paper argument

The paper treats communication as the hardware boundary. Keccak and NTT state remain in
ordinary lane registers; stateless collectives implement only fixed lane neighborhoods and
return one architectural result per lane. The same serialized multiplier bank and routing
serve 16-bit ML-KEM, 32-bit ML-DSA, and ML-DSA pointwise multiplication. Complete-request
measurements, rather than primitive throughput alone, support Keccak granularity and the M2
NTT bank.

The intended page allocation, rather than the current compiled pagination, is:

| Pages | Content |
| --- | --- |
| 1--3 | Introduction, related work, shared primitives, software communication, architectural requirements, and the start of the collective ISA |
| 3--5 | Collective ISA, Keccak Stage/Round, shared K/D NTT, execution control, software integration, and the start of Evaluation |
| 5--6 | Complete-request results, fusion and M16/M8/M4/M2/M1 tradeoffs, concurrency, routed cost, scope, and conclusion |
| After the body | References on separate pages |

## Background and motivation

Section II connects the common computations to communication and ISA constraints:

1. PQC Workloads and Common Primitives: the roles of NTT/INTT and Keccak,
   coefficient representations, state sizes, and the measured complete-request
   composition of cooperative software A in Figure 1.
2. Warp-Cooperative Execution and Communication: Figure 2's register layouts,
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
semantics and hardware mechanisms. Neither primitive interval attribution nor
the presence of shuffles establishes that communication dominates the current
baseline. The comparison measures combined communication-and-arithmetic
specialization, not an isolated routing gain. Granularity and multiplier sizing
remain design and evaluation questions rather than background requirements.

## ISA and hardware organization

Section III introduces the collective ISA before its implementation. Table I
defines explicit source operands and per-lane outputs for the four Keccak
families, NTTBF, and NTTMUL. Its three top-level subsections cover Collective
ISA, Hardware Implementation, and Software Integration. Hardware Implementation
then separates the Keccak datapath, shared K/D modular datapath, and execution
control/core integration. Verification methods and model-parity results remain
in Evaluation Methodology.

Table I fills one column at 9-point type with separate per-lane `rs1[l]` and
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

All five figures are native TikZ/PGFPlots sources with restrained labels and explanations in
the captions.

| Figure | Source | Purpose |
| --- | --- | --- |
| 1 | `figures/motivation_profile.tex` | Single-column measured composition of complete KEM and DSA requests in software A |
| 2 | `figures/thesis.tex` | Single-column coefficient-index matrix and Keccak lane neighborhoods |
| 3 | `figures/architecture_v2.tex` | Core integration, symbol-level K/D preparation and indexed assembly, and expanded Keccak logic across R1/R2 |
| 4 | `figures/datapath_detail.tex` | Single-column M2 product assignment and overlapped instruction lifetime |
| 5 | `figures/eval_scaling.tex` | Fixed-eight-request E throughput and E/A speedup at matched worker counts across six parameter sets |

The manuscript contains four tables: collective interfaces, complete-request
acceleration, NTT bank tradeoffs, and matched post-route cost cohorts. The platform
configuration is described in the Experimental Methodology prose.
Table II reports B--E speedups over A for all six parameter sets at one and eight
requests, with E's absolute batch makespan in Mcycles; bold marks final mode E.
The abstract and table caption define each request as KeyGen/Encaps/Decaps for
KEM or KeyGen/Sign/Verify for DSA, rather than an individual API latency.
Table III reports NTTBF test-cycle increase, maximum request-cycle increase,
and exact NTT-hierarchy LUT/FF/DSP counts for all five banks; M2 is bold.

Figure 1 uses two stacked bars for Keccak-f, NTT/INTT, and Other, with fractions
derived from nonoverlapping measured intervals rather than deletion experiments.
Figures 2--4 use compact labels at their native drawing sizes. The
redraw follows the concrete layout and hierarchy of PacQ Fig. 3, SynGPU Fig. 6,
CHAM Figs. 3--4, and NTT-PIM Fig. 6: indexed data, connected functional blocks,
and explicit schedules replace sentence boxes. Figure 3 allocates approximately
46/75/57 mm to core integration, NTT, and Keccak. NTT uses four visual levels:
operand preparation, first/remaining-beat selection, shared arithmetic,
and indexed assembly. The multiplier pair feeds reduction and finishing
vertically; the aligned coefficient pipeline runs down the right edge and
also supplies K-GS reduction. Two short arrows show the CT lane writes.
Keccak expands the five-input column XOR, fixed gather/rotate, row NOT/AND/XOR,
and second-stage correction/constant injection across R1/R2. The row operands,
round index, and retained input use separate wires. Figure 4 maps 16 logical pair
products onto two multipliers and shows
how the following instruction enters while its predecessor drains, separating
the eight-cycle initiation interval from the 14-cycle commit latency.

Figure 5 uses two panels at fixed N=8: E throughput relative to E at one worker,
and E/A speedup at the same worker count. Each worker is one resident 32-lane warp.
Panel titles appear below the plots in regular type. Hyperlinks, citations, and
cross-references use the original dark blue. The original fixed-W8 batch-size
results remain in the text, including the intermediate DSA peak and input caveat.
The primitive/request fusion comparison remains in the prose.
Whole-core and AFU resources remain in Table IV and the routed-cost prose.

Evaluation has five subsections: methodology, complete-request acceleration with
hardware contributions, granularity/capacity, concurrency/batches, and routed
cost/scope. Single-request execution cycles are reported in Mcycles.
Figure 5 and Tables II--IV use single-column floats.

## Reproducible numbers

`data/` contains archived measurement snapshots from `pqc/results/`. The core PPA
sweep is routed at 200 MHz with the board's write-through D-cache setting.
Vivado 2025.1 OPT3 applies to the independent core implementations; the board
image retains its separately archived routing strategy.
The complete-mapping board sweep measures 156 configurations with one warmup
and five timed repetitions each (780 measurements). It includes A--E at one
and eight requests plus A/E occupancy and fixed-W8 batches through 64 requests.
Its source record is
`pqc/results/v80_hw_validation/parameter_full_board_200mhz/`.
Each run measures batch cycles as `C_batch = max(end_i) - min(start_i)`,
from the first request's API start to the last request's API completion.
This excludes host transfers and the initial setup/final diagnostics, while
retaining gaps between waves. `C_X` is the median of five measured runs after
one warmup; primary speedup is `C_A / C_X`. At the fixed `f = 200 MHz`,
`T = C / f`, so cycle and time ratios coincide. Figure 5 fixes `N = 8`:
panel (a) plots `C_E(W=1) / C_E(W)`, and panel (b) plots `C_A(W) / C_E(W)`.
Five-run relative range `(max-min)/median` has median 0.233% and maximum 3.409%
across the 156 configurations. Repeated inputs measure timing variation, not the
distribution of signing retries.
All arms enable the same software mapping; A/B contain no NTT instructions,
including Montgomery conversion. Zeroization uses warp cooperation for one
worker and leader execution otherwise, consistently across arms.
Worker-count scaling therefore does not isolate occupancy alone.
Primary A--E runs omit Figure 1's additional phase probes; baseline call
counters and existing DSA pointwise probes remain enabled consistently.

The primary A/E speedups are 2.314--2.365x / 2.004--2.126x for ML-KEM and
1.910--2.150x / 1.914--2.023x for ML-DSA (one/eight requests). Hardware NTT
adds 1.082--1.153x and 1.118--1.194x after Stage for KEM and DSA. Extra modular
arithmetic has limited KEM benefit; KEM-768 remains within 0.2% of D.
The E throughput curves gain 2.218--3.042x from one to eight workers, while E/A
speedup at matched worker counts ranges from 1.826x to 2.365x. Fixed-W8
N64/N8 throughput is 0.998--1.047x; intermediate DSA batches peak at 1.119x
and include input-dependent signing-retry variation.

The manuscript validates exactly the completed 60 headline SimX/XRT pairs,
with identical instruction counts and a maximum whole-device PERF cycle gap of
4.422%; this is a different timing window from request-batch makespan. Their
300 board repetitions also match the XRT instruction counts. Supplementary
simulation scans may continue in the background; their completion is neither
required nor claimed by the paper. The frozen headline CSVs are independent
of the live queue. `full_mapping_sources.json` records hashes and scope.
The older middle-set board JSONs remain historical snapshots, not headline
inputs. Separate fusion/bank timing controls retain their archived write-back
configuration; the primary board/model cohort and routed PPA use write-through.
Table III preserves distinct measurement windows: NTTBF test-program cycles
and request batch makespan cycles, alongside independent routed resources. The fusion prose
compares XRT permutation spans and SimX KEM launch cycles.
`generate_results.py` validates matched binaries, correctness status, instruction
equality, parity bounds, configuration, and routing status before generating:

- `assets/numbers.tex` for reported values;
- `assets/motivation_profile.dat`, `assets/board_kem.dat`, `assets/board_dsa.dat`, `assets/parameter_speedup.dat`,
  `assets/parameter_occupancy.dat`, `assets/parameter_worker_speedup.dat`, and
  `assets/ntt_bank_requests.dat` for plots and supporting comparisons;
- `assets/parameter_ablation_rows.tex`,
  `assets/ntt_bank_rows.tex`, and `assets/cost_rows.tex` for
  result tables;
- `assets/source_manifest.json` with source hashes and derived values.

The motivation figure derives its values from `baseline_profile_200mhz.csv`,
`baseline_profile_summary_200mhz.json`, and `baseline_profile_manifest_200mhz.json`.
The source record is `pqc/results/v80_hw_validation/baseline_profile_200mhz/`.
These are real AVED board measurements of correct cooperative software A at
200 MHz, with one active worker. Complete KEM requests include KeyGen, Encaps,
and Decaps; complete DSA requests include KeyGen, Sign, and Verify. Each operation
is timed separately. One KEM-768 KAT input and 32 DSA-65 inputs are measured
with one warmup and five timed repetitions of both profile and control builds.

Keccak-f, NTT, and INTT intervals do not overlap. The figure combines NTT/INTT
and computes Other as the request total minus those measured primitive intervals;
it divides summed primitive cycles by summed three-API request cycles across
all measured repetitions, rather than using batch makespan or averaging shares.
The current Keccak timer boundaries differ: KEM measures its in-register
24-round permutation, while DSA measures the permutation wrapper including
dispatch and state transfer. Sponge absorption and extraction remain in Other.
The measurement runner checks correctness and matched call counts. The generator
checks archived intervals, configuration, and matched control identities.
Controls omit the added primitive phase probes;
baseline call counters and the existing DSA pointwise probes remain identical
in both builds. The reported per-input overhead is the incremental cost of the
added phase probes. It remains in the plotted instrumented denominators and is
not subtracted. The plot is a diagnostic of major costs,
not an exact uninstrumented breakdown or an isolated communication-cost measure.

Section II defines the register-resident cooperative baseline A. Evaluation
Methodology distinguishes it from portable-C correctness references and defines
all five A--E modes. Historical PQRV scalar controls remain separate archived
measurements. The headline modes use the same surrounding
software mapping; separate granularity and bank controls have their own matched
configurations. The introduction states the denominator before reporting gains.

The NTT-M16 rows include Stage, Round, and Pointer; NTT-M2 includes a directly routed
Stage+M2 core. NTT-M16 and NTT-M2 already contain NTT hardware. Stage's 1.289%
LUT increment is relative to NTT-M2, not to a core without PQC extensions.
The full board image
contains additional optional Keccak units; its AFU area is reported separately
from the lean core sweep. Power and energy are excluded because no
workload-activity-based measurement is available.

The fusion and Table III timing controls are distinct from the main board cohort:

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

The introduction is approximately 900 English words.
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
