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

## Eight-request XRT follow-up

The matched follow-up fixes RV32IM with F/D disabled, one core, W8T32,
Pointer Keccak, the shared 16-lane K/D NTT bank, and full-RAM ML-DSA-65.
Both arms execute input IDs 1 through 8 concurrently (`-b 8 -s 1`). The
control uses C for both pointwise hooks; the mapped arm uses `NTTMUL.D`
and the W32 L5 sum. No phase-detail probes are enabled. The existing
per-hook counters and timestamps are retained in both arms.

After reconfiguring `build32_im`, rebuild from the generated
`tests/pqc/mldsa_profile/Makefile` in separate sibling directories
`tests/pqc/mldsa_m8_c` and `tests/pqc/mldsa_m8_both`. Use
`LIBC_PATH=/home/jiangbowang/aphdcode/vortex_v80/toolchains-im/libc32`,
`LIBCRT_PATH=/home/jiangbowang/aphdcode/vortex_v80/toolchains-im/libcrt32`,
`KECCAK=pe MLDSA_RAM=full NTT=reg32 NTTMUL=ise NTTBF=ise`, and add
`POINTWISE=ise POINTWISE_L5=w32` only for the mapped arm. Both application
builds match the archived runtime's configuration:

```text
-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE
-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_EXT_NTT_ENABLE
-DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_EXT_KROUND25_ENABLE
-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32
```

Run each application directory with the same archived runtime pair:

```sh
env LD_LIBRARY_PATH=/home/jiangbowang/aphdcode/vortex_v80/vortexPQC/build32_im/mldsa_ntt_shared/runtime \
    VORTEX_DRIVER=xrt XRT_DEVICE=xrtsim VORTEX_PROFILING=0 \
    ./mldsa_profile -b 8 -s 1 > m8_xrt.log 2>&1
env LD_LIBRARY_PATH=/home/jiangbowang/aphdcode/vortex_v80/vortexPQC/build32_im/mldsa_ntt_shared/runtime \
    VORTEX_DRIVER=simx XRT_DEVICE=xrtsim VORTEX_PROFILING=0 \
    ./mldsa_profile -b 8 -s 1 > m8_simx.log 2>&1
```

The mapped kernel is byte-identical to the earlier mapped arm
(`ce5ee86e8218b1303c12f13c380243788c37732765e7056f24604a9eba76c622`).
The freshly compiled control has hash
`3502b82c5e3221bca4be57c148cd3562d0b41f4d711292738d472b8b826fb6f0`;
therefore both models rerun it instead of reusing the earlier control's
cycles. The prior `mldsa_pointwise.csv` snapshot is preserved.

The collector `pqc/results/collect_mldsa_pointwise_m8.py` requires 32
byte-exact request checks across two arms and two models, identical
primitive-call counts for each input, identical retired instructions across
models, and at most 5% model disagreement for both launch cycles and batch
makespan. Per-request intervals overlap; their sum is not batch latency.
The eight inputs exercise signing variability but do not establish a
population tail-latency bound or isolated per-input signing latency.

| Model | C/C makespan | Mapped makespan | Reduction |
| --- | ---: | ---: | ---: |
| simx | 77,528,313 | 68,096,270 | 12.166% |
| xrt | 77,071,268 | 68,063,840 | 11.687% |

Both models retire exactly 33,555,891 instructions for C/C and 26,293,053
for the mapped arm. The C/C model gaps are 0.593% for both device cycles
and makespan; the mapped gaps are 0.056% and 0.048%, respectively. All
pass the existing 5% threshold.

All 32 request checks pass. Input IDs 1--8 exercise 8, 7, 2, 9, 3, 2, 11,
and 8 signing attempts, respectively, derived from full-RAM L5 counts:
keypair and verification each make six calls, and each signing attempt
makes six more. The primitive counts match across arms and models, and
all allocator checks pass. These are concurrent request intervals, not
isolated signing latencies.

Structured results are in `pqc/results/mldsa_pointwise_m8.csv`; the raw
logs, build commands, launch manifest, and measured binaries are archived
in `pqc/results/mldsa_pointwise_m8_sources.zip`. Regenerate the CSV from
`build32_im` with `python3 ../pqc/results/collect_mldsa_pointwise_m8.py`.
