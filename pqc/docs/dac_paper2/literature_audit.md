# Literature and motivation audit

Checked 2026-09-29. This note records editorial evidence; it is not manuscript text.

## Local DAC sample

Source: `/home/jiangbowang/aphdcode/paper_ref/DAC_2022-2026_GPU_Crypto_Hardware_Papers_17/pdfs`.
The directory contains 33 PDFs. We excluded five papers from 2020--2021 and
PathFinder's 20-page presentation, leaving 27 local paper versions for 2022--2026.
Counts use numbered bibliography entries after the final reference heading, with
contiguous labels 1 through N checked in every file. Some local PDFs are author
manuscripts or extended versions; this is not a census or a DAC policy statement.

Counts range from 10 to 50, with median 30; 10/27 fall between 25 and 40.

| Local PDF | References | Pages |
| --- | ---: | ---: |
| `01_2026_G-Power_ Architecture-level GPU Power Modeling with Aggregated Knowledge Foundations from Known GPUs.pdf` | 36 | 7 |
| `02_2026_Beyond Exact_ Tight WCET Analysis of GPU Kernels with Branch Divergence.pdf` | 33 | 8 |
| `03_2026_An Efficient Heterogeneous Co-Design for Fine-Tuning on a Single GPU.pdf` | 36 | 7 |
| `04_2026_ZK-Flex_ A Flexible and Scalable Framework for Accelerating Zero-Knowledge Proofs.pdf` | 31 | 7 |
| `05_2025_GEM_ GPU-Accelerated Emulator-Inspired RTL Simulation.pdf` | 24 | 7 |
| `06_2025_ABC-FHE_ A Resource-Efficient Accelerator Enabling Bootstrappable Parameters for Client-Side Fully Homomorphic Encryption.pdf` | 34 | 7 |
| `07_2025_SeDA_ Secure and Efficient DNN Accelerators with Hardware_Software Synergy.pdf` | 22 | 7 |
| `08_2025_ZK-Hammer_ Leaking Secrets from Zero-Knowledge Proofs via Rowhammer.pdf` | 39 | 7 |
| `09_2025_AmpereBleed_ Exploiting On-chip Current Sensors for Circuit-Free Attacks on ARM-FPGA SoCs.pdf` | 44 | 7 |
| `11_2023_CHAM_ A Customized Homomorphic Encryption Accelerator for Fast Matrix-Vector Product.pdf` | 33 | 6 |
| `12_2023_NTT-PIM_ Row-Centric Architecture and Mapping for Efficient Number-Theoretic Transform on PIM.pdf` | 24 | 6 |
| `13_2023_BP-NTT_ Fast and Compact in-SRAM Number Theoretic Transform with Bit-Parallel Modular Multiplication.pdf` | 31 | 6 |
| `14_2023_Towards A Formally Verified Fully Homomorphic Encryption Compute Engine.pdf` | 12 | 6 |
| `15_2023_Primer_ Fast Private Transformer Inference on Encrypted Data.pdf` | 23 | 6 |
| `16_2022_MATCHA_ A Fast and Energy-Efficient Accelerator for Fully Homomorphic Encryption over the Torus.pdf` | 22 | 6 |
| `24_2022_Apple vs. EMA_ Electromagnetic Side Channel Attacks on Apple CoreCrypto.pdf` | 27 | 6 |
| `25_2022_PARIS and ELSA_ An Elastic Scheduling Algorithm for Reconfigurable Multi-GPU Inference Servers.pdf` | 45 | 13 |
| `26_2022_GTuner_ Tuning DNN Computations on GPU via Graph Attention Network.pdf` | 30 | 6 |
| `27_2022_GATSPI_ GPU Accelerated Gate-Level Simulation for Power Improvement.pdf` | 19 | 7 |
| `28_2022_Xplace_ An Extremely Fast and Extensible Global Placement Framework.pdf` | 24 | 6 |
| `29_2025_FastPath_ A Hybrid Approach for Efficient Hardware Security Verification.pdf` | 45 | 7 |
| `30_2024_MSMAC_ Accelerating Multi-Scalar Multiplication for Zero-Knowledge Proof.pdf` | 12 | 6 |
| `31_2025_SynGPU_ Synergizing CUDA and Bit-Serial Tensor Cores for Vision Transformer Acceleration on GPU.pdf` | 22 | 7 |
| `32_2025_PacQ_ A SIMT Microarchitecture for Efficient Dataflow in Hyper-asymmetric GEMMs.pdf` | 24 | 7 |
| `33_2023_GenFuzz_ GPU-accelerated Hardware Fuzzing using Genetic Algorithm with Multiple Inputs.pdf` | 10 | 6 |
| `34_2026_iHyperG_ Incremental Hypergraph Partitioning on GPU.pdf` | 47 | 7 |
| `35_2026_A Differentiable Approach to Task Graph Partitioning_ A Case Study in RTL Simulation.pdf` | 50 | 7 |

The approximate introduction prose counts for PacQ, SynGPU, ABC-FHE, CHAM,
NTT-PIM, BP-NTT, and ZK-Flex are 593, 662, 729, 879, 473, 770, and 734 words.
These exclude abstract, figures/captions, footers, and citation numbers; contribution
paragraphs remain included. Most extend into page 2, which also includes title,
abstract, and sometimes motivation figures. A manuscript position near page 1.5
is not equivalent to 1.5 full pages of introduction prose.

SynGPU II.C uses register-file occupancy and execution-time evidence. ABC-FHE
II.D uses transform fractions and encryption/decryption imbalance. ZK-Flex II.1
uses changing POLY/EC fractions. These support placing detailed workload evidence
in Background and Motivation, while the introduction states the architectural
problem, related-work gap, proposed approach, and main results.

## Added primary sources and supported claims

| Bib key | Primary source | Claim used / boundary |
| --- | --- | --- |
| hikyber | [Author manuscript](https://eprint.iacr.org/2023/1194) | GPU Kyber register reuse, fusion, and NTT scheduling; related work, not a reproduced numerical baseline. |
| cudilithium | [Author manuscript](https://eprint.iacr.org/2024/1365) | GPU Dilithium warp cooperation, batching, and scheduling; both single- and multi-warp mappings occur. |
| alkim | [Author paper](https://eprint.iacr.org/2020/049) | Fine-grained finite-field RISC-V extensions and optimized software controls. |
| bevin | [Published paper](https://doi.org/10.1109/SOCC66126.2025.11235487) | Shared scalar Montgomery-reduction extension, not a lane-pair collective. |
| sapphire | [Author paper](https://eprint.iacr.org/2019/1140) | Configurable lattice processor shares resources; schemes predate the final FIPS standards. |
| kali | [Author paper](https://eprint.iacr.org/2022/1086) | Unified Kyber/Dilithium hardware is established. Published first author is Aikata Aikata. |
| kid | [Author paper](https://arxiv.org/abs/2311.04581) | Unified FPGA NTT multiplication and configurable butterfly resources; different storage/interface boundary. |
| highvolume | [Author-institution full text](https://upcommons.upc.edu/server/api/core/bitstreams/b87f2f19-0f64-4afd-a3c9-8370cdf14102/content) | Full-protocol FPGA batching; a shared framework alone does not prove a physically shared K/D datapath. |
| karl | [Author-institution full text](https://publica-rest.fraunhofer.de/server/api/core/bitstreams/62370613-ab72-412b-bdd1-e72fa635143a/content) | Hash-interface and sponge-I/O communication costs; its several interfaces are not all identical to our Pointer arm. |
| horcrux | [Author preprint](https://arxiv.org/abs/2607.13939) | Persistent private hash registers in a recent RISC-V PQC architecture; explicitly cited as a preprint. |

The bibliography uses formal publisher metadata where available. GPU, ISE, and
unified-hardware PDFs were checked against local primary papers in the
`PQC_hardware_top50_top100_bundle/top100` collection. PacQ and SynGPU remain drawing
references in the README, but their unrelated application claims were removed
from the manuscript. The result is 25 cited entries, not an arbitrary quota.

Do not claim first GPU PQC, first register-resident Keccak, first whole-round
Keccak, first warp collective, or first unified Kyber/Dilithium hardware. The
paper's contribution is the SIMT operand/writeback contract, its shared serialized
implementation, and controlled complete-request evaluation of the design choices.

## Historical motivation and baseline boundaries (superseded)

The historical motivation table has been replaced by Figure 1's real AVED board
profiling of complete KEM and DSA requests; see the current README and
`data/baseline_profile_200mhz.csv` for its measurement scope.

The removed table used raw baseline/ablation cycles from `data/ablation_mlkem.csv` and
`data/ablation_mldsa.csv`. KEM: 33,492,153 baseline cycles, 21,986,475 Keccak delta,
4,243,182 NTT/INTT delta. DSA key generation, full RAM: 39,121,351 baseline,
29,119,995 Keccak delta, 2,981,695 NTT/INTT delta. Independent stubs yield invalid
outputs; their cycle differences include dependent scheduling/memory effects.
The DSA full-request estimates in the archive are deliberately excluded from the
motivation table because they extrapolate key-generation costs across signing
retries. The historical RV32 configuration does not establish F/D-disabled status.

Portable C supplies correctness references and this initial diagnosis. PQRV is a
separate optimized scalar comparison. The main board denominator is A: a fully
mapped, register-resident cooperative implementation on the same Vortex core.
B/C/D/E isolate Keccak Stage, NTT, their combination, and arithmetic reuse.
Old hotspot shares are not fractions of A or E, and do not prove that NTT becomes
the dominant phase after Keccak acceleration.

## Evaluation-method presentation

Checked 2026-10-02 against page 5 of the local primary papers:

- [SynGPU](https://chenzhangsjtu.github.io/files/2025-DAC_SynGPU-3.pdf),
  Section V-A, describes software workloads and hardware evaluation in separate
  prose paragraphs, including the simulator, synthesis tools, and technology.
- [PacQ](https://ruokaiyin.github.io/papers/pacq.pdf), Section V's Experimental
  Setup, explains simulation and synthesis in prose and uses Table I for the
  detailed configurations of PacQ and its baselines.
- [CHAM](https://renxuanle.github.io/pub/cham2023.pdf), Section V-A, describes
  platforms and tool flow in prose, then reports resource utilization in Table II.

These examples support either prose or tables according to the information being
presented; they do not establish a DAC prohibition on configuration tables. Our
former six-entry setup table is absorbed into the platform paragraph, while
comparative speedups and routed resources retain tables. The paper keeps the
measurement protocol readable in prose; the README preserves the detailed cycle
and throughput definitions. These are editorial references, not additions to the
manuscript bibliography.
