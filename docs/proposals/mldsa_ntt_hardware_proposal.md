# ML-DSA NTT/INTT hardware path

## Scope and instruction contract

ML-DSA-65 uses q=8380417, signed 32-bit coefficients and eight NTT layers.
The existing ML-KEM unit uses q=3329 and signed 16-bit coefficients, so its
Montgomery and butterfly instructions cannot represent this transform.

Add `NTTMUL.D` and `NTTBF.{CT,GS}.D.XORs` under the NTT extension. The former
computes the library's signed Montgomery product with R=2^32. Each butterfly
operates on a full active 32-lane ALU group, with the low lane supplying the
twiddle. CT returns `a + Mont(b,zeta)` / `a - Mont(b,zeta)`; GS returns
`a + b` / `Mont(a-b,zeta)`. K and D share one half-bank of 32x32 multipliers,
the XOR lane-pairing network, issue slot, and result pipeline. Their modular
reduction paths differ, and K retains its inverse Barrett finish. A pair
consumes one multiplier; scalar multiply uses two beats. The added reduction
stage changes K latency by one cycle, so its baseline must be remeasured.

In the native ML-DSA hooks, one warp distributes 256 coefficients as eight
registers per lane. The first three forward layers and last three inverse
layers pair registers within a lane. The other five layers use the XOR
butterfly. All inverse outputs use the hardware Montgomery multiply for the
final factor 41978. The library's signed coefficient ranges and twiddle table
remain authoritative. The NTT comparison keeps pointwise arithmetic on the
library path; its later mapping to the same multiplier bank is documented in
[`mldsa_pointwise_mapping_proposal.md`](mldsa_pointwise_mapping_proposal.md).

## Verification

1. Check the scalar D instruction against signed Montgomery arithmetic at
   representative bounds, including negative coefficients and products.
2. Check CT and GS at all five XOR distances and compare complete
   key/sign/verify outputs with the portable library.
3. Run the full ML-DSA-65 keypair/sign/verify profile in SimX and XRT using
   the same RV32IM W8T32 binary. Compare pk/sk/signature byte for byte and
   retired instructions exactly; require cycle agreement within the existing
   5% gate. Measure against the same Keccak backend with software NTT.
4. Synthesize the changed core with Vivado before claiming an area or clock
   result. Keep prior result snapshots unchanged; identify the K-only PPA
   reference separately in the DAC manuscript.

## Current validation

The shared unit passes the K decoder/arithmetic/backpressure test for 100
requests at RV32 and RV64. A separate D bench passes 88 vectors covering
scalar multiplication and both butterfly orders at every XOR distance, also
at both XLENs. The D instructions are decoded under the existing NTT
extension and the disassembled RV32IM binary contains their custom encodings.

With Pointer Keccak fixed and ML-DSA-65 input zero, the full SimX protocol
passes byte-exact keypair, signature, and verification against portable C:

| Polynomial mapping | Complete request cycles | Retired instructions |
|---|---:|---:|
| W32 register + shuffle, software Montgomery | 31,573,692 | 2,870,860 |
| W32 register + shared NTTBF.D/NTTMUL.D | 29,703,554 | 2,754,196 |

This is a 5.92% cycle reduction against the matched cooperative software
mapping. It is not a 2x reduction: the larger gain against scalar library
NTT includes the W32 layout. The prior K-only routed numbers do not size the
widened shared multiplier bank.

The same shared SimX core also passes the ML-KEM-768 byte-exact KAT. With
Pointer Keccak and the remaining cooperative polynomial arithmetic fixed,
W32 software NTT takes 6,339,380 request cycles and the shared K instructions
take 6,030,216 (4.88% fewer). ML-DSA also passes with Stage Keccak plus the
shared D instructions (32,266,312 cycles), and two concurrent requests with
different inputs pass the byte-exact KAT under Pointer Keccak. These controls
show that both instruction modes compose with the existing cryptographic
paths; the concurrent D and Stage+D checks here are SimX-only controls.

The matched XRT RTL-simulation request intervals are:

| Scheme | W32 software NTT | Shared NTT instructions | Saved |
|---|---:|---:|---:|
| ML-KEM-768 | 6,309,732 | 5,999,742 | 4.913% |
| ML-DSA-65 | 31,314,709 | 29,454,908 | 5.939% |

Every row passes the complete byte-exact KAT. SimX and XRT retire identical
instructions for each of the four arms; the largest whole-launch cycle gap
is 0.879%, below the existing 5% gate. The new common RTL build also passes
eight concurrent ML-KEM requests in both models (4,296,352 instructions;
XRT 9,017,337 launch cycles). Raw-log hashes and the matched runtime hash are
in `pqc/results/shared_ntt_xrt.csv`; the collection script checks the four
binary pairs and KAT markers. This comparison fixes Pointer Keccak and all
other polynomial choices within each scheme. It does not reuse earlier
K-only performance or PPA rows as the shared-unit denominator.

The first common RV32IM W8T32 250 MHz OPT3 NTT-only implementation routed
without errors but missed timing by 0.709 ns. The worst path ran from the
issue buffer through XOR-pair selection and the signed 32x32 DSP multiplier
to its first output register. Registering the operands before the multiplier
and shortening its output shift register by one stage keeps the interface
latency unchanged while splitting that path. The revised implementation
routes with 230,739 LUTs, 137,252 FFs, 133 BRAM tiles, and 176 DSPs;
post-route WNS is +0.018 ns at 250 MHz, with zero routing errors. The
archived K-only NTT-only RV32IM core uses 220,950 LUTs, 131,798 FFs, and
112 DSPs under the same target. The revised KEM and DSA XRT requests pass
byte-exact KATs with unchanged 5,999,742 and 29,454,908 device cycles,
respectively. A second independent shared-NTT-plus-Stage implementation
also routes without errors at 250 MHz: 235,887 LUTs, 139,478 FFs, 133 BRAM
tiles, 176 DSPs, and +0.018 ns WNS. Compared with the shared NTT-only
core, Stage adds 5,148 LUTs (2.231%) and 2,226 FFs (1.622%) without adding
DSPs. Final report paths, source and report hashes, and the exact
configuration are archived in `pqc/results/shared_ntt_ppa.csv`.
