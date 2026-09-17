# RV32 SG25 Keccak: staged subgroup instructions and whole-round comparator

Status: the software baseline, all six RV32 stage instructions, and the
two-instruction `KROUND25` comparator are implemented. The stage design has
SimX and full-core XRT correctness, eight-arm timing, matched full-core PPA,
and end-to-end ML-KEM results. KROUND25 has RTL unit, SimX/rtlsim core,
full-core XRT, isolated- and full-core PPA, SHAKE, and ML-KEM results.

The initial platform is RV32, one core, eight warps, and 32 threads per warp,
matching the final selection in
[`core_config_v80.csv`](../../pqc/results/core_config_v80.csv). Keep the pointer
`KECCAKF` as the dedicated-engine baseline and the
[SG5 proposal](../../pqc/docs/proposals/cooperative_ise_proposal.md) as a layout
comparison. RV64 and SG5-specific instructions are deferred: changing the whole
core's word width would confound the first collective-versus-engine experiment.

## 1. Layout and execution contract

One warp holds one Keccak-f[1600] state. For `0 <= x,y < 5`, lane `t = x + 5*y`
holds `A[x,y]`; two ordinary RV32 GPRs hold its low and high 32 bits. The state
byte order is little endian within each word, then increasing `t`. The same
layout holds at all three stage boundaries.

The extension is enabled with `VX_CFG_EXT_KSG25_ENABLE`, independently of the
pointer PE's `VX_CFG_EXT_PQC_ENABLE`. It requires `XLEN=32`, `NUM_THREADS=32`,
`SIMD_WIDTH=32`, and `NUM_ALU_LANES=32`. Warp width alone is insufficient: a narrower, packetized ALU
would need an operand collection design outside version 1. SimX compilation
and RTL simulation assert these requirements. RTL assertion macros are removed
for synthesis, so a synthesis configuration must first pass simulation;
synthesis alone does not validate this contract. Disabled-extension encodings
follow the existing unsupported-instruction behavior.

All 32 lanes must execute each collective convergently at the same instruction,
with the full execution mask. Lanes 25–31 contribute no state and produce zero
for every stage instruction. They must still execute the instruction; software
must not place collectives inside `if (lane < 25)`. Loads and stores may use
lane guards provided execution reconverges before the next collective.

Partial-mask execution, nonuniform round operands, or round values outside
`0..23` violate this version's programming contract and have no specified
result. SimX and RTL debug checks must diagnose those cases. This proposal does
not introduce a runtime trap ABI or masked-collective fallback.

Each instruction reads a snapshot of both complete source vectors before any
destination lane changes. `rd` may alias either source register within that
instruction. Normal zero-register behavior applies. There are no memory
operands, memory-ordering effects, implicit state registers, or hidden round CSR.
Ordinary data dependencies still apply between instructions.

## 2. Six single-destination instructions

Each instruction reads at most two register vectors and writes one 32-bit
destination vector. All six use CUSTOM0 (`0x0b`), funct7 `0x06`: funct3 `0/1` selects THETA.L/H,
`2/3` selects RHOPI.L/H, and `4/5` selects CHII.L/H. `VX_ISA_EXT_KSG25`
(extended MISA bit 12, device capability bit 44) advertises the implemented
stage extension. This unmerged ISA draft now includes all three stages.

For THETA and RHOPI, define the input word at lane `t` as
`A[t] = uint64(rs1[t]) | (uint64(rs2[t]) << 32)`. Let `lo(v)` and `hi(v)` select
bits `31:0` and `63:32`. All coordinates below wrap modulo 5, shifts are unsigned,
and `ROL64(v, 0) = v`.

| Instruction | Sources | Result at a state lane |
|---|---|---|
| `KTHETA.L.SG25 rd, alo, ahi` | Low/high state halves | `lo(T[x,y])` |
| `KTHETA.H.SG25 rd, alo, ahi` | Low/high state halves | `hi(T[x,y])` |
| `KRHOPI.L.SG25 rd, alo, ahi` | Low/high theta output | `lo(B[x,y])` |
| `KRHOPI.H.SG25 rd, alo, ahi` | Low/high theta output | `hi(B[x,y])` |
| `KCHII.L.SG25 rd, blo, round` | Low rho/pi output; uniform round index | `U_lo[x,y]` |
| `KCHII.H.SG25 rd, bhi, round` | High rho/pi output; uniform round index | `U_hi[x,y]` |

### THETA

```text
C[x]   = A[x,0] ^ A[x,1] ^ A[x,2] ^ A[x,3] ^ A[x,4]
T[x,y] = A[x,y] ^ C[x-1] ^ ROL64(C[x+1], 1)
```

Both halves consume both source vectors: bit 63 of the neighboring column's
parity contributes to the low result, and bit 31 contributes to the high result.
This operation includes the local state XOR, not just the correction `D[x]`.

### RHOPI

The forward permutation is `B[y, 2*x+3*y] = ROL64(A[x,y], r[x,y])`. Hardware
and shuffle software can express it as a gather. At destination `(xd,yd)`:

```text
xs = (xd + 3*yd) mod 5
ys = xd
B[xd,yd] = ROL64(A[xs,ys], r[xs,ys])
```

Rotation amounts belong to the source coordinates, not the destination. The
table below lists `r[x,y]` with rows indexed by `y` and columns by `x`:

| y \ x | 0 | 1 | 2 | 3 | 4 |
|---|---:|---:|---:|---:|---:|
| 0 | 0 | 1 | 62 | 28 | 27 |
| 1 | 36 | 44 | 6 | 55 | 20 |
| 2 | 3 | 10 | 43 | 25 | 39 |
| 3 | 41 | 45 | 15 | 21 | 8 |
| 4 | 18 | 2 | 61 | 56 | 14 |

### CHII

For either half, let `V[x,y]` be `rs1[x+5*y]`, and let `q` be the common
unsigned value of `rs2` across all 32 lanes:

```text
U_half[x,y] = V[x,y] ^ ((~V[x+1,y]) & V[x+2,y])
U_half[0,0] = U_half[0,0] ^ half(RC[q])
```

The complement is 32 bits wide. Only lane 0 applies the round constant.
`RC[0..23]` are the standard Keccak-f[1600] constants, also present in
[`mlk_KeccakF_RoundConstants`](../../pqc/third_party/mlkem-native/mlkem/src/fips202/keccakf1600.c).
The `.L` and `.H` forms select the corresponding constant half. No round
counter persists in the execution unit; software supplies `q` on every call.

### Source lifetime and issue count

Snapshot semantics apply to a single instruction, not an `.L/.H` pair. Both
THETA and RHOPI instructions must see the same old halves. A valid round uses
distinct temporary outputs before changing the input pair:

```text
tlo = KTHETA.L.SG25(alo, ahi)
thi = KTHETA.H.SG25(alo, ahi)
blo = KRHOPI.L.SG25(tlo, thi)
bhi = KRHOPI.H.SG25(tlo, thi)
alo = KCHII.L.SG25(blo, q)
ahi = KCHII.H.SG25(bhi, q)
```

Register allocation may reuse storage after its last read. It must not overwrite
`alo` with THETA.L before THETA.H reads it, nor overwrite `tlo` with RHOPI.L
before RHOPI.H reads it. Inline wrappers must preserve those lifetimes. CHII's
halves have no cross-half dependency. Two GPRs describe persistent state capacity,
not peak live-register usage; temporaries, pointers, and loop state also count.

A round has six collective issues plus ordinary software overhead. This is
neither a six-cycle latency claim nor a guarantee of one issue each cycle.

## 3. Software baseline first

`tests/pqc/keccak_sg25` uses existing `vx_shfl_idx` for all cross-lane traffic.
It implements all 24 rounds and complete SHAKE128/256, with rates 168/136 bytes,
SHAKE domain separation, and padding. Keep the two state halves live across
absorb, permutation, and squeeze in one kernel. Absorb uses ordinary load/XOR;
squeeze uses ordinary stores. The compiler's actual allocation must be checked
before calling the implementation spill-free or register-resident.

All lanes execute shuffles; padding lanes use valid source indices and discard
their results. Round-trace stores are allowed only in correctness mode, so they
cannot contaminate permutation traffic or throughput measurements.

The initial correctness gate on SimX requires:

- Every complete round agrees with an independent scalar permutation for zero
  and deterministic randomized states.
- SHAKE128 and SHAKE256 match known answers, including empty input, and a
  separate host implementation for nontrivial messages.
- For each rate `R`, inputs around `0, 1, 7, 8, R-1, R, R+1, 2R-1, 2R, 2R+1` and
  output lengths around rate boundaries exercise empty, partial, exact-block,
  and multiple-block absorb/squeeze behavior.
- Distinct states in multiple warps agree independently; single-state and
  eight-warp launches preserve the full-lane collective contract.

Use the isolated `build32_sg25` RV32 build. Re-run `configure` after adding the test
directory and before execution, and match app and driver configuration at
`8w x 32t` with a 32-lane ALU. Run generated build-tree Makefiles and scripts.
Record commands, correctness results, and disassembly findings before hardware
work starts; do not turn instruction-count estimates into measured results.

From `build32_sg25`, after configuring with the installed RV32 toolchain:

```bash
CONFIGS="-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32" make software
make -C tests/pqc/keccak_sg25 clean
CONFIGS="-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32" \
  make -C tests/pqc/keccak_sg25
CONFIGS="-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32" \
  ./ci/blackbox.sh --driver=simx --app=pqc/keccak_sg25 --warps=8 --threads=32
CONFIGS="-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32" \
  ./ci/blackbox.sh --driver=simx --app=pqc/keccak_sg25 --warps=8 --threads=32 --args="-b 1"
```

This first test is for correctness, not cycle or throughput measurement.

### Validation record (2026-09-09)

Both runs used RV32, one core, eight warp slots, 32 threads, and L2/L3 disabled.
`-b` controls states per launch; the final batch may contain fewer states.

| Batch warps | Trace states | Round-word comparisons | SHAKE cases | Incorrect bytes |
|---|---:|---:|---:|---:|
| 1 | 17 | 10,200 | 204 | 0 |
| 8 | 17 | 10,200 | 204 | 0 |

The trace oracle agrees with mlkem-native on all 17 full permutations. Four
empty/`abc` SHAKE known answers validate the host sponge and are also exercised
on the device. The other 200 cases cross the ten input/output lengths above
for each rate, with unaligned byte addresses and output guards. The eight-warp
case is registered in `ci/testcases/pqc.yaml` as a SimX `full` test.

The initial unrolled trace exceeded the compiler's divergence basic-block
guard and failed correctness. Keeping the round loops rolled restores mask
lowering; the passing binary contains 27 split sites and 27 join sites. See
[kernel divergence lowering](../debugging.md#kernel-divergence-lowering).

The initial binary kept the SHAKE state in `a0/a1` but saved/restored three
registers on every round call. The timed implementation below removes those
per-round calls. The earlier split/join counts describe the initial binary,
not a permanent code-generation requirement.

### Historical THETA-only implementation and timing vehicle (2026-09-09)

This record describes the earlier THETA-only implementation snapshot, whose
binary hashes are in its CSV. The current three-stage results are recorded
separately below. `THETA=0/1` selects software or THETA instructions when building the same test.
Both measured arms enable the same hardware extension and use the same core
configuration. `keccak_round` is inlined into a rolled, separately callable
`permute`: there are no calls or stack accesses inside its 24-round loop.
Each permutation still saves/restores six GPRs for software or one for THETA,
and reads rho/round constants. State remains in `a0/a1`; this does not imply
zero LSU traffic.

`-p N` runs only a timed chain of N permutations per warp; `-b` selects one to
eight warps. Each warp records start/end cycles after input loads and before
output stores, and the host checks every final word against mlkem-native.
Batch span is `max(end)-min(start)`. It includes warp scheduling skew and can
overlap other warps' setup/teardown; each warp excludes only its own input/output
accesses from its interval. Dividing by completed permutations gives reciprocal
batch throughput, not state latency. Use `(span(N=8)-span(N=4))/(4*batch)` to
estimate marginal cycles per completed permutation. `PERF` counters cover the
whole launch and are reported separately.

From a configured RV32 `build32_ksg25` directory:

```bash
export CONFIGS="-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32 -DVX_CFG_EXT_KSG25_ENABLE"
export RHOPI=0 CHII=0
make -C sw/kernel
for theta in 0 1; do
  THETA=$theta make -C tests/pqc/keccak_sg25
  for batch in 1 8; do
    for perms in 4 8; do
      THETA=$theta ./ci/blackbox.sh --driver=rtlsim --app=pqc/keccak_sg25 \
        --args="-b $batch -p $perms" > "theta${theta}_b${batch}_p${perms}.log" 2>&1
    done
  done
done
```

The recorded [RTL results](../../pqc/results/keccak_sg25_theta.csv) give:

| Batch warps | Software cycles/permutation | THETA cycles/permutation | Improvement |
|---|---:|---:|---:|
| 1 (single-state marginal latency) | 19,306.75 | 12,076.00 | 1.599x |
| 8 (reciprocal batch throughput) | 6,548.97 | 3,135.25 | 2.089x |

These are simulation cycles for the permutation only, with RHOPI/CHII still in
software. They do not establish end-to-end SHAKE/ML-KEM speedup, achieved clock,
or area efficiency. The software arm also benefits from the inlining change;
the comparison does not use the older per-round-call baseline.

Default `THETA=1` tests passed on SimX and XRT: 53 input patterns times seven
register-alias/zero-register variants (11,872 word checks), 10,200 round-word
checks, and 204 SHAKE cases. `-s` runs the THETA checks alone. Padding inputs
are deliberately nonzero and all 32 output lanes are checked. A separate RTL
unit test (`make -C hw/unittest/ksg25 run`) passed 256 transactions with random
source/sink backpressure, metadata preservation, two-cycle PE latency, and
one-cycle initiation interval. The existing PE result buffer adds one cycle
in the measured single-issue, single-ALU-block configuration.

SimX and rtlsim retire identical instructions at all eight recorded points
(two arms, two batch sizes, two chain lengths). The maximum whole-launch cycle
difference is 1.68%, below the unchanged 5% parity tolerance. The THETA cases
at `-p 4 -b 1` and `-p 4 -b 8` are registered as `model_parity` checks.
Both standard CI checks passed at 0.51% and 0.68%, respectively.

Closing this gate required three timing-model corrections supported by RTL:

- Issue arbitration now matches the scoreboard's sticky round-robin (up to
  eight warps per issue slice) or sticky matrix policy. Selection no longer
  changes arbitration history until the operand stage accepts the instruction.
- Memory responses follow each platform bank's request FIFO, including writes,
  as in rtlsim and default XRT. DRAM callbacks mark completion rather than
  bypassing the FIFO; the existing three-cycle response boundary is unchanged.
  Callback records remain owned safely across reset.
- In the enabled full-width ALU model, dispatch credit returns when a PE
  actually accepts the instruction. A bound channel carries that event within
  the core's clock domain. Returning credit on entry to the extra FU input
  queue had allowed excess overlap: correcting this event alone changed the
  eight-warp/eight-permutation THETA result from 249,586 to 272,886 cycles,
  against RTL's 273,149. This path requires one packet per issue; disabled,
  potentially packetized ALU configurations retain their existing credit path.

The default `4w x 4t` build with KSG25 disabled also passed the existing vecadd,
SGEMM, and softmax model-parity tests, with exact instruction counts and cycle
differences of 0.51%, 0.73%, and 3.41%. Independent checks exercised arbitration
under backpressure and ALU result/credit backpressure across repeated resets.
No latency constant, parity tolerance, or golden baseline was changed.

This establishes agreement on the tested workloads and configurations, not a
complete proof of the simulator. Separate investigation found remaining request
crossbar arbitration, commit-buffer topology, and multiply-latency differences;
those diagnostic changes are not part of this implementation.

### Full three-stage implementation (2026-09-09)

Build-time `THETA`, `RHOPI`, and `CHII` values independently select each software
or instruction stage. All default to zero. The eight arms use the same hardware
with all six encodings enabled; this is an instruction-use ablation, not an
area ablation. Benchmark arm strings list stages in `T/R/C` order (`---`, `T--`,
`-R-`, `TR-`, `--C`, `T-C`, `-RC`, `TRC`).

The all-instruction `permute` disassembly has exactly six collectives, a round
increment, and a loop branch per round. It is a leaf function with no load,
store, stack frame, or call. State and stage temporaries use `a0/a1/a4`; `a2/a3`
hold the round and loop bound. These five GPRs describe the permutation only:
the enclosing benchmark and SHAKE functions still have setup, absorb/squeeze,
and other stack traffic.

The current benchmark launches one CTA with `32 * batch` threads, one state
per warp. CTA barriers after input loads and after end timestamps prevent
other warps' initial state loads and final stores from overlapping the timed
batch. Each warp records `vx_rdcycle_sync()` before and after its chain;
`max(end)-min(start)` includes scheduling skew, loop overhead, and software
stages' constant/stack accesses. The second barrier is outside every recorded
interval. Whole-launch `PERF` counters include setup, barriers, and teardown.

This change matters for short, fast chains: without the barriers, all-instruction
SimX and RTL launched the eight states at different offsets, and whole-launch
cycle differences reached 14–20% despite identical instruction counts. With
synchronized batches, the eight-warp/eight-permutation point measured 114,211
versus 114,348 whole-launch cycles (0.12% difference) and 30,837 versus 30,900
interval cycles (0.20%). No hardware/model latency or parity tolerance was
changed to obtain this result. Use the synchronized three-stage measurements
for comparisons; do not mix them with the historical THETA-only measurements.

Direct tests cover 11,872 THETA output words and 11,872 RHOPI output words,
including each state's bit 31/63 routing, all-ones/random/zero input, nonzero
padding inputs, destination aliases, equal sources, and zero registers. CHII
checks 9,216 output words: eight state patterns across all 24 rounds, with all
seven alias/zero-register variants at rounds 0 and 23. Equal CHII sources use
a uniform round value; zero `rs2` selects round zero. Every enabled arm also
runs 10,200 round-word comparisons and 204 SHAKE cases.

The final all-instruction XRT run passed all direct stage checks, 10,200
round-word comparisons, and all 204 SHAKE cases (35,372 output/guard bytes),
with 254,941 retired instructions and 12,769,164 whole-suite cycles. Its
synchronized eight-warp/eight-permutation benchmark also passed: 13,600
instructions, 114,892 whole-launch cycles, and a 30,900-cycle measured span.
The full suite is correctness coverage with many short launches, not an
end-to-end SHAKE throughput benchmark.

The mixed RTL unit test now covers 288 transactions across all six instructions,
with forward-coordinate RHOPI reference and LFSR-generated round constants.
It checks result/metadata stability under backpressure, latency and initiation
interval, plus rejection of a partial mask, out-of-range round, and nonuniform
round (including a disagreement in padding lane 31).

The full XRT SHAKE sweep also exposed an insufficient simulation watchdog
budget with KSG25 enabled and pointer PQC disabled. A continuous launch sequence
reached a 100,000-cycle wait at an ordinary descriptor load (`PC=0x80000124`).
The trace showed other warps continuing to issue and complete memory operations;
the isolated batch passed on all three drivers. A diagnostic run with a larger
budget completed all 204 SHAKE cases, including the previously interrupted
batch, with zero mismatches. This was a finite arbitration/queued-memory wait,
not a Keccak result mismatch or a stopped core.

`VX_DBG_STALL_TIMEOUT` now applies the existing PQC lane-count budget when either
PQC or KSG25 is enabled: 3,200,000 cycles for the 32-lane configuration without
L2/L3, and still 100,000 with both extensions disabled. Enabling both extensions
does not multiply the budget twice. The per-warp counter and greedy scheduler
are unchanged; unrelated progress cannot continually reset a stuck warp's
counter. The budget affects simulation diagnostics only, not RTL execution
latency, cycle measurements, or the 5% model-parity tolerance. Both the existing
PQC watchdog regression and a KSG25-only configuration check a completing long
queue, a stopped FU, unrelated-FU progress, and an unresolved dependency despite
same-FU activity. Restoring 100,000 as a negative control must reject the long
queue. This budget does not establish bounded fairness for arbitrary workloads.

The synchronized [64-row measurement set](../../pqc/results/keccak_sg25_stages.csv)
contains eight arms, two batch sizes, two chain lengths, and both SimX/rtlsim.
Retired instruction counts agree at all 32 paired points; the maximum
whole-launch cycle difference is 3.24%, below the unchanged 5% tolerance.
Every arm passed the complete round/SHAKE suite on SimX. The six canonical
THETA, RHOPI, CHII, and combined-stage model-parity cases also passed (maximum
1.68% difference). RTL marginal results use `(span_p8-span_p4)/(4*batch)`:

| Arm | Single-state cycles/perm. | Speedup | 8-warp cycles/completed perm. | Throughput gain |
|---|---:|---:|---:|---:|
| `---` | 19,285.75 | 1.000x | 6,215.59 | 1.000x |
| `T--` | 12,074.00 | 1.597x | 3,161.56 | 1.966x |
| `-R-` | 11,173.25 | 1.726x | 3,626.09 | 1.714x |
| `--C` | 14,760.50 | 1.307x | 4,650.69 | 1.336x |
| `TR-` | 5,275.00 | 3.656x | 1,182.75 | 5.255x |
| `T-C` | 8,818.00 | 2.187x | 2,117.38 | 2.936x |
| `-RC` | 7,708.00 | 2.502x | 1,953.38 | 3.182x |
| `TRC` | 2,050.00 | 9.408x | 481.50 | 12.909x |

The all-instruction loop reduces RTL single-state marginal latency by 9.41x
and raises eight-warp cycle-normalized throughput by 12.91x relative to SG25
shuffle software on this core. These are permutation-chain measurements;
they do not measure SHAKE/ML-KEM speedup, achieved clock, or area efficiency.

Reproduce the eight instruction-use arms from a freshly configured RV32 build
with matching runtime/kernel libraries:

```bash
export CONFIGS="-DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32"
for THETA in 0 1; do
  for RHOPI in 0 1; do
    for CHII in 0 1; do
      export THETA RHOPI CHII
      make -C tests/pqc/keccak_sg25
      for driver in simx rtlsim; do
        for batch in 1 8; do
          for perms in 4 8; do
            ./ci/blackbox.sh --driver="$driver" --app=pqc/keccak_sg25 \
              --args="-b $batch -p $perms" \
              > "sg25_${THETA}${RHOPI}${CHII}_${driver}_b${batch}_p${perms}.log" 2>&1
          done
        done
      done
    done
  done
done
```

## 4. Hardware gate and integration

All three stages now have allocated encodings, alias/zero-register tests, a
shared two-stage `VX_alu_ksg25.sv` pipeline under `VX_alu_unit.sv`, and matching
SimX behavior. THETA reduces columns in stage one and applies the correction
in stage two. RHOPI gathers and rotates using constant wiring in stage one;
stage two forwards the result. CHII gathers and computes the Boolean expression
in stage one alongside the round-constant lookup; stage two adds the constant
in lane zero. Each form keeps the two-cycle PE latency and one-cycle initiation
interval, plus the existing one-cycle result buffer on the measured core.

The design consumes full GPR vectors and uses fixed cross-lane routing where
useful. Existing shuffle connectivity is a starting point, not a free five-way
reduction or three-source gather. Evaluate routing, fanout, and register costs.
Do not put a large combinational chain in the ordinary integer ALU path.

The collective instructions have no LSU, pointer AGU, or private architectural state. Pipeline
buffers are still required as dictated by timing. Normal RAW dependencies and
unit backpressure can stall issue; ready warps should overlap execution subject
to measured arbitration and writeback limits. Report latency and initiation
interval separately. Communicate between SimX modules only through channels.

The six registered `model_parity` cases use matching applications and
configurations with the unchanged 5% tolerance. Stage and SHAKE cases cover
both SimX and the canonical XRT integration path; rtlsim supplies processor
cycle comparisons. Full-core routed timing is reported below because
isolated-unit simulation latency does not establish closure for the wide V80
core.

The isolated implementation results are recorded in
[`keccak_sg25_ppa.csv`](../../pqc/results/keccak_sg25_ppa.csv):

| Flow | Unit | Logic | State/registers | Fmax |
|---|---|---:|---:|---:|
| V80 post-route | pointer `KECCAKF` | 3,223 LUT | 1,607 FF | 368.4 MHz |
| V80 post-route | SG25 stages | 2,821 LUT | 2,101 FF | 323.7 MHz |
| V80 post-route | SG25 whole round | 2,610 LUT | 6,282 FF | 359.6 MHz |
| ASAP7 RVT TT mapped STA | pointer `KECCAKF` | 1,723.254 um2 total | 470.642 um2 sequential | 2,187.227 MHz |
| ASAP7 RVT TT mapped STA | SG25 stages | 1,832.123 um2 total | 612.943 um2 sequential | 1,882.885 MHz |
| ASAP7 RVT TT mapped STA | SG25 whole round | 3,889.186 um2 total | 1,831.831 um2 sequential | 2,041.233 MHz |

Against the pointer unit, SG25 uses 12.47% fewer V80 LUTs and 30.74% more FFs;
in the matched 500 MHz ASAP7 mapping it uses 6.32% more total cell area. Its V80
critical path contains two LUT levels and is 92.34% routing delay, from the
parity buffer to the result buffer. It closes 300 MHz in isolation with 0.244 ns
slack. These figures establish that the stage unit is viable by itself; they do
not establish full-core timing or compare operation latency, since the pointer
unit completes a permutation while SG25 executes one RV32 stage instruction.

Matched full-core post-route results use RV32, one core, eight warps, 32 threads,
and a 32-lane ALU. Constraint-dependent placement means each extension must be
compared with the base row at the same target:

| Target | Core | LUT | FF | WNS | Achieved Fmax |
|---|---|---:|---:|---:|---:|
| 300 MHz | base | 325,407 | 269,344 | -0.066 ns | 294.2 MHz |
| 300 MHz | SG25 | 328,403 | 272,309 | -0.096 ns | 291.6 MHz |
| 300 MHz | KROUND25 | 327,816 | 276,308 | -0.058 ns | 294.9 MHz |
| 250 MHz | base | 323,072 | 263,116 | +0.015 ns | 250.9 MHz |
| 250 MHz | SG25 | 325,288 | 265,015 | +0.033 ns | 252.1 MHz |
| 250 MHz | KROUND25 | 325,832 | 269,371 | 0.000 ns | 250.0 MHz |
| 250 MHz | pointer PE | 334,917 | not recorded | 0.000 ns | 250.0 MHz |

At 300 MHz SG25 adds 0.921% LUT and 1.101% FF and reduces achieved Fmax by
0.884%. At the shared 250 MHz operating point it adds 0.686% LUT and 0.722% FF
and meets timing. The pointer PE adds 3.7% LUT at that operating point, 5.35
times SG25's integration overhead. KSG25 itself accounts for 869 LUT and 2,025
FF in the 250 MHz hierarchy. The worst path is an eight-level local-memory
crossbar path, and none of the ten worst paths enters KSG25. The 300 MHz
permutation-chain result therefore corresponds to a derived 12.68x gain in
achieved throughput per full-core LUT over software:

```text
12.909 * (291.6 / 294.2) / (328403 / 325407) = 12.68
```

### Whole-round GPR comparator

`VX_CFG_EXT_KROUND25_ENABLE` enables an independent comparator extension. It
uses CUSTOM2 (`0x5b`) with `funct3=0/1` for `KROUND.L/H.SG25`; `funct7` is the
literal round number `0..23`, `rs1/rs2` contain the low/high state halves, and
one instruction writes one 32-bit half. Both halves read the same old state, so
a complete round takes two issues and distinct output registers. Extended MISA
bit 13 advertises the extension. The RV32, 32-thread, full-mask contract is the
same as the stage design.

The ALU-side unit has four registered stages: column parity, THETA, RHOPI, and
CHII/IOTA plus result buffering. It has a four-cycle direct latency and a
one-cycle initiation interval. It keeps no hidden architectural state and does
not use the LSU. The kernel expands the 24 literal rounds into 48 instructions;
the state remains in two lane GPRs across the permutation and sponge.

The RTL unit test passes 192 randomized transactions with metadata, padding
lanes, input/output stalls, and an initiation-interval check. Separate negative
tests diagnose a partial mask and invalid round. The complete SimX and XRT
SHAKE tests pass 17 states checked after every round and 204 SHAKE cases.
Full-core rtlsim passes both one-warp and eight-warp timing cases. The full
measurements are in
[`keccak_kround25.csv`](../../pqc/results/keccak_kround25.csv):

| Model | Design | Dependent cycles/permutation | 8-warp cycles/completed permutation |
|---|---|---:|---:|
| SimX | six stages | 2,079.312 | 479.463 |
| SimX | whole round | 502.406 | 121.727 |
| RTL | six stages | 2,052.219 | 481.664 |
| RTL | whole round | 499.266 | 117.693 |

KROUND25 improves the measured SimX chain by 4.139x and eight-warp throughput
by 3.939x; RTL gives 4.110x and 4.093x. This is the implemented instruction
sequence comparison, including the literal-round expansion for KROUND25 and
the loop control in the stage kernel.

The isolated V80 implementation uses 7.48% fewer LUTs and 2.990x the FFs of
the stage unit; it closes the 300 MHz target at 359.6 MHz. In the matched
500 MHz ASAP7 mapping it uses 2.123x the total cell area and 2.989x the
sequential area. The 250 MHz full-core route uses 325,832 LUTs and 269,371 FFs,
adding 2,760 LUTs (0.854%) and 6,255 FFs (2.377%) over the base core. Its
hierarchy accounts for 2,483 LUTs and 6,130 FFs. The worst path runs from issue
to the ordinary integer ALU, and none of the reported worst paths enters
KROUND25.

At the 300 MHz target, KROUND25 uses 327,816 LUTs and 276,308 FFs, adding
2,409 LUTs (0.740%) and 6,964 FFs (2.586%) over the matched base. Its hierarchy
accounts for 2,428 LUTs and 6,131 FFs. The achieved 294.9 MHz is 1.13% above
the stage core's 291.6 MHz; KROUND25 uses 587 fewer LUTs and 3,999 more FFs.
The worst path is in the local-memory response crossbar, and none of the 100
reported worst paths enters KROUND25.

Against the stage core, KROUND25 adds 544 LUTs and 4,356 FFs. At the common
250 MHz operating point, its measured eight-warp permutation throughput per
full-core LUT is 4.086x the stage design:

```text
(481.664 / 117.693) * (325288 / 325832) = 4.086
```

Using each 300 MHz-target build's achieved Fmax gives 4.146x, consistent with
the common-frequency result:

```text
(481.664 / 117.693) * (294.9 / 291.6) * (328403 / 327816) = 4.146
```

Using the matched software core and its 6,215.594 RTL cycles per completed
permutation gives a 52.36x throughput/full-core-LUT result over software. The
large FF and ASIC-area costs still make KROUND25 a performance-bound comparator
rather than the main stage-factored contribution.

### ML-KEM integration

[`sg25_fips202.h`](../../tests/pqc/mlkem_width/sg25_fips202.h) implements the
serial mlkem-native FIPS-202 API with one logical request per 32-lane warp. The
one-shot sponge keeps one 64-bit state word per lane in GPRs throughout absorb,
permute, and squeeze. Incremental SHAKE128 stores one word per lane in its
context between the absorb and squeeze API calls, then reloads it. The compiled
permutation loop contains six custom instructions, an increment, and a branch,
with no load, store, call, or stack frame. A warp-shared allocation arena avoids
32 copies of mlkem-native's 19,232-byte allocation peak. The surrounding ML-KEM
runs on lane 0. Each FIPS-202 wrapper uses `TMC` followed by a noinline worker;
the worker broadcasts lane 0's arguments and executes with all 32 lanes, then
the wrapper restores the lane-0 mask.

All one-request and eight-request runs complete 143 permutations per request
and pass ML-KEM return-code, shared-secret, and pk/sk/ct/ss checksum checks. The
full data and binary hashes are in
[`keccak_sg25_mlkem.csv`](../../pqc/results/keccak_sg25_mlkem.csv):

| Backend | Lanes/request | 1-request device cycles | Speedup vs SG1 | 8-request device cycles | Throughput vs SG1 |
|---|---:|---:|---:|---:|---:|
| SG1 serial software | 1 | 34,688,954 | 1.000x | 53,176,031 | 1.000x |
| SG25 shuffle software | 32 | 14,320,553 | 2.422x | 26,588,208 | 2.000x |
| SG25 redundant outer KEM | 32 | 28,684,047 | 1.209x | 160,039,011 | 0.332x |
| SG25 lane-0 outer KEM | 32 | 11,749,447 | 2.952x | 19,228,330 | 2.766x |
| SG25 whole-round comparator | 32 | 11,532,487 | 3.008x | 18,989,395 | 2.800x |
| pointer `KECCAKF` | 1 | 12,430,412 | 2.791x | 17,013,577 | 3.125x |

The lane-0 mapping removes the redundant outer computation. MPM class 1 counts
164,422 loads and 162,082 stores at one request, reductions of 24.3x and 24.2x
from the redundant mapping. These counts are also below SG1's 628,039/512,165
and close to the pointer PE's 152,797/136,038. Batch scaling rises from 1.434x
to 4.888x. SG25 is 1.058x faster than the pointer PE for the one-request launch;
the pointer PE is 1.130x faster for eight requests. This gives separate latency
and saturated-throughput conclusions without requiring a parallel rewrite of
the rest of ML-KEM.

The shuffle-only SG25 arm uses the same lane-0 outer KEM, FIPS wrapper, layout,
core configuration, and absorb/squeeze code as the instruction arm. Replacing
its shuffle rounds with the six stage instructions improves whole-launch cycles
by 1.219x at one request and 1.383x at eight requests. This comparison isolates
the instruction contribution from the benefit of assigning one state to 25
lanes; SG25 software alone is already 2.422x/2.000x faster than SG1 at the two
load points.

KROUND25 completes the same 143 permutations and passes all ML-KEM checks. It
improves the six-stage whole launch by only 1.019x at one request and 1.013x at
eight requests, despite the roughly 4x isolated permutation result. It retires
1.91% fewer instructions at one request. The gap shows that this ML-KEM mapping
is dominated by absorb/squeeze, polynomial work, and wrapper overhead once the
six stage instructions are present.

The one-request KROUND25 XRT run also passes and retires the same 1,073,037
instructions as SimX. It reports 12,691,670 whole-launch RTL cycles and uses
the same `c98c18e9...4bb3d2` kernel binary. As with the stage design, the SimX
rows remain the matched backend comparison.

The one-request XRT integration run also passes all ML-KEM checks and retires
the same 1,093,915 instructions as SimX. It reports 12,875,947 whole-launch RTL
cycles and uses the same `b997d0f4...55e2a8b` kernel binary. This is a
correctness and integration result; the SimX rows above remain the matched
backend comparison. The full ML-KEM case runs on both SimX and XRT in CI.

The boundary exposed a SimX `TMC` bug: `1 << 31` used a signed literal, so
reactivating a 32-lane warp dropped lane 31. Using an XLEN-width `Word(1)` makes
SimX match RTL. The FIPS wrappers also require a noinline worker after `TMC` so
newly active lanes begin at a clean function boundary before parameter shuffles.

## 5. Experiments and paper claim

### Matched W32 software and unrolling controls

The remaining controls use RV32IM, F/D disabled, one core, W8T32, the final
half-bank NTT, and the same Stage/KROUND-enabled core. The permutation bench
compares reference C, PQRV RV32IM assembly, the existing five-lane column
mapping with two transpose fences per round, and SG25 shuffles. One state per
warp establishes latency; packed states (32 for SG1, six for SG5, one for
SG25) establish completed-permutation throughput. All state initialization
and output traffic are outside a CTA-synchronized timed interval, and the host
checks every output word against the same reference and input sequence.

A fully unrolled Stage arm controls for KROUND's required literal-round
expansion. Both use 24 rounds and the same dependent chain length; the existing
looped Stage arm remains a separate measurement. Disassembly must confirm the
round-loop removal and preserve divergence lowering in guarded callers.

The final ML-KEM profile adds the PQRV permutation hook and an unrolled Stage
arm while retaining the same serial FIPS-202 call graph, NTT, cooperative
arithmetic, and KAT input. SimX M1/M8 establishes application behavior; XRT
checks the new paths and retired-instruction agreement. These software
controls do not change RTL or require new synthesis.

#### Results and reproduction

The implementation snapshot is `38c56304f`. The archived matrices are
[`keccak_w32_controls.csv`](../../pqc/results/keccak_w32_controls.csv) and
[`mlkem_w32_controls.csv`](../../pqc/results/mlkem_w32_controls.csv). Each row
preserves its ELF and raw-log hashes. Binaries, disassemblies, build arguments,
and logs remain in `build32_im/tests/pqc/{keccak_sg25,mlkem_profile}/matched_controls/`.

For a 64-permutation chain, the SimX cycles per completed permutation are:

| Backend | One warp | Eight warps |
| --- | ---: | ---: |
| Looped Stage | 2,079.312 | 479.463 |
| Fully unrolled Stage | 1,584.906 | 401.688 |
| Whole round | 502.406 | 121.727 |

Equal unrolling leaves a 3.155x/3.300x whole-round advantage. The former
roughly 4x comparison includes loop overhead. Both application disassemblies
contain the same 44-byte looped Stage, 676-byte expanded Stage, and 196-byte
KROUND functions. The expanded functions have no round-loop branch and issue
144/48 custom instructions per permutation. Divergent byte-load and output
guards retain split/join lowering; the expanded SHAKE trace and boundary tests
also pass on SimX and XRT.

With final NTT/arithmetic fixed, PQRV improves complete ML-KEM over C by
1.122x at M1 and 1.170x at M8. Expanded Stage improves PQRV by 4.626x/3.063x.
KROUND improves expanded Stage by only 1.028x/1.033x. Stage unrolling removes
3,640 retired instructions per request, but reduces M1 cycles by 1.252% and
increases M8 cycles by 1.364%. The data establish this behavior without
attributing the M8 difference to an unmeasured stall category.

SG5 passes every output comparison, but its two XRT checks fail the unchanged
5% timing-model criterion: 6.932% for one state/warp and 12.223% for six states
per warp at two warps. Instruction counts and ELF hashes agree. SG5 retains
two cache-flushing fences per round. As documented in the earlier
[NTT synchronization investigation](ntt_acceleration_proposal.md#layer-synchronization-correction),
SimX waits for pending LSU requests without modeling RTL's complete cache flush.
This limitation must accompany the SG5 SimX predictions; a numerical PASS is
not a timing-model PASS.

The existing `lsu_model_probe` was also rerun on this exact core at M1/L32,
384 iterations, for empty, clean fence, dirty fence, barrier, and dirty barrier.
All ten SimX/XRT checks pass with exact instruction agreement. The measured
intervals and log directory are preserved in the microbenchmark CSV comments.
These controls reproduce the fence discrepancy; they do not claim to calibrate
every SG5 memory interaction. The original experiment keeps the existing SG5
algorithm fixed. The following synchronization investigation tests alternatives;
the general cache-flush model remains incomplete.

#### SG5 transpose synchronization correction

The transpose buffer is private to each five-lane group in one physical warp.
It needs stores to finish before the transposed loads, and loads to finish
before the next round overwrites the buffer. It does not need dirty lines
written back beyond the core. `SG5_SYNC=barrier` replaces both transpose fences
with synchronous one-warp barriers; `SG5_SYNC=fence` remains the default.
Barrier slot 1, indexed by physical warp ID, separates these operations from
the timing interval's CTA barrier in slot 0. All active SG5 lanes execute both
barriers after reconverging from iota; inactive lanes never own transpose data.
The existing BAR instruction drains the LSU in both RTL and SimX and counts
warps, so the five- through thirty-lane masks require one arrival per warp.

The retained implementation is `1bd44cf34`. Both rebuilt fence/barrier ELFs
are byte-identical to the binaries used in the measurements. Every packed
group count (5, 10, 15, 20, 25, or 30 active lanes) passes SimX and XRT at
eight warps. Longer chains and one/two-warp cases also pass numerical and
output-guard checks. Disassembly retains the active-lane and iota split/join
pairs, joins iota before the transpose, and has two static transpose BARs with
no fence in the barrier benchmark body. All five diagnostic kernels use soft
ABI flags 0 and contain no FP/AMO opcodes in executable sections.

The original two failures become 1.387% and 1.193% with the warp barriers.
The expanded matrix nevertheless exposes two remaining failures: W8/S6/P2
at 5.442%, and W8/S1/P8 at 9.627%. The barrier version passes nine of its
eleven timing pairs; it is **not** an SG5-wide timing-model closure.

Three additional controls separate barrier scope, storage, and the number
of synchronizations. CTA barriers synchronize all participating warps; LMEM
reserves 1200 bytes per warp for the same transpose; a single-barrier variant
relies on the next theta consuming all five loads before the next overwrite.
All run the same five common workloads. Absolute whole-launch SimX/XRT gaps
are below; values above 5% remain failures:

| Synchronization / buffer | W1/S1/P8 | W2/S6/P2 | W8/S1/P8 | W8/S6/P2 | W8/S6/P8 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Two fences / global | 6.932% | 12.223% | 26.588% | 20.439% | 21.849% |
| Two warp barriers / global | 1.387% | 1.193% | 9.627% | 5.442% | 3.566% |
| Two CTA barriers / global | 0.515% | 1.692% | 3.180% | 6.301% | 7.486% |
| Two warp barriers / LMEM | 1.348% | 1.445% | 22.115% | 1.487% | 1.861% |
| One warp barrier / global | 1.247% | 1.273% | 6.556% | 0.596% | 0.950% |

Across these 31 pairs, all 62 runs pass numerical checks and every pair has
identical retired instructions and binary hashes. Twenty pairs pass timing;
eleven fail. The CSV records both whole-launch and isolated-span gaps, so
fixed launch overhead cannot hide a larger permutation-interval discrepancy.
The two original fence results reproduce exactly. At W8/S1/P8, two warp
barriers reduce measured XRT launch cycles from 1,372,343 to 1,113,440; the
remaining model error is therefore distinct from the RTL performance gain.

Data: [`keccak_sg5_sync.csv`](../../pqc/results/keccak_sg5_sync.csv).
The three diagnostic source patches are in
[`keccak_sg5_sync_sources.zip`](../../pqc/results/keccak_sg5_sync_sources.zip);
each applies independently to `68845dd72`, and the CSV records its hash.
Build commands, raw logs, ELF/host binaries, and disassembly audits are under
`build32_im/sg5_sync/`. The unreserved-LMEM preliminary run is excluded; the
archived LMEM control includes the host reservation. The existing legacy
`keccak_sg5 -a sg5 -t 32 -p 2 -f 2` also passes SimX after the shared-header change.
CI catalog lint and collection pass; the new eight-warp barrier case is a
**functional** SimX/XRT case, not a timing waiver or a claimed full CI run.
The measurements here keep F/D disabled and use the local soft-ABI libraries.

In that experiment, no synchronization variant passed every common workload, so the default
and historical paper denominator remain unchanged. BAR waits on the core's
shared LSU drain in both models; assigning a warp-private barrier slot does
not turn that drain into a warp-private resource. The fence-free failures show
that cache-flush omission alone cannot explain all SG5 timing differences.
This motivated the BAR/LSU/cache trace comparison below. No RTL, model
constants, tolerance, or synthesis results changed in the synchronization experiment.

#### Cache fill-forward investigation

The subsequent W8/S1/P8 trace comparison at `2c3a5e533` identifies an extra
SimX cycle between D-cache fill acceptance and the first forwarded read.
RTL captures `fbuf_data_r` and arms the MSHR dequeue on `mem_rsp_fire`, so
forwarding starts the following cycle. SimX instead arms forwarding in
`CacheBank::processFill`, after the array pipeline, and reads the installed
cache sector. The correction stages the fill payload at admission and
forwards from that buffer independently of array installation. A write-through
store arriving before the staged fill installs must still merge on replay,
even if the older forwarded reads already released their MSHR entries.

The cache regression fails before the change (external response latency
5/7 cycles for array depths 2/4) and passes afterward (4/4). It also checks
read/store/read chains, response backpressure, and write-through stores at
eight offsets around fill arrival. RV32 and RV64 host unit cases pass.
The retained SG5 barrier matrix passes the unchanged whole-launch 5% gate
in all eleven pairs; its maximum isolated-span gap is still 5.633%.
Historical synchronization results stay intact and the fence/cache-flush
limitation is not declared fixed by this change.

The correction is committed as `6455855c2`. All eleven retained warp-barrier
workloads were rerun on release SimX and XRT, using `PERF=1` and
`VORTEX_PROFILING=4`. The same ELF and host hashes match the earlier experiment;
all 22 runs pass output/guard checks and all eleven pairs retire identical
instructions. Every XRT launch and isolated span reproduces its previous
value exactly. Selected whole-launch results are:

| Workload | Previous SimX | Corrected SimX | XRT | Previous gap | Corrected gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| W8/S1/P8 | 1,220,634 | 1,076,767 | 1,113,440 | 9.627% | 3.294% |
| W8/S6/P2 | 1,574,342 | 1,553,825 | 1,493,084 | 5.442% | 4.068% |
| W8/S2/P2 | 584,719 | 585,967 | 562,211 | 4.003% | 4.225% |

The last row is the largest remaining launch gap. Its isolated permutation
span is 490,962 versus 464,783 cycles (5.633%); it must not be described as
agreement within 5% for every measured interval. The fence, CTA, LMEM, and
single-barrier variants were not rerun with the corrected model. Historical
paper throughput tables retain their original model snapshot.

Data: [`keccak_sg5_cache_forward.csv`](../../pqc/results/keccak_sg5_cache_forward.csv).
Source revisions, binary/runtime/log hashes, trace histograms, and regression
scope are in
[`keccak_sg5_cache_forward_trace.json`](../../pqc/results/keccak_sg5_cache_forward_trace.json).
Raw logs and traces are under `build32_im/sg5_model/`. The first read following
a fill took two bank cycles in all 92,535 old SimX samples; RTL forwards in
one cycle for 91,274 of 91,311 samples. Corrected SimX forwards in one cycle
for 94,474 of 94,554 samples, with response backpressure accounting for later
responses. The initial DEBUG traces used an unsupported profiling selector
and failed the final host perf dump; they are diagnostic evidence only.
DEBUG XRT also has a different aggregate span from release XRT, so only the
release runs enter the parity matrix.

The native `sw-cache-forward` CI case passes at both XLENs. Additional release
SimX/XRT runs pass numerical checks and exact instruction counts for
`pqc_alu_mix`, `vecadd -n16384`, and `io_addr`. The first two have launch gaps
2.562% and 0.616%. The tiny `io_addr` launch remains a functional-only pass:
its gap is 12.445%, versus 12.708% before the correction. CI catalog lint and
the software/simulator boundary check pass; this is not a full regression
suite or a new synthesis run.

#### Matched XRT software-mapping comparison

A subsequent XRT run holds the final RV32IM W8T32 core fixed with NTT,
pointer, Stage, and KROUND enabled and F/D disabled. Each row processes one
state per warp for eight consecutive permutations across eight warps; the
measured span excludes setup and output. All six output/guard checks pass,
and instruction counts and ELF hashes match the archived controls.

| Mapping | Span cycles | Cycles/completed permutation |
| --- | ---: | ---: |
| SG1 C | 2,377,862 | 37,154.094 |
| PQRV | 1,586,045 | 24,781.953 |
| SG5, two warp barriers | 1,018,270 | 15,910.469 |
| SG25 software shuffle | 432,215 | 6,753.359 |
| Stage, unrolled | 24,949 | 389.828 |
| KROUND | 7,675 | 119.922 |

Data and log hashes: [`keccak_w32_matched_xrt.csv`](../../pqc/results/keccak_w32_matched_xrt.csv).
The common input isolates permutation latency at one state per warp; it does
not measure each mapping at its maximum packing, nor complete ML-KEM requests.

With the build environment below, reproduce the retained control using
`make -s -C tests/pqc/keccak_sg25 MAPPING=sg5 SG5_SYNC=barrier`; run the same
ELF with `-b 1 -n 1 -p 8`, `-b 2 -n 6 -p 2`, and the eight-warp cases above
on both drivers. Select `SG5_SYNC=fence` to reproduce the original control.

Reproduction starts in the configured `build32_im` directory:

```sh
../configure --xlen=32 --tooldir="$(realpath ../../toolchains)"
export CONFIGS="-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE -DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_EXT_KROUND25_ENABLE -DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32"
export LIBC_PATH="$(realpath ../../toolchains-im/libc32)"
export LIBCRT_PATH="$(realpath ../../toolchains-im/libcrt32)"
make -s -C sw/runtime/simx DESTDIR="$PWD/sw/runtime"
make -s -C sw/runtime/xrt DESTDIR="$PWD/sw/runtime"

make -s -C tests/pqc/keccak_sg25 MAPPING=asm
cd tests/pqc/keccak_sg25
LD_LIBRARY_PATH=../../../sw/runtime VORTEX_DRIVER=simx ./keccak_sg25 -b 8 -n 32 -p 8
cd ../../..

make -s -C tests/pqc/mlkem_profile KECCAK=sg25 UNROLL=1 SERIAL=1 \
  NTT=reg32 NTTBF=ise NTTMUL=ise ARITH=all ARITH_MUL=ise
cd tests/pqc/mlkem_profile
LD_LIBRARY_PATH=../../../sw/runtime VORTEX_DRIVER=simx ./mlkem_profile -b 8 -t 32
```

The software permutation matrix uses `MAPPING=sg1|asm|sg5|sg25`, batch warps
1/8, chain lengths 4/8, and states/warp 1 or the mapping's maximum. Stage uses
`THETA=1 RHOPI=1 CHII=1`, the expanded arm additionally uses `UNROLL=1`, and
whole round uses `KROUND=1`. Those three arms also run 64-permutation chains on
both SimX and XRT at 1/8 warps. Each software mapping has an XRT check at
one warp/state with eight permutations and at two fully packed warps with two
permutations. The expanded Stage arm additionally runs the full trace/SHAKE
suite with `-b 2`, without `-p`, on both drivers.

The six complete-KEM arms are `KECCAK=sg1|asm|sg25_sw|sg25|kround`, with a
second `sg25 UNROLL=1` arm. All run M1/M8 on SimX; assembly, looped Stage,
expanded Stage, and KROUND additionally run M1 on XRT. Each passes the same
FIPS 203 KAT with counts 140/0/15/9 for scalar/x4 Keccak/NTT/INTT. The local
soft-ABI libraries remain subject to the existing prebuilt-toolchain packaging
requirement before shipping integer-only CI environments.

Evaluate the eight THETA/RHOPI/CHII enable combinations: software only, each
single stage, each pair, and all three. Keep the nonaccelerated stages in
software. For area comparisons, remove disabled hardware and rebuild; merely
avoiding its instructions does not measure area savings.

Compare SG1 software, SG5 software, SG25 software, the stage variants, and
pointer `KECCAKF` on the same workload, core configuration, toolchain, and
implementation flow. Include an optimized SG1 baseline and inspect spills.
SG1 and SG5 can process more states per warp than SG25: normalize by completed
states and separate single-state latency from saturated batch throughput.

Report kernel cycles and wall-time throughput using achieved clock, eight-warp
throughput, spill/LSU traffic, whole-core LUT/FF/BRAM/DSP, routed Fmax, and
throughput/LUT. Include SHAKE message/output-length sweeps and eventual ML-KEM
integration. Present both fixed-W32 mechanism comparisons and comparable-area
system comparisons. The implemented GPR whole-round comparator now isolates
stage granularity from the pointer engine. Finish its full-core PPA and XRT rows
before deciding which granularity is superior on throughput per LUT. RV64
remains a later word-width study.

[ML-Cube, CCS 2025, §5.1.1](https://zhengfangyu.github.io/assets/papers/CCS2025.pdf)
already uses 25 threads holding Keccak words and shuffle exchange, crediting
Lee et al.'s earlier approach. NVIDIA's public application
[US20260222190A1, Listing 3 and Fig. 7–8](https://patents.justia.com/patent/20260222190)
describes a 64-bit warp-collective Keccak round, multi-round forms, and internal
XOR/Boolean circuits with fixed rotation/permutation wiring. These disclosures
do not establish a shipping GPU's instruction latency or measured PPA.

Do not claim the first SG25 layout, register-based collective Keccak, or internal
Keccak circuit. The candidate contribution is an RV32 single-writeback subgroup
microarchitecture, evidence about stage granularity under real register and
timing constraints, and a same-platform comparison against pointer engines.
Stage splitting or a platform port alone does not establish that contribution;
the correctness, scheduling, PPA, and end-to-end evidence must support it.
