# ML-DSA pointwise mapping on the shared NTT core

## Question

The shared K/D NTT unit also implements scalar `NTTMUL.D`, but the ML-DSA
application initially used it only inside NTT/INTT. Is routing the remaining
polynomial products through that unit useful for complete signatures, and
does the L5 vector dot product need another instruction?

## Mapping

The ML-DSA-65 `poly_pointwise_montgomery` hook computes 256 independent
Montgomery products. One W32 warp assigns coefficients `lane + 32*k`,
`k=0..7`, to its lanes. Each product uses the existing `NTTMUL.D` instruction;
there is no new RTL. The scalar-C control executes the same native hook and
performs the library's signed Montgomery arithmetic on the leader lane.

The full-RAM `polyvecl_pointwise_acc_montgomery_l5` hook computes five raw
products per coefficient, accumulates them in `int64_t`, and reduces **once**.
Replacing it with five independently reduced `NTTMUL.D` values would change
the intermediate representation. The W32 mapping assigns eight output
coefficients to each lane but keeps the five-product sum and single final
reduction. Thus this arm tests software lane parallelism, not a new hardware
dot-product instruction.

The two hooks are separate `POINTWISE=ise` and `POINTWISE_L5=w32` build
arms. Both use the same 32-lane leader/warp expansion used by register NTT.
`mldsa_profile` reports call counts and per-phase hook cycles, alongside
byte-exact keypair/signature verification. Cumulative hook cycles are a
meaningful phase share for M1; with concurrent requests, the primary metric
is batch makespan because different warps' intervals can overlap.

## Initial SimX results

All arms use the same RV32IM, F/D-disabled W8T32 shared-NTT core, Pointer
Keccak, full-RAM ML-DSA-65, and seed zero. Every row passes the portable-C
byte-exact KAT.

| Pointwise arm | L5 arm | M1 request cycles | Saved vs C/C |
|---|---|---:|---:|
| C | C | 29,515,634 | -- |
| `NTTMUL.D`/W32 | C | 28,129,775 | 4.695% |
| C | W32 sum/reduce | 26,974,066 | 8.611% |
| `NTTMUL.D`/W32 | W32 sum/reduce | 25,829,676 | 12.488% |

The C/C run invokes ordinary pointwise 31 times and L5 dot products 24
times. Their measured hook cycles total 1,414,948 and 2,885,726,
respectively. The latter is larger, so an L5-specific hardware operation
should not be proposed before comparing with the W32 software mapping.

For eight different inputs (first input 1), SimX makespan falls from
77,560,227 to 68,096,270 cycles, or 12.202%, with both mappings active.
The low-RAM path also passes a distinct seed-one byte-exact KAT; it calls
ordinary pointwise 371 times and does not call the full-RAM L5 hook.

Raw logs live under `build32_im/mldsa_ntt_shared/pointwise_*.log` and the
matching binaries under `build32_im/mldsa_pointwise/`. The archive script
`pqc/results/collect_mldsa_pointwise.py` checks arms, counts, KAT markers,
binary equality across drivers, and hashes the logs and runtime.

## XRT validation and decision

Both C/C and the combined mapping pass the byte-exact ML-DSA-65 KAT on the
same XRT RTL build. The matched M1 request interval falls from 29,268,362 to
25,598,724 cycles, a 12.538% reduction. The same binaries' SimX intervals
differ from XRT by 0.845% and 0.902%, respectively. The archive script
verifies binary hashes and all KAT markers. The full
`model_parity-mldsa_pointwise_d` CI case passes on the same RV32IM W8T32
application: SimX and RTL each retire 2,287,289 instructions; device cycles
are 26,082,770 and 25,836,199, respectively, a 0.95% gap under the 5%
gate. These device totals include launch and profiling overhead, so they
are not substituted for the request intervals above.

In the mapped XRT request, both pointwise hooks together account for
620,545 measured cycles, or 2.424% of the request interval. Even eliminating
them entirely could save at most that interval under this M1 profile. The
L5-specific RTL direction therefore has a small remaining application
ceiling. Keep the W32 software mapping and existing shared multiplier bank;
do not add a separate dot-product engine. No new synthesis is needed because
the RTL is unchanged. This pointwise gain is separate from the previously
reported NTT-only hardware gain.
