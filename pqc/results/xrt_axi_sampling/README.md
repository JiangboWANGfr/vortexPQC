# XRT AXI request sampling regression

The XRT DRAM model read AR/AW/W request signals after evaluating the rising
edge, whereas it sampled response readiness before that edge. The write-credit
counter in `VX_mem_to_axi` can deassert AWVALID/WVALID at that edge. In the
write-through K512 E reproduction, both signals changed from 1 to 0 on bank 1
at timestamp 39608; the RTL accepted a write which the model did not enqueue,
so its BRESP never arrived and subsequent reads waited indefinitely.

The model now snapshots accepted requests and payloads before the edge and
processes that snapshot afterward. No RTL, clock, watchdog or parity tolerance
changed. The original `fence -n1` reproduction timed out; the corrected runtime
passes in 3136 device cycles. The independent generated-build CI configuration
`config2/fence-write-through-xrt` also passes. `fence -n256` passes in 1082485
cycles. K512 E M1 passes with 79953 instructions in both models and 4.373% device
cycle difference. Larger PQC runs are tracked in the complete-mapping campaign.

`sampling_diagnostic_sources.zip` retains failure logs, the failing simulator
library, corrected CI runtime/source, the CI definition and generated-build
command/log. The initial K512 diagnostic used the full PQC core; the small
CI test uses an independent core without PQC extensions.
These diagnostic timings are not V80 board measurements.
