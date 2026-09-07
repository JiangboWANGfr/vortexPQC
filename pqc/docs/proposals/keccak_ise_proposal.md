# Keccak-f1600 ISA extension for Vortex — design proposal

Status: proposal. Nothing here is implemented; `VX_CFG_EXT_PQC_ENABLE` is still
`false` and `hw/rtl/pqc/VX_pqc_pkg.sv` is still a stub.

This proposes the instruction, not the microarchitecture's last detail. It fixes
the things that are expensive to change later — the encoding, the semantics, the
software contract, and the shape of the evaluation — and leaves the datapath
width and the engine count as parameters, because the measurements say those are
worth single-digit percent and should be settled by area rather than argued
about now.

---

## 1. The constraint that shapes everything: there is no performance decision left

`pqc/results/ablation_mlkem.csv` measures ML-KEM-768's non-Keccak residual at
**11,505,678** cycles of a **33,492,153**-cycle round trip over **144**
permutations, so the Amdahl bound for a free Keccak is **2.911×**. Sweeping the
end-to-end cost of one hardware permutation against that residual:

| cycles per permutation | end-to-end | fraction of the bound |
|---:|---:|---:|
| 26 | 3.096× | 99.9% |
| 226 | 3.088× | 99.7% |
| 1,000 | 3.056× | 98.6% |
| 2,500 | 2.996× | 96.7% |

**A hundredfold difference in permutation cost moves the end-to-end result by
3.3%.** Two consequences, and they are the reason this document is short on
datapath and long on interface:

- **Do not optimise throughput.** A 4-wide engine turns 144 sequential
  permutations into 75 (69.2% arrive in 4-groups, `mlkem_MxL.csv`'s `keccak_x1`
  and `slots` columns) and is worth **1.7%** at the pessimistic end and 0.06% at
  the optimistic one. A single blocking engine at M=8 requests occupies **0.69%**
  of the measured 40,994,244 cycles. Neither is a bottleneck.
- **Optimise area, interface size, and evaluation cleanliness.** Those are the
  axes on which this design can still be wrong.

> **Denominator discipline.** The 11,505,678 / 33,492,153 pair is from
> `tests/pqc/mlkem_profile` at **1 core / 4 warps / 4 threads**. The M×L
> throughput figures are from `tests/pqc/mlkem_width` at **8 warps**. They are
> different machines and must not be spliced. Every number in the table above is
> the profile build's; the throughput arm needs its own bound re-measured at
> 8 warps before any speedup is quoted against it.

---

## 2. The instruction

One instruction. Four independent designs were explored from different
optimisation targets and all four converged on the same shape, which is worth
something.

```
KECCAKF   rd=x0, rs1, rs2=x0
          opcode = INST_EXT1 = 7'b0001011 (0x0B)   VX_gpu_pkg.sv:289
          funct7 = 7'h05                            first free row; 0x00-0x04 taken,
                                                    0x03 is DXA (VX_decode.sv:719)
          funct3 = 3'h0  KECCAKF_U   warp-uniform
                   3'h1  KECCAKF_L   per-lane
          rs1    = byte address of a 25 x 64-bit state, 8-byte aligned
          rd, rs2 = x0. No register writeback.
```

`INST_EXT3` (0x5B) and `INST_EXT4` (0x7B) remain entirely unclaimed; funct7 0x05's
other six funct3 codes are reserved for the NTT half.

### 2.0 Why one instruction

Because Keccak-f1600 is a fixed function with no parameters, and the library's
contract at the ISA boundary is one line:

```c
int mlk_keccak_f1600_x1_native(uint64_t *state);   // permute these 200 bytes in place
```

That is the whole interface. Absorb, padding, domain separation, rate handling,
squeeze — the entire sponge — stay in the library's C, because this project has
committed to leaving the submodules byte-for-byte unmodified. That commitment is
what makes the two-column Keccak baseline and the equal-permutation-count
property possible, so it is not negotiable for a 1% gain.

The permutation takes 1600 bits, returns 1600 bits, runs 24 rounds, and has no
options. At the ISA boundary there are exactly two degrees of freedom:

| degree of freedom | encoded as |
|---|---|
| where the state is | `rs1`, one source register |
| which lanes take part | `funct3`, one bit |

There is nothing else to encode. One instruction is not minimalism, it is the
shape of the function.

What a larger instruction set would have bought, and why none of it is taken:

| candidate | why not |
|---|---|
| state move-in / move-out (`KWR`/`KRD` style) | needs 6.4 KB of new architectural state per core — see §2.0.1, which prices it properly |
| sponge-level absorb/squeeze | lets the state stay resident, worth ~1%; costs an ownership protocol and has to hook *above* the library API, which breaks equal permutation counts across arms (§3.1) |
| a separate x4 instruction | x4 is four independent states; `KECCAKF_L` with four lanes covers it, and the library's C fallback already decomposes x4 into four x1 calls, so even x1 alone is sufficient |
| async launch + wait | must live inside synchronous straight-line library code, so the handle has nowhere to survive and becomes pure cost (§3.2) |
| a round-level instruction | 24 issues per permutation and the state back in the register file — worse on both counts |
| a configuration instruction or DCR | there is nothing to configure; rate, padding and domain separation are all library-side |

### 2.0.1 Write-in / execute / read-back, priced

The obvious alternative is the classic triple: move the state into the PE, run
it, move it out. It is rejected here, but **not for the reason one would
expect**, and the wrong reason is worth stating so nobody re-derives it.

**The instruction count does not decide this.** Per permutation, against the
measured non-Keccak remainder of 11,505,678 cycles, the measured baseline of
33,492,153 (`pqc/results/ablation_mlkem.csv`), 144 permutations, and the
measured CPI of 9.537 (`pqc/results/keccak_baseline_columns.csv`):

| design | per permutation | 144 permutations | end to end |
|---|---|---|---|
| **A.** one instruction, PE addresses memory | ~64 cy (24 rounds + 400 B) | 9,216 | **2.909x** |
| **B.** `KLD`/`KST` shuttle, 50 + 1 + 50 = 101 instructions, no GPR round trip | 101 x 9.537 + 64 ~= 1,027 | 147,888 | **2.874x** |
| **C.** full GPR shuttle, 50 `lw` + 50 `KWR` + 1 + 50 `KRD` + 50 `sw` = 201 | 201 x 9.537 + 64 ~= 1,981 | 285,264 | **2.841x** |

The shuttle is worth **1.19% (B) to 2.34% (C)**. On a machine at CPI 9.5 where
one permutation costs 151,872 cycles, two hundred instructions are noise. Any
argument of the form "the shuttle is too slow" is wrong here, and B is the
honest strongest form of the opposing case — it should be beaten on its merits,
not on a number that does not hold.

**What decides it is architectural state.** A shuttle is needed only when the
PE holds state the core can reach exclusively through instruction operands.
That state is architecturally visible: it goes in the ISA manual, it is saved
and restored across a context switch, and in a SIMT core it is replicated per
hart. At this paper's best configuration (M=8 requests x L=4 lanes, so 8 warps
x 4 threads = 32 harts per core):

| | bytes per core |
|---|---|
| Keccak state file, 200 B x 32 harts | **6,400** |
| the entire integer register file, 32 x 4 B x 32 harts | 4,096 |
| integer + floating-point register files | 8,192 |

**One algorithm would add 1.56x the integer register file in new architectural
state.** That is what §7's "per-slot special-register marshalling" anti-pattern
is actually about. Design A's 200-byte staging buffer is microarchitectural
instead: the instruction blocks, the buffer is dead at every instruction
boundary, so it is never saved, never restored, invisible to software, and one
copy serves the whole core.

Two consequences follow:

- **`KECCAKF_L` multiplies the shuttle's cost by L.** Per-lane permutation needs
  a private state file per lane — four copies. Design A needs four pointers.
- **The encoding does not fit.** `KWR`/`KRD` need a slot index (0-24, plus a
  high/low half) = 6 bits of immediate. R-type has no room, since funct7 already
  selects the group, so they would have to be I-type: more encoding space, and
  against §2.5's preference for R-type.

At the W = 16 the project has since chosen, the same table reads 200 B x 128
harts = **25.6 KB** against a 16.4 KB integer register file. The 1.5625x ratio is
width-invariant; only the absolute figure moves.

The one-line version: the shuttle is not slow, it asks to put 6.4 KB of new
architectural state into the ISA manual -- 25.6 KB at W = 16.

This is not a one-instruction doctrine. NTT has real degrees of freedom —
forward versus inverse, possibly a twiddle pointer, possibly a layer index — and
gets funct7 0x05's remaining six funct3 codes and its own proposal. Keccak needs
one instruction because it has exactly one thing to do.

### 2.1 Why two funct3 codes, and not one

**Corrected.** An earlier version of this section claimed this was a
wrong-answer bug. It is not; it is a 4x redundancy. The claim rested on the two
library hooks calling with different pointer semantics, and the "identical
pointer" half of that is false:

| hook | how it is called | pointers across lanes |
|---|---|---|
| `mlk_keccak_f1600_x1_native` | every active lane calls it in redundant SPMD | **distinct addresses, identical bytes** |
| `mlk_keccak_f1600_x4_native` (`mlk_simt_fips202.h:60-67`) | each lane copies its sub-state into a per-lane stack `tmp[25]` | distinct addresses, distinct bytes |

Every Keccak state reaching the x1 hook is a stack automatic --
`fips202.c:208` `mlk_shake256ctx state`, `:221` `uint64_t ctx[25]`,
`sampling.c:221` `mlk_xof_ctx state` -- and `sw/kernel/src/vx_start.S:95-98`
gives every hart its own 8 KB slab
(`sp = VX_MEM_STACK_BASE_ADDR - (mhartid << VX_MEM_STACK_LOG2_SIZE)`). So four
active lanes name **four different addresses that happen to hold the same
bytes**. A purely per-lane instruction is therefore **correct** on both paths.
The failure mode this section used to describe -- one address, a serialising PE,
pass 2 reading what pass 1 wrote -- cannot occur in this harness.

What is true is the redundancy. On the x1 path a per-lane instruction makes four
lanes each permute a private identical copy: four permutations to produce one
useful result. That is exactly what the software baseline already does, so it is
not a regression -- but Keccak is 65.65% of ML-KEM, and paying 4x on the
redundant-SPMD path throws away most of what the instruction is for.

So the caller says which it means:

- **`KECCAKF_U`** (funct3=0): execute **once**, using `rs1` from the lowest
  active lane, and broadcast nothing -- each lane's own copy is separately
  correct only if it is separately permuted, so `KECCAKF_U` must either write
  every active lane's buffer or the software must be told one buffer is
  authoritative. **This is now an open question, not a settled design** (§8).
- **`KECCAKF_L`** (funct3=1): execute **once per active lane**, each on its own
  `rs1`. Correct on both paths; 4x redundant on the x1 path.

**Consequence for the "one instruction" argument (§2.0).** If `KECCAKF_U` turns
out to need a write-back-to-all-lanes semantics that `KECCAKF_L` does not, the
two funct3 codes are no longer a single decode bit over one datapath, and §2.0's
claim that the only degrees of freedom are "where" and "which lanes" needs
re-examining. The cheaper resolution is to ship `KECCAKF_L` alone and accept 4x
on the x1 path, then measure whether `KECCAKF_U` is worth its complexity. The
x1 path's share is measurable today from `mlkem_MxL.csv`'s `keccak_x1` column.

### 2.2 Semantics

For each lane the instruction serves: load 25 little-endian `uint64` from that
lane's `rs1`, apply Keccak-f[1600] (24 rounds, FIPS 202 §3.3), store the 200
bytes back to the same address. No GPR is read or written other than `rs1`.

The state **never enters the register file**. A lane's entire GPR file is
32 × 32 bit = 128 B against a 200-byte state, so §2.3 of
`docs/designs/custom_accelerator_isa_extensions.md` (register window) is not
merely undesirable here, it is arithmetically impossible. That is what forces
§2.4 — the accelerator addresses memory itself — and it is also why the earlier
experiment's KWR/KXOR/KRD shape (50 writes + 1 permute + 50 reads = 101
instructions per permutation on RV32) is the §7 anti-pattern *per-slot
special-register marshalling* named in that guide.

### 2.3 Guide conformance

| guide section | this design |
|---|---|
| §2.1 DCR | not used; there is no per-dispatch configuration |
| §2.2 lane-scatter | not used |
| §2.3 register window | **impossible** — 200 B against a 128 B register file |
| §2.4 custom LD into accelerator storage | **this is the design** — the PE fetches and writes the state itself, no `rd` writeback |
| §2.5 R-type vs R4-type | **R-type.** One source register is enough, and it keeps the 7-bit funct7 free rather than spending five bits of it on an `rs3` nothing needs |
| §5 async launch/handle/wait | **rejected, deliberately** — see §3.2 |
| §6 stream-to-destination | obeyed: responses land in the permutation register, no staging copy |
| §7 anti-patterns | avoids per-slot marshalling, field-by-field delivery, and register-file replication |

---

## 3. Two things this design deliberately does not do

### 3.1 It does not keep the state resident across a sponge

A sponge performs many permutations on one state — H(pk) over 1184 B is nine.
Keeping the state inside the PE across those nine would save eight round trips
of 400 B. Against a permutation whose end-to-end cost is a few hundred to a few
thousand cycles, that is on the order of **1%**, and it would cost a residency
and ownership protocol, per-slot 200-byte storage, an open/absorb/squeeze/close
instruction set instead of one instruction, and — the real objection — it would
have to hook **above** the library's permutation API, which breaks the property
`pqc/results/keccak_baseline_columns.csv` depends on: that every arm executes
the identical number of permutations, so the arms differ only in what a
permutation costs.

### 3.2 It is not asynchronous

§5 of the design guide recommends async launch with a handle for long-latency
operations, and a permutation is long. It is rejected here because the software
that calls it is synchronous straight-line code inside a library this project
has committed to keeping pristine: `mlk_keccak_f1600_x1_native` takes a pointer,
returns `SUCCESS`, and the caller reads the state on the next line. An async
handle would need somewhere to live across that return, which means either
forking the library or a wrapper that immediately waits — an async instruction
used synchronously, with the handle machinery as pure cost. If a future
sponge-level interface (§3.1) is ever built, async becomes worth revisiting with
it.

The warp is instead released by `is_wstall` at decode plus a completion pulse,
so ordering is architectural rather than a software contract.

---

### 3.3 It does not distribute the state across lanes

That is school D, and it has its own document:
[`cooperative_ise_proposal.md`](cooperative_ise_proposal.md). It is a real
alternative rather than an anti-pattern, and it beats this design on the one axis
where this design is weakest -- **it adds zero architectural state**, because the
Keccak state stays in ordinary GPRs. §2.0.1's 25.6 KB objection does not apply
to it at all.

The reasons this proposal stands anyway, all from that document:

| | school D (SG5 at W=16) | this design |
|---|---|---|
| new architectural state | **none** | 25.6 KB/core |
| instructions per permutation | 1,512 (with its own 5-instruction set) | 1 |
| **throughput** vs the PQRV baseline | measured **1.73x** (§4.1 there) | the 2.911x Amdahl bound is the ceiling |
| latency vs the PQRV baseline | **9.76x** | higher, engine-limited |
| lanes occupied per permutation | 5 of 16 | 1 |
| at plain ISA, no new instructions | **0.66x -- loses to plain SIMT** | n/a |

Two findings from that analysis bear directly on this one:

1. **School D's cooperative instructions are only 25% of its own gain.** The
   attribution is `KRHO` 58% and `KXAN` 18% -- both lane-internal, both school A --
   against `KXPOSE` 16% and `KTHL`/`KTHH` 9%. The largest win in a *cooperative*
   Keccak layout on this machine is a 64-bit funnel shift, which is not
   cross-lane at all.
2. **The two schools are complementary, not competing.** School D wins on
   single-permutation latency (measured 2.60x at plain ISA), which is what the serial-chain share of ML-KEM's 144
   permutations forming a strictly serial sponge chain need -- 31%, and the one
   place this design's per-lane engine runs 15 of 16 lanes idle. This design wins
   on the batchable remainder. A hybrid is a better paper claim than either
   school beating the other.

**Neither is decided.** Both need the measurements in their respective §9s.

## 4. The one genuinely open question: ordering against the caller's stores

The caller writes the state through the ordinary LSU; the PE reads it through
its own path. **Nothing currently orders those two.** This has to be solved
before implementation and it is not solved here.

What does *not* work: `vx_fence()`. In Vortex a fence is not a lightweight
ordering operation — `hw/rtl/core/VX_lsu_slice.sv:93` sets
`mem_req_attr_struct[i].is_flush = req_is_fence`, i.e. it is a **whole-dcache
flush**. Emitting one per permutation would cost far more than the entire 3.3%
design space is worth. Any design sketch that sprinkles fences around this
instruction has mispriced it.

Three candidates, in order of preference:

1. **Scoreboard the address.** Stall `KECCAKF` at issue until the warp's
   outstanding stores have been accepted. Vortex already tracks outstanding
   memory operations per warp (`VX_CFG_LSU_PENDING_SIZE`); this reuses that
   rather than adding a mechanism.
2. **Share the LSU client.** Route the PE's requests through the same port the
   lane LSU uses, so the dcache sees one ordered stream per warp. Costs
   arbitration; removes the question entirely.
3. **A dedicated lightweight ordering op.** A new instruction that waits for
   store drain without flushing. Cleanest semantics, most new ISA surface, and
   it is a second instruction where the whole point was to have one.

Note also that `LSU_SCHED_NUM_CLIENTS` is 2 only under `TCU_META_ENABLE` and 1
otherwise (`hw/rtl/core/VX_core.sv:376`), so at the evaluation configuration —
TCU off — adding a PE client is itself a change to that parameterisation, not
free wiring.

---

## 5. Placement: one config key, one A/B

```toml
[pqc]
VX_CFG_PQC_KECCAK_ENGINES = 1     # 1 .. VX_CFG_NUM_SFU_LANES
```

The number of Keccak engines inside the single per-core PE. The PE serves the
lanes an instruction addresses in `ceil(n / ENGINES)` passes.

This is a **pure replication factor with no architectural visibility**: the
encoding, the semantics, the intrinsic, the library hook and the compiled binary
are byte-identical at `ENGINES=1` and `ENGINES=4`. Per-core versus per-lane is
therefore a parameter sweep over one source tree — `ENGINES=1` is one engine
shared by every lane of every warp, `ENGINES=4` is one per lane of a warp — and
not two implementations wearing one name. That is exactly the property the
evaluation needs, and the reason it is worth insisting the instruction be
defined in terms of *which lanes it serves* rather than *where the engine is*.

The PE itself hangs off the SFU as `PE_IDX_PQC`, following the cumulative
`PE_COUNT` / `PE_IDX_*` construction at `hw/rtl/core/VX_sfu_unit.sv:71-88`. The
SFU is `BLOCK_SIZE=1` — one instance per core serving all warps — which is
already the placement §7.3 of the survey recommends.

### 5.1 MEASURED: the replication factor costs exactly what it says

`pqc/results/keccak_pe_area.csv`, `hw/rtl/pqc/VX_pqc_keccak_f1600.sv` through
yosys to Nangate 45nm. Not the V80 — use it for ratios, not absolutes.

| engines | cells | area µm² | vs 1 |
|---:|---:|---:|---:|
| 1 | 9,909 | 22,471 | 1.00× |
| 2 | 19,818 | 44,939 | **2.00007×** |
| 4 | 39,637 | 89,885 | **4.00004×** |

**Exactly linear, and that is the finding.** There is no sharing to discover: the
engines are independent and each carries its own 1600-bit state, which is ~1,600
of the ~1,610 flops. So **the per-core versus per-lane argument cannot be won on
area** — at W=16 a per-lane PE is 16 × 22,471 = 359,543 µm², precisely the naive
16× — and has to be won on utilisation instead. §6's arithmetic is therefore the
load-bearing part of the case, not a supporting one.

The rounds-per-cycle axis *is* settled by measurement, and against unrolling.
Logic depth grows linearly with it, ~7.9 gate levels per added round, so the
clock period lengthens at the same rate the cycle count falls:

| rounds/cycle | 1 | 2 | 4 | 8 | 24 |
|---|---:|---:|---:|---:|---:|
| cycles | 24 | 12 | 6 | 3 | 1 |
| logic depth | 11 | 18 | 35 | 69 | 193 |
| cycles × depth | 264 | 216 | 210 | 207 | **193** |
| area vs 1 | 1.00× | 1.47× | 2.71× | 4.99× | **16.06×** |

**1.37× of wall-clock proxy for 16× the area. Take one round per cycle.**

**Not a knob:** the datapath width. A 1-round-per-cycle engine and a lane-serial
engine share no RTL, so a key selecting between them would be two designs behind
one name. Pick one, measure it, and report the other as future work if it
matters.

---

## 6. What this does to the L axis

**The L axis survives, and that was a design constraint rather than a
side effect.**

The measured lane axis — 1.485× on ML-KEM, 1.601× on ML-DSA, and the 8.280× /
9.306× best cells — exists only because
`tests/pqc/mlkem_width/mlk_simt_fips202.h` puts **one Keccak state on each lane
of a warp**. The earlier `origin/keccak_warp` experiment held **one state per
warp and read operands from lane 0 only**, so adopting that shape would have
collapsed four lanes onto one state and retired the axis — a paper-structure
decision disguised as a microarchitectural one.

`KECCAKF_L`'s operand is per-lane by construction, so the SIMT backend keeps its
exact structure and the axis keeps its meaning. What changes is what the axis
*measures*: with a hardware permutation, the lane axis stops being about
splitting Keccak work and becomes about how many engines are available, which is
`ENGINES`. Both halves stay measurable, and the paper can report them
separately: the software lane axis as a software result, the engine count as a
hardware one.

---

## 7. Verification

Non-negotiable, in this order:

1. **KAT before cycles.** `tests/pqc/mlkem` compares pk/sk/ct/ss byte for byte
   against the FIPS 203 vectors, per request. A cycle count without that
   attached is not a measurement — this tree has produced three plausible wrong
   numbers that only a correctness check caught.
2. **SimX/RTL parity.** Both models must agree on instruction count exactly and
   on cycles within the repo's stated tolerance. The RORI divergence filed in
   `docs/proposals/vortex_upstream_defects.md` is what happens when they do not.
3. **A device guard.** A binary built with `KECCAKF` must refuse to run on a
   device without the unit, following `pqc_config.h`'s `require_slots` and
   vortexCrypto's ISA-flag check. Without it the encoding decodes as something
   else — and, per that same defect report, differently in RTL than in SimX —
   and produces a plausible wrong answer instead of an error.
4. **Both hooks, both funct3 codes.** x1 through `KECCAKF_U` and x4 through
   `KECCAKF_L`, each verified at L = 1, 2 and 4, with the width-invariance
   checksums `tests/pqc/mlkem_width` already asserts.

---

## 8. Not decided

- **The ordering mechanism** (§4). Blocking; must be settled first.
- **Datapath width** — 1 round/cycle against a lane-serial engine. Worth
  ~4,400 LUT and ~3% end-to-end; decide with a synthesis number, not here.
- **Area, at all.** No Keccak-f1600 hard-macro anchor has ever been obtained for
  this project, and the earlier experiment was never synthesized. The survey's
  per-core recommendation still rests on two legs — utilisation and
  performance-indistinguishability — and this proposal does not add the third.
- **`ENGINES` default for the paper's headline configuration.** Sweep first.
- **NTT.** Its bound is 1.136× (ML-KEM) / 1.221× (ML-DSA at `RAM=full`) but its
  lane scaling is the best measured anywhere here (3.94×, 98.5-98.7%). It gets
  funct7 0x05's remaining funct3 codes and its own proposal.

---

## 9. First step

Not RTL. **Measure the ordering cost**, because §4 is the only thing that can
still make this design expensive, and it can be priced without building
anything: instrument how many cycles a `KECCAKF`-shaped stall would cost by
adding a scoreboard dependency on the state address in the SimX model alone and
running `tests/pqc/mlkem` with the existing software permutation. If the answer
is small, candidate 1 wins and the design is settled. If it is large, candidate
2 becomes necessary and the LSU client work grows.

Everything else in this proposal is cheap to change afterwards. The ordering
mechanism is not.
