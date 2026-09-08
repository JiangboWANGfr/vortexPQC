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
Run the manual end-to-end suite with `./ci/pqc_e2e.sh rtlsim` (or `simx`).
ML-KEM checks its upstream known-answer vectors. ML-DSA checks the complete
public key, secret key and deterministic signature against a separate portable
host implementation, in addition to verification on the device. This is a
differential check; it does not claim an independent ML-DSA standard vector.

The AVED integration smoke uses `./ci/blackbox.sh --driver=aved --target=avedsim
--app=pqc/keccak_pe --args="-b 4 -t 4 -p 4"` with the PQC extension enabled.
For the board, use a matching PQC-enabled AVED image and `--target=hw`.
The wide-configuration SimX timing residual remains open until measured and
explained; the existing parity tolerance is not increased.

The full 8-warp, 32-lane RTL probe exposed a simulation watchdog false positive:
the original and fixed-routing AGUs both stopped at timestamp 4,851,379 on
warp 5's KECCAKF. Other warps completed at 4,700,845, 4,765,725 and 4,830,305
while it queued. The scoreboard now resets its simulation-only stall counter
on acceptance by the waiting instruction's FU when its register operands are
ready. The 100,000-cycle threshold is unchanged. The target probe completes
with 13,480 retired instructions, 2,688,152 cycles and zero mismatches.

`make -C hw/unittest/issue run-watchdog` verifies a queue progressing for longer
than the timeout, a stopped FU, unrelated-FU activity and an unresolved register
dependency with same-FU activity. Restoring the original counter rejects the
progressing queue; the corrected counter still rejects all three stuck cases.
This watchdog checks lack of progress, not a fixed per-warp latency guarantee.

The AVED build also rejects two unused high bits in the LSU scheduler's modulo
temporaries. Keep the addition and comparison wide, and cast the modulo result
directly to the client-index width. This preserves wraparound for three clients
and avoids unused intermediate bits without disabling the lint checks.
