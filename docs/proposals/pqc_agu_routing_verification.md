# PQC AGU routing and pre-board verification

The V80 target is RV32, one Keccak engine per core, eight warps and 32 threads.
The processor timing check uses rtlsim; system integration uses avedsim and
ultimately the AVED hardware backend. Board execution awaits the board being installed.

The 250 MHz routed baseline has 389,378 LUT and WNS 0.000 ns. Its worst path
ends at the AGU state register through tag decoding and state-update LUTs
attributed to the response arbiter hierarchy. The 43,701 LUT reported under
that arbiter must not be interpreted as the cost of a two-input mux alone.

Every AGU request starts at a multiple of the memory lane count. Responses
retain their original tag and lane positions, including partial responses.
A state word therefore has one fixed response lane. Group selection replaces
generic indexed reads and multiport indexed writes without changing the
request count, handshake, or cycle schedule. The 32-lane RV32 path retains its
32+18-word beats. Area and timing gains require the matched synthesis runs.

Validation uses real AGU RTL with independent Keccak known answers, reversed
partial responses, request backpressure, acquire stalls, empty masks, nonfinal
packets, and both completion handshake orders. Mutation checks must reject
premature unlock and duplicate retirement. The software release probe uses an
invertible fold and must reject a word-0 bit-63 fault that the old fold hid.

Run the PQC CI from the configured build tree with `pytest ci -m pqc`.
Run the manual end-to-end suite with `./ci/pqc_e2e.sh rtlsim` (or `simx` or `avedsim`).
ML-KEM checks its upstream known-answer vectors. ML-DSA checks the complete
public key, secret key and deterministic signature against a separate portable
host implementation, in addition to verification on the device. This is a
differential check; it does not claim an independent ML-DSA standard vector.

The AVED integration smoke uses `./ci/blackbox.sh --driver=aved --target=avedsim
--app=pqc/keccak_pe --args="-b 4 -t 4 -p 4"` with the PQC extension enabled.
For the board, use a matching PQC-enabled AVED image and `--target=hw`.
The wide-configuration SimX timing residual remains open until measured and
explained; the existing parity tolerance is not increased.

The full 8-warp, 32-lane probes exposed an undersized simulation watchdog
budget. In AVED, a normally completing KECCAKF took 181,786 cycles, exceeding
the old 100,000-cycle limit. CSR reads also wait behind queued KECCAKF packets;
the resulting dependent branch can time out while the PE still makes progress.
With the original per-warp watchdog logic and a 32-lane budget, the full AVED
probe completes with 13,480 instructions, 4,951,308 cycles and zero mismatches.

For PQC-enabled configurations, the default debug timeout now scales by the
warp's thread count, accounting for the serial lane service. It remains 100,000
cycles without PQC (before the existing cache-level multiplier). The counter
retains its original per-warp semantics: unrelated activity cannot indefinitely
hide a lost writeback or a starved warp. This changes a simulation timeout,
not execution timing or the 5% model-parity tolerance.

`make -C hw/unittest/issue run-watchdog` checks a legal serial queue exceeding
the unscaled budget, a stopped FU, unrelated-FU activity and an unresolved
register dependency despite same-FU activity. All stuck cases must still fail.
`PARAMS=-DVX_DBG_STALL_TIMEOUT=100000` restores the old budget as a
negative control and must reject the legal queue.

The AVED build also rejects two unused high bits in the LSU scheduler's modulo
temporaries. Keep the addition and comparison wide, and cast the modulo result
directly to the client-index width. This preserves wraparound for three clients
and avoids unused intermediate bits without disabling the lint checks.

## Verified implementation

Commit `5c684929a` has the same source tree as isolated verification commit
`24a5d565d`: PQC CI passed 35/35 checks and ordinary-core model parity passed
8/8. The PQC total comprises 24 functional driver runs, three parity pairs,
four unit checks, one RTU-only run and three AVED integration checks. The new
SFU test uses production RTU and PQC completion producers with three overlapping
RTU returns and both immediate and delayed commit acceptance. Duplicate-result
and lost-unlock mutations both fail this test.

SimX had treated dispatch credits as a preference and ignored congestion when
all eligible warps targeted full units. RTL gates readiness on those credits.
Matching that gate reduces the 4-thread, 8-warp `-b 8 -t 1 -p 8` whole-run gap
from 9.01% to 3.97%. The 1/4-warp gaps are 2.21%/1.23%; all retain the 5% gate.
The 32-thread timing residual remains open: `-b 8 -t 32 -p 1` differs by 6.82%
in whole-run cycles and 7.80% in kernel span. A 32-thread vecadd control differs
by 1.02%, with identical cycles when the unused PQC extension is enabled or
disabled. A fixed platform-wide offset does not explain all these workloads.

Both default and 8w×32t processor RTL configurations, plus 8w×32t AVED
simulation, pass ML-KEM known-answer and ML-DSA full-byte portable-C
differential checks. AVED's ML-KEM run retires the same 1,163,708 instructions
as rtlsim and takes 11,979,330 versus 11,979,295 whole-run cycles. Its ML-DSA
timed region takes 72,355,469 versus 72,355,410 cycles. The hardware AVED driver
also compiles and resolves its dynamic symbols against the SLASH fork; upstream
SLASH lacks the required host-buffer API. This checks the driver build, without
opening a device.

The same portable ML-DSA-65 host oracle also passes 115 checks from
[NIST ACVP v1.1.0.41](https://github.com/usnistgov/ACVP-Server/tree/v1.1.0.41/gen-val/json-files):
25 key-generation, 60 internal signing and 30 internal verification cases,
including rejection of 24 invalid signatures. The downloaded inputs and
expected outputs have recorded SHA-256 digests. These ACVP inputs were tested
on the host oracle; the device runs above retain their differential-test scope.

Matched V80 implementations compare PQC-enabled baseline `94708d50c` with
optimized `dabf107d2`; the latter differs from the merged RTL only in a disabled
diagnostic print. The comparison includes integration fixes, not just routing.

| Configuration | Baseline LUT / WNS (ns) | Optimized LUT / WNS (ns) | LUT change |
| --- | ---: | ---: | ---: |
| 8w×4t, 300 MHz | 53,194 / +0.111 | 48,894 / +0.129 | −8.08% |
| 8w×32t, 250 MHz | 390,411 / 0.000 | 334,917 / 0.000 | −14.21% |

At 32 threads, the post-synthesis response-arbiter hierarchy drops from 44,340
to 1,595 LUT. This supports the dynamic-indexing diagnosis; the 32+18-word
request geometry remains intact. The routed 250 MHz result still has zero
setup margin and does not establish full-AFU timing or a new maximum frequency.

Full-image preparation exposed two SLASH dependency fixes. Commit `b57195f5`
parses setup/hold slack by column label and gates both compute and service
static shells; the old parser could accept negative hold slack. Commit
`c6c1050a` carries the requested job count through the static-shell launchers,
which previously used 14 jobs even when passed `--jobs=1`. The 139 linker tests
pass, including failure controls against the preceding code. Build the common
HLS IP repository before installing the static shell; a missing `hbm_bandwidth`
IP otherwise leads to a secondary circular block-design error.

Full static-shell implementation exposed two clock-constraint defects in the
SLASH dependency. `7cbc7b2b` selects `No_buffer` for the three Clocking Wizards
fed by CIPS and retains their 100 ps input jitter on the shared `clk_pl_3`.
The previous default created three redundant primary clocks on internal pins.
A controlled project-level STA comparison preserves WNS +0.002 ns and WHS
+0.007 ns while removing those clocks; implementation from regenerated IPs
is required before signing off the resulting shell.

`5ffeaa10` expresses the requested kernel rate as a generated clock instead of
replacing the MMCM output with a primary clock. A real MMCME5 probe consumes
the emitted XDC: the old constraints fail the master-clock check, while the
new ones retain one 250 MHz clock and its reference. Both the new constraint
and a 250 MHz MMCM configuration report 2.809 ns setup slack in this small
probe. Runtime-frequency STA can still report TIMING-1 because the shell's
boot MMCM registers describe 200 MHz; board validation must record the actual
programmed rate and clock registers. Complete-static-region STA at the runtime
rate and final RM timing remain required in addition to the small probe.

AMC firmware builds with the installed `armr5-none-eabi-gcc` once
`/data/Xilinx/2025.1/gnu/armr5/lin/gcc-arm-none-eabi/bin` is on `PATH`.
The generated BSP can be reused when resuming compilation after this environment
error. The board-side ML-KEM and ML-DSA kernels are byte-identical to the kernels
that passed the target AVED simulation checks.

The first regenerated-clock implementation was rejected despite positive timing:
two exported XDC files from the project-level STA diagnostic remained in its
constraint set and reintroduced the old primary clocks. Removing them and
rebuilding produces 39 clocks, WNS +0.001 ns, WHS +0.011 ns and WPWS 0.000 ns,
with zero failing setup, hold or pulse-width endpoints. The routed design has
no DRC errors or clock-related Critical Warnings. Temporary STA imports now
use `read_xdc -no_add` and verify that the project file list is unchanged.

Resuming implementation from a checkpoint loses the nonproject subprocess's
IP repository setting. Restoring the project's repository paths in the image
pre-hook lets the original UUID update script run; readback matches the
synthesized-checkpoint digest `356ef9dbb6983008146b803cc7b555fc`.
Device-image generation succeeds while retaining the routed checkpoint.
The full static region, with the reference bandwidth RM black-boxed, passes
at boot 200 MHz (WNS +0.427 ns / WHS +0.013 ns) and runtime 250 MHz
(+0.311 ns / +0.013 ns), with zero failing setup, hold or pulse-width endpoints.
The runtime constraint reports the expected TIMING-1 mismatch with the boot
MMCM parameters; actual programmed-clock readback remains a board check.
All 486 declared pin properties match the 245 ports in the routed design,
including six scalar reference-clock ports renamed to single-bit buses by
implementation. A wrong-package-pin control is rejected by the checker.
Final PQC RM timing and DFX compatibility checks remain required.

Final RM linking exposed nine NoC boundary mismatches in the compute template:
`M04_INI` through `M11_INI` advertised two extra address windows, and the QDMA
window differed from the implemented compute shell. SLASH `3e97b088` matches
these declarations to the existing static boundary. A regression compares all
86 boundary declarations and rejects the original nine differences; all 140
linker tests pass. The corrected Vivado run reaches logic optimization without
the previous `ModularNoC 90-3` merge failure. The active GPU/staging mappings and
RV32 stack coverage also pass again; final routing and DFX checks remain pending.

AVED simulation's in-process C API does not execute the hardware C++ VRT
staging code. A separate `TARGET=sim` run using an actual simulation vbin and
VRT, including `VORTEX_AVED_FORCE_STAGE=1`, is queued to cover that path
without requiring a board.

The legacy xrtsim entry point also needs direct coverage: its extension source
list currently omits the PQC directory. Default-core and PQC combined/full-width
smokes are queued after the VRT simulation work to obtain a failing control
before repairing that build entry point.
