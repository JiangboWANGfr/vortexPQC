# V80 parameter sweep at 200 MHz

`run_parameter_board_200mhz.py` runs ML-KEM-512/768/1024 and
ML-DSA-44/65/87 on the resident V80 image. For each parameter set it
compares software arm A with the Stage, shared NTT, and arithmetic-mapped
arm E at ten `(requests, workers)` points: `(1,1)`, `(2,2)`, `(4,4)`,
`(8,1/2/4/8)`, and `(16/32/64,8)`. Each of the 120 cells has one warmup
and five timed runs. The CSV records the median device makespan in cycles.

| Parameter | A/E, one request | A/E, eight requests | E eight-worker throughput / one-worker throughput at eight requests | E 64-request throughput / eight-request throughput at eight workers |
| --- | ---: | ---: | ---: | ---: |
| ML-KEM-512 | 1.552 | 1.832 | 4.307 | 1.000 |
| ML-KEM-768 | 1.517 | 1.779 | 4.348 | 1.001 |
| ML-KEM-1024 | 1.531 | 1.809 | 4.295 | 0.998 |
| ML-DSA-44 | 1.467 | 1.677 | 3.798 | 1.021 |
| ML-DSA-65 | 1.454 | 1.697 | 4.381 | 1.005 |
| ML-DSA-87 | 1.480 | 1.733 | 4.112 | 1.025 |

The throughput ratios compare `requests / makespan`. All requests pass
byte-exact known-answer checks. For each parameter and scheduling point,
the algorithm call counts match across arms and timed repetitions; their
60 hashes are in `summary.json`. The board clock reads 200 MHz before
and after the sweep. The resident image was reused without programming;
VRT metadata cannot independently attest its PDI identity. The local
`.log` and per-run `.json` files are archived in
`parameter_board_sources.zip` rather than tracked individually. The archive
also contains the runner, application binaries, and build manifest.
All 24 one/eight-request board cases have the same retired instruction
counts as the corresponding XRT runs, across all five board repetitions.

Regenerate the two plots with:

```sh
python3 pqc/results/plot_pqc_parameters.py \
  pqc/results/v80_hw_validation/parameter_board_200mhz --driver aved
```
