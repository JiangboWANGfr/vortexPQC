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
