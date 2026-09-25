# Stage + NTT + arithmetic bottleneck attribution

This is a single-request diagnostic of the existing `E` mapping on RV32IM,
one core, W8T32, two shared NTT multiplier lanes, F/D disabled. It does not
change the paper or the production kernels. The probes were built in the
ignored `build32_im/pqc_bottleneck_shadow` tree. Exact probe sources and device
binaries are in [probe_sources.zip](probe_sources.zip); raw outputs are in
[`raw/`](raw/). The repository was at `24a74cdb8912091ff154479173d99eaffc993a91`
with pre-existing local changes in the PQC test sources, which are included in
the archive. The archive's `v5/` directory contains the later inner-loop probe
overrides and binaries; the top-level source snapshot is the disjoint-profile
probe. Runtime library hashes are in [runtime_sha256.txt](runtime_sha256.txt).

The build uses `KECCAK=sg25`, `NTT=reg32`, `NTTMUL=ise`, `NTTBF=ise`,
`PROFILE_PHASES=1`, `CONFIGS` with `VX_CFG_EXT_F_DISABLE`,
`VX_CFG_EXT_D_DISABLE`, PQC/NTT/KSG25/KROUND25 enabled, eight warps,
32 threads, and `VX_CFG_NTT_MUL_LANES=2`. ML-KEM also uses `SERIAL=1`,
`ARITH=all`, `ARITH_MUL=ise`; ML-DSA uses `POINTWISE=ise`,
`POINTWISE_L5=w32`, `MLDSA_RAM=full`. All outputs pass the portable-C
byte-exact KAT. The `xrt` driver runs `XRT_DEVICE=xrtsim`, including the AFU
surface; its runtime is the archived parameter-sweep runtime.

| ML-KEM request | Driver | Total cycles | Matrix rejection, excluding SHAKE | Share | Noise, excluding SHAKE | Pack/unpack |
|---|---|---:|---:|---:|---:|---:|
| 512 | SimX | 3,401,095 | 960,884 | 28.25% | 415,940 | 407,979 |
| 768 | SimX | 5,810,983 | 2,181,376 | 37.54% | 557,844 | 625,108 |
| 768 | XRT | 5,782,193 | 2,171,678 | 37.56% | 563,016 | 617,810 |
| 1024 | SimX | 9,061,186 | 3,924,880 | 43.32% | 714,311 | 875,809 |

For ML-KEM-768/XRT, the old `other` category was 4,473,795 cycles (77.37%).
The disjoint rejection, noise, and pack/unpack intervals account for
3,352,504 of those cycles, leaving 1,121,291 cycles (19.39% of the full
request) still unassigned. `matrix_nonshake` is an inclusive parent of
`rej_nonshake`; do not add them. The matched KEM-768 SimX/XRT probe retired
500,322 instructions in both models; request cycles differ by 0.50%.
The former `other` value was the remainder after a short list of primitive
timers, not one function. Likewise, keypair/encapsulation/decapsulation and
keypair/sign/verify are enclosing API phases; their shares locate a slow
request phase but do not identify its slow primitive.

| ML-DSA request | SimX total cycles | Signing attempts | Matrix uniform sampling, excluding SHAKE | Share | SHAKE absorb + squeeze, excluding permutation | Share |
|---|---:|---:|---:|---:|---:|---:|
| 44, input 1 | 22,185,216 | 4 | 4,023,527 | 18.14% | 4,119,912 | 18.57% |
| 65, input 1 | 45,594,811 | 8 | 7,581,966 | 16.63% | 8,420,717 | 18.47% |
| 65, input 2 | 42,738,635 | 7 | 7,582,079 | 17.74% | 8,072,854 | 18.89% |
| 65, input 3 | 28,633,045 | 2 | 7,579,844 | 26.47% | 6,333,315 | 22.12% |
| 87, input 1 | 49,277,646 | 3 | 14,083,583 | 28.58% | 11,118,691 | 22.56% |

The DSA-65/input-3 v2 probe also ran through XRT. Its matched SimX and XRT
binaries retire exactly 2,556,316 instructions; request cycles are
28,641,683 and 28,387,273 (0.90% difference against XRT). XRT attributes
7,427,298 cycles (26.16%) to matrix uniform sampling and 6,314,989 cycles
(22.25%) to SHAKE absorb/squeeze data handling. Matched SimX shares are
26.46% and 22.11%. This v2 probe lacks the exclusive sign-body counters used
in the table above; compare it only with its own v2 SimX run.

ML-DSA matrix generation is run for keypair, sign, and verify. The uniform
sampler cost is almost unchanged between the three ML-DSA-65 inputs, while
signing varies with the rejection loop. For input 1, the eight signing
attempts consume 24.66 million *inclusive* cycles. The exclusive sign-body
probe (after subtracting measured SHAKE, NTT/INTT, and pointwise intervals)
attributes 6.33 million to decompose/pack `w1`, 4.96 million to compute/check/
pack `z`, 3.37 million to hint processing, and 1.66 million to sampling `y`.
The full-request disjoint residual is 5.83 million cycles (12.79%). These
sign-body intervals and the inclusive attempt time must not be added together.

The current bottleneck shared by both algorithms is matrix coefficient
generation: decode, reject, and store a deterministic XOF byte stream.
Its cost scales with the matrix dimensions, from 12 to 48 sampled polynomials
per KEM request and from 48 to 168 per DSA request. A warp-parallel software
sampler that preserves byte order and exact output is the next useful test.
The decoders differ: KEM extracts two 12-bit candidates per three bytes;
DSA extracts one 23-bit candidate per three bytes. They can share an ordered
lane-prefix/compaction approach, but a single new sampler instruction is not
justified until the software mapping is measured.
SHAKE absorb/squeeze data handling is another shared candidate. More Keccak
permutation hardware or more NTT multiplier lanes does not directly address
these measured intervals. ML-DSA additionally needs sign-specific work on
decompose/packing and retries.

All percentages use the *instrumented* request as denominator. The KEM-768
probe is 8.20% slower than the uninstrumented XRT `E` headline
(5,343,904 cycles); most of this comes from the pre-existing phase-profile
instrumentation. The DSA-65 input-1 exclusive probe is 0.78% slower than its
uninstrumented SimX `E` headline (45,243,225 cycles). These are diagnostic
attributions, not new end-to-end speedup claims.

A separate inner-loop SimX probe places 1,646,599 of 2,028,562 KEM-768
non-SHAKE sampling cycles (81.2%) and 5,642,304 of 6,400,904 DSA-65
non-SHAKE sampling cycles (88.1%) in the byte-decode/filter/store loop itself.
This probe changes compiler code generation and reduces whole-request cycles
by about 2.5%; its ratios are a localization check and must not be mixed
into the disjoint tables above.
The matching KEM core-loop XRT run finds 1,620,256 of 2,013,411 sampling
cycles (80.5%) in the loop and passes the byte-exact KAT. Request cycles are
within 0.56% of SimX, but the probe retires 501,570 versus 501,632
instructions. Therefore the inner-loop probe does not satisfy the exact
instruction-parity criterion; the earlier disjoint-profile KEM-768 pair does.
