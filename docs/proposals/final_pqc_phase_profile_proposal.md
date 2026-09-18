# Final W8T32 primitive profile

## Question

The shared ML-KEM/ML-DSA NTT unit has 16 signed 32x32 multiplier lanes.
Could eight lanes retain enough throughput when the scheduler switches among
eight warps? The existing 32-to-16 experiment does not answer this: 16 lanes
still accept all 16 pairs of a W32 CT butterfly in one beat, while eight
lanes would split that instruction. Warp scheduling can cover a dependent
warp's latency, but it cannot increase a shared unit's beat throughput.

| W32 operation | 16-lane bank | Eight-lane minimum |
| --- | ---: | ---: |
| CT K/D, GS D: 16 pair products | 1 beat | 2 beats |
| GS K: 16 pair products plus 16 Barrett products | 2 beats | 4 beats |
| Scalar NTTMUL K/D: 32 lane products | 2 beats | 4 beats |

The eight-lane column is a capacity lower bound, not a completed pipeline
design or a measured latency. A single multiplier would need at least 16/32
beats for these operations, so eight warps cannot make it equivalent to the
current bank under sustained demand.

The routed shared-NTT-only core attributes 80 of its 176 DSPs to the NTT
hierarchy (`pqc/results/shared_ntt_ppa.csv`). This makes a smaller bank worth
testing for area, but the report does not establish linear DSP savings.

Before changing the RTL, measure how much of each complete request is spent
in NTT and other directly attributable primitives. A small NTT fraction
limits the possible complete-request loss; it does not establish that an
eight-lane design will meet timing or sustain an eight-request batch.

## Measurement

Use RV32IM with F/D disabled, one core, W8T32, Pointer Keccak, the shared
K/D NTT unit, and the final arithmetic mappings. Keep the complete byte-exact
KAT as the correctness check. Measure M1 with direct cycle intervals and
compare the instrumented request with an uninstrumented build. M8 is a
separate makespan experiment because concurrent per-request intervals overlap.

ML-KEM already has direct absorb, permute, squeeze, NTT, INTT, mulcache,
basemul, and reduction probes. ML-DSA's native hooks can directly time
permutation, NTT, INTT, and both pointwise operations per keypair/sign/verify
phase. The ML-DSA residual includes sponge absorb/squeeze, sampling,
serialization, control, memory, and probe overhead; it must not be labeled
as any one of those components. Do not modify the pinned third-party library
solely to assign its residual.

If an eight-lane NTT design is pursued, preserve instruction encodings and
compare it with the 16-lane core under identical software and configuration:
unit arithmetic/backpressure, SimX and RTL retired instructions, M1/M8
complete-request cycles and NTT intervals, then independent Vivado 250 MHz
post-route LUT/FF/DSP and WNS. The area/performance decision follows those
measurements; no area saving is assumed from lane count alone.

## Matched M1 results

The instrumented binaries use RV32IM, F/D disabled, W8T32, Pointer Keccak,
the shared K/D NTT, and the final cooperative arithmetic mappings. SimX and
XRT use the same binary for each scheme and the matched all-extension
runtime pair. Every run passes the complete byte-exact KAT.

| Scheme / XRT | Uninstrumented request | Instrumented request | Probe overhead | Permute | Absorb+squeeze | NTT+INTT | Other mapped products | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ML-KEM-768 | 5,875,990 | 6,251,909 | 6.398% | 0.516% | 26.784% | 2.115% | 2.395% | 66.402% |
| ML-DSA-65 | 25,598,724 | 25,769,189 | 0.666% | 0.334% | not isolated | 3.251% | 2.417% | 93.997% |

For ML-KEM, "other mapped products" is mulcache plus basemul; its separate
reduction bucket is 1.788%. For ML-DSA, it is ordinary plus L5 pointwise.
The ML-DSA residual contains all unmeasured library work, including sponge
absorb/squeeze. Percentages use the instrumented request as denominator.

ML-KEM retires 559,906 instructions in both models; its instrumented SimX
and XRT intervals are 6,306,737 and 6,251,909 cycles (0.877% gap).
ML-DSA retires 2,304,694 instructions in both models; its intervals are
25,996,623 and 25,769,189 cycles (0.883% gap). The collector checks exact
instruction agreement, byte-exact KAT markers, phase sums, binary hashes,
and the existing 5% request-cycle tolerance. The structured rows, runtime
hashes, and raw-log paths are in
[`final_phase_profile.csv`](../../pqc/results/final_phase_profile.csv); regenerate
them with `python3 pqc/results/collect_final_phase_profile.py`.

These M1 fractions make the eight-lane NTT bank a plausible area experiment:
the directly measured NTT and mapped-product intervals together are 4.510%
of the KEM instrumented request and 5.668% of the DSA request. They do not
predict the M8 throughput penalty or the final DSP count. No new NTT RTL or
Vivado result is claimed by this profile.
