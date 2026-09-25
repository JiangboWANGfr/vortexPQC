# V80 PQC board validation

The matched ML-KEM-768 A/B/C/D comparison uses RV32IM (F/D off), one core,
W8T32, two shared NTT multiplier lanes, a write-through D-cache, and up to
16 outstanding AXI writes per bank. A is software Keccak plus software NTT;
B uses Stage Keccak; C uses hardware NTT; D uses both. All four retain the
same arithmetic mapping.

The 150 MHz and 200 MHz images were independently routed with Vivado 2025.1.
The 200 MHz final report in `timing_multiout_200mhz.txt` has WNS +0.001 ns,
TNS 0, no hold violations, and zero routing errors. The worst setup path is
in the LSU scheduler request queue: 4.598 ns data-path delay, 4.307 ns of it
routing. The 1 ps margin is too small to assume that future implementation
changes will also close at 200 MHz.

The clock was read back from the board after programming each image:
`board_clock_multiout_150mhz_readback.log` and
`board_clock_multiout_200mhz_readback.log` contain 150000000 and 200000000 Hz.
The first smoke run after the 200 MHz reconfiguration is excluded from the
five-run timing statistics.

| Workload, arm D | 150 MHz median cycles | 150 MHz elapsed | 200 MHz median cycles | 200 MHz elapsed | Elapsed-time speedup |
|---|---:|---:|---:|---:|---:|
| ML-KEM-768, one request | 1,719,753 | 11.465 ms | 1,868,785 | 9.344 ms | 1.227x |
| ML-KEM-768, eight requests | 5,137,701 | 34.251 ms | 6,411,939 | 32.060 ms | 1.068x |

At 200 MHz, A/D is 2.357x for one request and 2.044x for eight requests.
The eight-request run retires the same 1,205,992 instructions at both clock
rates, but its core cycles increase by about 25%. This shows that the batch
contains substantial time that does not shrink with the core period; it does
not by itself identify whether memory latency, bandwidth, or internal
backpressure is responsible. The raw logs and validated medians are indexed
by `mlkem768_ablation_v80_multiout_5x.json` and
`mlkem768_ablation_v80_multiout_200mhz_5x.json`.

`mlkem768_samepdi_clock_scaling.json` repeats arm D at both clocks using the
*same* routed PDI. Only the vbin clock metadata and programmed user clock
change. Five runs per clock after a warmup give 1.227x at M1 and 1.068x at
M8, matching the independent-image comparison. Thus differing placement is
not the cause of the weak M8 clock scaling.

On the 200 MHz image, ML-KEM-512/768/1024 and ML-DSA-44/65/87 pass the
single-request and eight-request byte-exact KATs. Their logs are named
`<scheme>_full_*_multiout_v80_200mhz.log`.

`VORTEX_PROFILING=1/4/7` on the performance image reports zero extension
counters because that image was built without `PERF_ENABLE`. A separate
instrumented image is required to attribute the extra cycles; its results
must not be substituted for the uninstrumented performance numbers above.

The `PERF_ENABLE` image was routed with the same architecture and a 200 MHz
target. Its route report had WNS -0.167 ns at 200 MHz. A diagnostic PDI was
generated from that routed checkpoint after applying a temporary 190 MHz XDC
with `read_xdc -no_add`; the 190 MHz report has WNS +0.063 ns, TNS 0, and no
routing errors. The project constraint-file list was unchanged. This PDI was
loaded once, then tested at 150 and 190 MHz by changing only the user clock
and vbin clock metadata. Both clock rates were read back from the board.

`mlkem768_perf_150_190mhz_5x.json` indexes five byte-exact ML-KEM-768 arm D
runs per batch, clock, and profiling class; `collect_perf.py` regenerates it
from the raw logs. Profiling classes 1, 4, and 7 report core, D-cache, and
off-chip-memory counters, respectively. The table shows medians; MSHR and
bank stall counters sum events over cache banks, so they are not exclusive
wall-clock cycles.

| Batch | Clock | Makespan | Time | D-cache MSHR stalls | D-cache bank stalls | Off-chip reads/writes | Mean off-chip read latency |
|---|---:|---:|---:|---:|---:|---:|---:|
| M1 | 150 MHz | 1,719,539 cyc | 11.464 ms | 131,649 | 229,835 | 54,914 / 70,767 | 46.9 cyc / 313 ns |
| M1 | 190 MHz | 1,838,579 cyc | 9.677 ms | 183,741 | 228,700 | 54,913 / 70,767 | 62.6 cyc / 329 ns |
| M8 | 150 MHz | 5,130,411 cyc | 34.203 ms | 1,055,341 | 1,841,456 | 445,234 / 686,072 | 47.9 cyc / 319 ns |
| M8 | 190 MHz | 6,150,652 cyc | 32.372 ms | 1,506,208 | 1,835,587 | 446,410 / 686,072 | 64.1 cyc / 337 ns |

M1 improves 1.185x in elapsed time from 150 to 190 MHz, whereas M8 improves
only 1.057x. Both retire identical instruction counts at the two clocks.
The M8 off-chip traffic and D-cache bank stalls remain nearly constant, but
MSHR stall events rise by 42.7% and average off-chip read latency rises in
core cycles while staying near 0.3 us in absolute time. This supports memory
completion latency and MSHR backpressure as the main reason that the batch
does not scale with core frequency. The counters do not split AXI handshake
backpressure from HBM service time, and the profiling image is not used for
the reported 150/200 MHz performance speedups.
