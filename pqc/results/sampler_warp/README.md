# Warp software matrix sampler experiment

This experiment compares `SAMPLER=warp` with the existing scalar sampler in
the **same** RV32IM W8T32 Stage + hardware NTT + arithmetic `E` configuration.
The profiler kernels call the unchanged SHAKE implementation and use the
existing NTT warp dispatcher. The new code only parses XOF bytes, rejects
out-of-range candidates, and stores accepted coefficients in stream order.
It is a software prototype in `tests/pqc/{mlkem,mldsa}_profile`, not a new
instruction or a change to the vendored crypto libraries.

Configuration: one core, eight warps, 32 threads, F/D disabled, two shared NTT
multiplier lanes; `KECCAK=sg25 UNROLL=1 NTT=reg32 NTTMUL=ise NTTBF=ise`.
ML-KEM also uses `SERIAL=1 ARITH=all ARITH_MUL=ise`; ML-DSA uses
`POINTWISE=ise POINTWISE_L5=w32 MLDSA_RAM=full`. The baseline binaries are
the archived `parameters_*_E` builds in `build32_pqc_parameters/tests/pqc/`.
The warp binaries are the `sampler_*_warp` builds there. Both use the
`build32_pqc_parameters/parameter_sweep/runtime` libraries; their hashes are
in [runtime_sha256.txt](runtime_sha256.txt). All runs passed
the full byte-exact KAT or portable-C reference check. ML-DSA inputs 1, 2,
and 3 cover distinct signing retry counts; its M8 batch uses inputs 3–10.
The exact sampler source snapshots, host executables, and paired baseline/warp
kernel binaries are in [kernels_and_sources.tar.gz](kernels_and_sources.tar.gz).
The ML-DSA M8 raw outputs are in `d65_m8_{scalar,warp}_simx.log`.

The paired cycle counts are in [request_cycles.csv](request_cycles.csv).
M1 is the complete keypair/encaps/decaps or keypair/sign/verify request. M8
uses the batch makespan, so the M1 and M8 speedups are different quantities.
The largest M1 reductions occur at larger matrix sizes; M8 gain is much
smaller (ML-KEM-768 1.019x, ML-DSA-65 1.079x). These results do not imply
the matrix sampler remains the largest bottleneck after the change; that
requires a new disjoint profile.

The ML-KEM-768 warp build retires 297,895 instructions in both SimX and XRT.
Its M1 request takes 3,917,153 versus 3,921,773 cycles, a 0.118% model
difference. The prior scalar XRT baseline takes 5,343,904 cycles. XRT uses
`XRT_DEVICE=xrtsim` and exercises the AFU path. The identical KEM-768 scalar
and warp batch inputs are visible in the KAT output. No RTL changed, so no
new synthesis result is implied by this experiment.

The ML-DSA-65/input-3 warp build also passes XRT byte-exact verification.
It retires 1,892,161 instructions in both models and takes 22,306,946 SimX
versus 22,174,120 XRT request cycles, a 0.599% difference. Both parity rows
are in [model_parity.csv](model_parity.csv); the DSA XRT raw output is
[`d65_m1_warp_xrt.log`](d65_m1_warp_xrt.log). The paired DSA speedups in the
cycle table are SimX comparisons. The full `PERF` cycle differences are
0.114% and 0.645% for KEM and DSA, respectively. An XRT scalar run for the
same input was not part of this experiment, so no DSA XRT speedup is claimed.

The native sampler hooks run only for offset zero. If the initial XOF buffer
does not supply 256 accepted coefficients, the upstream loop squeezes another
block and uses its unchanged scalar continuation. This preserves the exact
specified byte-stream order, including the rare extra-block path.
