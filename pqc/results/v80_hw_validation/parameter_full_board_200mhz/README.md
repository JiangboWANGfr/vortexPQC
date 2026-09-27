# Complete software mapping on V80 at 200 MHz

All 156 cells use one warmup and five timed runs. Each request passes byte-exact KATs; DSA arena allocation checks pass. Algorithm call counts match across arms and repetitions. A uses software Keccak/NTT; B enables Stage, C enables NTT, D enables both, and E also enables arithmetic NTTMUL. Every arm uses the same full software mapping. KEM Montgomery conversion obeys the arithmetic switch; A contains no PQC/NTT custom opcodes.

| Parameter | A/E, M1 | A/E, M8 | E M1 (ms) | E W8/W1, fixed M8 | E throughput M64/M8, fixed W8 |
| --- | ---: | ---: | ---: | ---: | ---: |
| K512 | 2.341 | 2.126 | 5.912 | 2.494 | 0.998 |
| K768 | 2.314 | 2.004 | 9.636 | 2.318 | 1.001 |
| K1024 | 2.365 | 2.013 | 14.757 | 2.218 | 1.000 |
| D44 | 1.966 | 1.966 | 51.264 | 2.766 | 1.019 |
| D65 | 1.910 | 1.914 | 107.181 | 3.042 | 1.047 |
| D87 | 2.150 | 2.023 | 97.864 | 2.637 | 1.016 |

## Incremental acceleration

| Parameter | A/B M1 / M8 | B/D M1 / M8 | D/E M1 / M8 |
| --- | ---: | ---: | ---: |
| K512 | 2.016 / 1.848 | 1.153 / 1.124 | 1.008 / 1.024 |
| K768 | 2.048 / 1.827 | 1.131 / 1.099 | 0.999 / 0.998 |
| K1024 | 2.124 / 1.835 | 1.108 / 1.082 | 1.005 / 1.014 |
| D44 | 1.495 / 1.514 | 1.147 / 1.194 | 1.146 / 1.088 |
| D65 | 1.466 / 1.541 | 1.148 / 1.193 | 1.135 / 1.041 |
| D87 | 1.744 / 1.687 | 1.118 / 1.154 | 1.102 / 1.040 |

All sweep inputs start at 1. Archived DSA reproduction uses input start 3 and is reported separately in `replay/summary.json`. Fixed-W8 batches use synchronized waves; DSA batch extensions add inputs and can change signing rejection work.

The resident image was reused without programming. The user clock reads back at 200 MHz; VRT metadata does not independently attest the resident PDI identity.

Simulation coverage at report generation: 14/312 planned runs; 0 SimX/XRT pairs; 0 model-parity failures. Board/XRT instruction counts checked for 0 cells (five repetitions each).

The write-through simulator build produces byte-identical host/kernel binaries for all 30 apps measured on the board; `simulator_manifest.json` records that comparison. The earlier write-back diagnostic is retained separately and does not pass RTL/model validation.

Raw logs, build/source manifests, opcode/mapping audit, binaries and configurations are in `parameter_board_sources.zip`. Derived ratios are in `pqc_parameters_speedups.csv`; absolute throughput and latency are in `headline.csv`.
