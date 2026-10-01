# Complete-mapping write-through simulation evidence

The full six-parameter A–E/scaling campaign completed **312/312 runs**:
156 SimX and 156 XRT runs. All runs exited zero and ended with `PASSED!`;
3,948 requests passed byte-exact reference checks. The last job finished on
2026-09-30 at 05:33:26 UTC+08:00. No measurements were rerun for this archive.

The configuration is RV32IM, W8T32, M2, F/D disabled, write-through D-cache,
and the full surrounding software mapping. The source revision and exact
flags are recorded in the archived manifest (`6e5cf178a9`). This is the
`build32_pqc_parameters_full_wt` campaign, separate from the earlier failed
write-back diagnostic.

## Remaining timing-model failure

All 156 SimX/XRT pairs retire identical instruction counts. **155/156 pairs
pass the unchanged 5% device-cycle gate.** The remaining pair is D87, mode E,
eight requests, four workers, input start 1:

| Metric | SimX | XRT |
| --- | ---: | ---: |
| Retired instructions | 14,401,161 | 14,401,161 |
| Device cycles | 92,797,847 | 97,885,089 |
| Request makespan | 92,559,833 | 97,658,408 |

The device-cycle gap is 5.197157%; the request-interval gap is 5.220825%.
Both executions pass functional checks. This archive does **not** certify
complete timing-model parity. The collector returns 1 and its normal
`--archive` path rejects this result; neither gate nor tolerance was changed.
The remaining mismatch still needs a timing-model investigation.

## Saved evidence

`pqc_parameters_evidence.zip` explicitly preserves the completed campaign
including the timing failure. It contains raw logs/statuses/plans, the source
manifest and snapshots, opcode audits, all 30 host/kernel binaries and their
disassembly/configuration, the exact simulator runtimes, collection scripts,
derived tables, and this validation record. `archive_manifest.json` lists
every member's SHA256; `SHA256SUMS` records the archive itself. Historical
supervisor status files are retained verbatim and may describe an earlier
intermediate state; use `validation.json` and `pqc_parameters_status.json`
for final coverage.

All 30 host/kernel binary pairs match the V80 board cohort. All 156 board
configurations, each repeated five times (780 timed executions), match their
corresponding XRT retired-instruction counts. The board comparison table is
included in the evidence ZIP and in
`../v80_hw_validation/parameter_full_board_200mhz/board_xrt_instruction_parity.csv`.
The board results are independent 200-MHz measurements; this simulation
archive does not replace their latency or speedup measurements.
