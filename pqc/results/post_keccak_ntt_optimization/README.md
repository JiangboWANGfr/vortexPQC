# Post-Keccak/NTT optimization results

This directory records the bottleneck study after the SG25 Keccak and shared
K/D NTT hardware paths were enabled.  The paper sources are intentionally not
modified by this experiment.

## Fixed configuration

- RV32IM; F and D disabled
- one core, eight resident warps, 32 threads per warp
- two shared NTT multiplier lanes
- SG25 Keccak, register-resident NTT/INTT, SG2 butterflies
- exact byte-for-byte KAT checking

The latency profile enables cooperative zeroization only for one resident
request.  The throughput profile uses scalar zeroization because entering a
32-lane collective for each small wipe slightly increases the eight-request
makespan.

## Findings

The added W32 linear mapping, zeroization, and aligned 32-bit SG25 sponge I/O
reduce single-request cycles over the already optimized post-Keccak/NTT
baseline by 25.30% for ML-KEM-512, 21.61% for ML-KEM-768, and 18.46% for
ML-KEM-1024.  Eight-request full-run cycles fall by 10.41%, 7.73%, and 5.51%.
Cooperative zeroization alone reduces the final ML-DSA latency by 7.02%,
7.70%, and 9.64% for parameter sets 44, 65, and 87 on deterministic input 3.

The fully instrumented ML-KEM-768 run attributes 31.44% to the complete
Keccak data path (absorb, permutation, and squeeze), 13.64% to matrix rejection
sampling, and 4.79% to NTT plus INTT.  No arithmetic primitive dominates the
remaining 30.48% residual.  Replacing eight byte accesses with two aligned
32-bit accesses per full state word reduces the instrumented absorb and
squeeze intervals by 7.71% and 11.32%, respectively.

The fully instrumented ML-DSA-65 input-3 run attributes 17.44% to Keccak
permutations, 6.04% to NTT plus INTT, and 4.53% to pointwise multiplication.
Its residual spans matrix generation and the variable-retry signing path,
including hint preparation, z processing, packing, decomposition, and control
overhead.  It must not be interpreted as one unnamed primitive.

These measurements do not justify another specialized instruction.  The next
hardware candidate would need a separate experiment that isolates Keccak
absorb/squeeze activation and data movement, or ordered rejection compaction.
Both current improvements reuse the existing ALU lanes and NTT multiplier.

## Validation

All SimX rows in `performance.csv` pass exact KAT checks.  ML-KEM-768 also
passes the XRT integration path at both M1 and M8.  SimX and XRT retire exactly
126,499 instructions at M1 and 1,202,216 instructions at M8; XRT cycles differ
by 1.569% and 3.002%, respectively, below the 5% model-parity threshold.

ML-DSA-65 input 3 passes byte-for-byte on SimX, rtlsim, and XRT for keypair,
keypair plus signing, and the complete keypair/sign/verify request.  Each scope
retires exactly the same number of instructions on all three paths.  For the
complete request, SimX, rtlsim, and XRT report 13,772,331, 13,689,233, and
13,704,765 cycles; the SimX-to-XRT gap is 0.493%.  The phase results are in
`mldsa_rtl_parity.csv`, with raw logs under `logs/rtl_phase/`.

The earlier two-hour ML-DSA XRT run used a stale `libxrtsim.so` built without
`VX_CFG_EXT_PQC_ENABLE` and `VX_CFG_NTT_MUL_LANES=2`.  A keypair-only bisection
made the mismatch reproducible at public-key byte 32 and secret-key byte 64.
Cleaning and rebuilding XRT/xrtsim with the fixed configuration removed the
mismatch and restored exact instruction parity.  The old run is retained as
an invalid stale-runtime result; `xrtsim_rebuild_diagnosis.txt` records the old
and new library hashes and `xrtsim_config_final.stamp` records the valid build.

Profiling adds timestamps and counters, so the percentages in
`phase_attribution.csv` use the instrumented request total.  They must not be
applied directly to the lower non-profile cycle totals.
