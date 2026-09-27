# Complete-mapping write-back diagnostic (incomplete, failed validation)

This exploratory queue used the older simulation recipe's write-back D-cache,
whereas the 200-MHz V80 image uses write-through. It was stopped after all
five K512 eight-worker XRT arms reached scoreboard timeouts. K512's five
single-worker arms passed their KATs with exactly matching SimX/XRT retired
instructions, but device-cycle gaps of 6.642--7.480% failed the existing 5%
bound. SimX's fence controller waits for pending loads and does not implement
the RTL's complete write-back flush; the timeout root cause has not been
traced. These records must not be presented as a passed parameter sweep.

`writeback_diagnostic_sources.zip` contains raw logs/statuses (including
interrupted jobs), plans, the stop record, source snapshots, all host/kernel
binaries, disassemblies, mapping audits and both simulator runtimes. The
remaining planned jobs were intentionally cancelled, not measured.

The independent write-through build is `build32_pqc_parameters_full_wt`.
All 30 host/kernel binaries are byte-identical to the board-tested binaries;
only the simulator core configuration changes. The matching experiment's
results are kept in `pqc/results/parameter_scaling_full/`. Its success would
not resolve this separate write-back issue.
