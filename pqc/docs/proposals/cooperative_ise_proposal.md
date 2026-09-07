# Cooperative (cross-lane) ISE for Keccak — school D, at W=16

Companion to [`keccak_ise_proposal.md`](keccak_ise_proposal.md), which proposes a
single pointer-taking `KECCAKF` instruction. This document proposes the opposite
thing — instructions that read the same register name across several lanes, with
the Keccak state distributed in ordinary GPRs — and reports honestly what it is
worth.

**Status: a design and a measurement plan, not a decision.** Every instruction
count here is derived, none is simulated. The one thing that *is* verified is the
layout's correctness (§2.4).

---

## 0. The four schools, and where this one sits

| | what the instruction sees | where the state lives |
|---|---|---|
| **A** Zbb/Zbkb/Zknh, Bolat et al. | one lane's GPRs | GPRs |
| **B** private crypto register file | a PE-private file | **new architectural state** |
| **C** [`keccak_ise_proposal.md`](keccak_ise_proposal.md), RISQ-V, HORCRUX | memory, via a pointer | memory |
| **D** *this document* | **the same register name across N lanes** | **ordinary GPRs, distributed** |

D is the only one of the four that does not exist on a scalar RISC-V core. That
is its entire claim to novelty, and §6 states how narrow that claim actually is.

---

## 1. The decision that fixes everything: W = 16

25 = 1 × 25 = 5 × 5. Those are the **only** lane counts that divide a Keccak
state without raggedness, and a SIMT register file cannot address a register
whose name depends on the lane — so a ragged layout turns every step into masked
merges. At W = 16 the design space is therefore exactly **{SG1, SG5}**:

| layout | lanes/state | states/warp | occupancy | verdict |
|---|---|---|---|---|
| **SG1** — one lane holds the whole state | 1 | 16 | 100% | the school A/B/C baseline |
| **SG5** — lane *x* holds column `A[x][0..4]` | 5 | 3 | **93.75%** | **this proposal** |
| SG5r — lane *y* holds row `A[0..4][y]` | 5 | 3 | 93.75% | rejected, §2.3 |
| SG16 — 16 lanes, 9 hold 2 words | 16 | 1 | 78.1% | rejected, ragged (§1.1) |
| SG25/SG32 — lane *t = x+5y* holds one word | 25 | — | — | **does not fit in 16 lanes** |

### 1.1 Why SG16 is not a layout

25 = 16 + 9, so nine lanes would hold two words. Aligning θ-columns to slot
planes needs three columns (15 words) in plane 0 and two (10 words) in plane 1,
but plane 1 has only 9 slots. No assignment works, and the χ-rows are transversal
to the columns anyway. Rejected structurally, not numerically.

### 1.2 W = 16 is the *worst* of the three regular widths for school D

Stated plainly because it is the honest framing. At W = 32, SG25 becomes
available and every lane holds exactly one word, which makes the slot partition
trivial, which makes the theorem in §2.2 vacuous, which makes every Keccak data
movement a pure lane permutation. **School D is strictly better at W = 32.** The
project chose W = 16 for reasons that have nothing to do with Keccak, and this
proposal lives with it.

---

## 2. The SG5 layout

Lane `l` holds column `x = l mod 5` of state `g = l / 5`. Lanes 0–14 carry three
states; **lane 15 is permanently idle**. Each lane holds 5 words × 2 RV32
registers = 10 GPRs, so 5 lanes × 10 = **200 bytes exactly**, the whole state.

### 2.1 What the column layout buys, and what it costs

The central question is what is exchanged for making θ cheap. The answer is
narrower than expected:

| step | SG5 (lane = column) | SG25 (lane = word), for contrast |
|---|---|---|
| **θ** `C[x] = ⊕ᵧ A[x][y]` | **lane-internal**, 4 XOR64 = 8 instructions, zero cross-lane | cross-lane 5-way all-reduce, 6 SHFL + 6 XOR |
| **θ** `D[x] = C[x-1] ⊕ ROL64(C[x+1],1)` | cross-lane, 2 neighbours = 4 `SHFL_IDX` | cross-lane, same |
| **ρ** | lane-internal, but the amount `r[x][y]` is now **per-lane** | rotate amount is warp-uniform per instruction |
| **π** | **cross-lane but slot-preserving** — 10 `SHFL_IDX` | pure lane permutation |
| **χ** | **lane-internal** in the post-π layout | cross-lane, 3 lanes same row |
| χ → θ | **one unavoidable transpose** (§2.2) | none |

**π is not a scatter.** This was the obvious objection and it is wrong. Store `B`
with lane = its *second* index — which is exactly the grouping χ needs — and π
becomes five slot-preserving lane bijections: for slot `y`, lane `d` gathers slot
`y` from lane `(3d + y) mod 5`. Verified bijective for all five slots.

The price is paid in exactly two places:
- **ρ's rotate amount becomes per-lane**, so the immediate-shift form is gone. A
  branch-free per-lane `ROL64` on RV32I is 12 instructions per word. This is the
  single largest cost in the whole layout — and it is not a cross-lane problem
  at all (§6).
- **One transpose per round**, which §2.2 proves cannot be avoided.

### 2.2 The transpose is provably necessary

χ's output lives in the lane = *y* partition; the next round's θ wants lane = *x*.
That transition **cannot be slot-preserving**, so `SHFL` cannot express it.

*Sketch.* χ in lane `b` combines the five cells whose `B`-first-index is
`a = 0..4`, and the cell with `B`-index `a` in lane `b` is cell `(3b + a, a)`. A
SIMT instruction encodes its operand slots, so `slot(3b + a, a)` must not depend
on `b`; since `x = 3b + a` covers all of ℤ₅ as `b` varies, this forces
`p(x, y) = σ(y)` for a single bijection σ. χ then writes `A″[a][b]` in lane `b`
at a warp-uniform slot `τ(a)`, while the next θ wants it at slot `p(a,b) = σ(b)`.
Slot-preservation — even allowing one global relabel — requires `k(τ(a)) = σ(b)`
for all `a, b`, forcing σ constant and contradicting bijectivity. ∎

Confirmed by exhaustive search over all 1,728,000 `(σ, τ, k)` triples: zero
solutions. At plain ISA the transpose is therefore a 20-instruction memory round
trip. `KXPOSE` (§3) is the escape hatch, and it works only because a register-window
macro-op is precisely an instruction whose source register name is *not*
warp-uniform.

### 2.3 SG5r (lane = row) is dominated

Included as the control that isolates what lane = column buys. 224 instructions
per round against 176, and **all 48 of the difference is in θ** (88 vs 30). SG5r
pays 36 extra cross-lane SHFL for the column reduction and gets nothing back: it
still needs exactly one slot-crossing round trip (the theorem is
orientation-independent), χ costs the same, ρ costs the same, and π's cheap
slot-preserving form is lost.

*Also tabulated so the search does not look unexplored:* the dual arrangement —
store `B` at lane = first index, pay π through memory, get χ cross-lane but
slot-preserving with no transpose — costs 70 instructions against the recommended
arrangement's 64. **The recommendation wins by 6 instructions, not by structure.**

### 2.4 The layout is verified; the costs are not

The SG5 round was written as a five-lane software simulation and checked against
reference Keccak-f1600 on **200 random states and the FIPS 202 all-zero KAT**
(`A[0][0] = f1258f7940e1dde7`), agreeing round by round. That discharges the
correctness question completely.

Nothing else here is verified. Every instruction count is a derivation.

---

## 3. The instruction set

Encoding home: `INST_EXT1` (0x0B), **funct7 = 0x06**. Row 0x05 is claimed by
[`keccak_ise_proposal.md`](keccak_ise_proposal.md) with its remaining funct3 codes
reserved for NTT; 0x06 is the first free row and its 8 funct3 codes cover all
five mnemonics with three to spare. (`funct7` 0x00–0x04 are taken; 0x05–0x7F are
free — grepped across both decoders and every `.insn` site.)

Lane algebra: `l` = lane, `g = l/5`, `c = l mod 5`, `m = 5g`,
`p = m + (c+1) mod 5`, `q = m + (c+4) mod 5`.

### `KTHL` / `KTHH  rd, rs1, rs2` — funct3 0, 1 — **school D**

With `rs1 = C_lo`, `rs2 = C_hi` (each lane's own column parity, computed
lane-internally):

```
KTHL:  rd[l] = rs1[q] ^ ( (rs1[p] << 1) | (rs2[p] >>> 31) )
KTHH:  rd[l] = rs2[q] ^ ( (rs2[p] << 1) | (rs1[p] >>> 31) )
```

Together `{rd_h, rd_l} = C[c-1] ^ ROL64(C[c+1], 1) = D[c]`. Replaces 4 `SHFL_IDX`
+ 6 (`ROL64` by 1) + 2 XOR with two instructions. **Saving: 10/round.**

Purest school D: the operand collector already presents the whole warp vector of
`rs1` and `rs2`, so reading lanes `q` and `p` of the *same register name* costs no
extra read port — only fixed 5-lane rotate muxing, narrower than the existing
16×16 SHFL crossbar. R-type, no rs3, no immediate; conforms to design guide §2.5.

### `KRHO  rd, rs1, #y` — funct3 2 — **school A**

```
{rd+1, rd} = ROL64( {rs1+1, rs1}, RHO[c][y] )
```

`y` is a 3-bit slot index in the `rs2` instruction field, immediate-style with no
register read (precedent: `TCU_LD`). `RHO` is a hardwired 25 × 6-bit ROM (150
bits) indexed by `(lane mod 5, y)`. Two source and two destination registers, so
a 2-uop macro-op through `VX_uop_sequencer.sv`.

Replaces 12 instructions per word with 2 **and deletes all 15 per-lane ρ constants
and the 15 loads that staged them. Saving: 65/round — the largest single win in
this design, and it is not cross-lane.**

### `KXAN  rd, rs1, rs2, rs3` — funct3 3 — **school A**, R4-type

```
rd = rs1 ^ ( (~rs2) & rs3 )
```

Vortex's operand path already carries rs3 (`NUM_SRC_OPDS = 3`; verified generic,
not hardwired — only 7 consumers, and 5 units tie it off with `UNUSED_VAR`), so R4
is not new plumbing. **Saving: 20/round.** This is Zbkb's `andn` plus an XOR,
fused; ratified `andn` alone would capture half of it.

### `KXPOSE  rd, rs1` — funct3 4 — **school D**, the step SHFL provably cannot do

A 10-register-window macro-op (design guide §2.3). Reads `{rs1..rs1+9}` across the
5 lanes of each group and writes `{rd..rd+9}`:

```
rd+2y+0 [m+x] = rs1+2x+0 [m+y]      (lo halves)
rd+2y+1 [m+x] = rs1+2x+1 [m+y]      (hi halves)
```

Destination lane `x` slot `y` receives source lane `y` slot `x` — the 5×5
transpose, in the register file, no memory. Replaces 10 `sw` + 10 `lw` with one
instruction and, more importantly, **removes the fence** (§5, risk 1).
**Saving: 18/round.**

*Fallback if a 10-register window is judged too invasive:* `KSROT rd, rs1, #k`
→ `rd[l] = (register rs1 + ((k + c) mod 5))[l]`, a per-lane register-indexed read
from a 5-register window. The transpose becomes 30 instructions instead of 1 —
still fence-free, with 5-register windows.

---

## 4. What it is worth

Per round, against PQRV's hand-written RV32 assembly as SG1
(`pqc/third_party/pqrv/fips202_rv32im.S`, machine-assembled and counted:
**615 instructions per round** — 116 `lw` + 102 `sw` + 396 ALU + 1 branch — and
14,885 dynamic instructions per permutation):

| | instructions/round | per permutation | latency vs SG1 | **throughput** vs SG1 |
|---|---:|---:|---:|---:|
| SG1 (PQRV) | 615 | 14,885 | 1× | 1× |
| SG5, plain ISA | 176 | 4,224 | **3.49×** | **0.66× — worse** |
| SG5 + the five instructions | **63** | **1,512** | **9.76×** | **1.83×** |

**The plain-ISA row is the one that matters most.** SIMT already gives 16-way
parallelism across independent states for free; a cooperative layout has to beat
that before it has done anything, and at plain ISA it does not — 58.7
warp-instructions per state-round against SG1's 38.4.

SG5's plain-ISA win is entirely on **latency**, and there is a real place for it:
48 of ML-KEM's 156 permutations (31%, from `mlkem_MxL.csv`'s `keccak_x1` 156→75
with `slots` 27 at L=4) form a strictly serial sponge chain on which SG1 runs 15
of 16 lanes idle.

**No end-to-end speedup is computed here and none should be spliced from these
numbers.** Multiplying an instruction ratio onto `ablation_mlkem.csv`'s 3.098×
would violate the denominator discipline both that file and
`keccak_baseline_columns.csv` insist on: the ablation is the profile build at
4 warps × 4 threads, the M×L figures are at 8 warps, and these counts are at 16.

---

## 5. The finding that decides how to write the paper

Attribution of the 113 instructions the ISE saves per round:

| instruction | saved | share | school |
|---|---:|---:|---|
| `KRHO` | 65 | **58%** | **A** — lane-internal |
| `KXAN` | 20 | **18%** | **A** — lane-internal |
| `KXPOSE` | 18 | 16% | D — cross-lane |
| `KTHL`/`KTHH` | 10 | 9% | D — cross-lane |

**The two cooperative instructions account for 25% of the gain.** The largest
single win in a cooperative Keccak layout on this machine is a 64-bit funnel
shift, which is not a cross-lane instruction at all.

A paper that claims the cooperative school's advantage *is* cross-lane fusion
will be caught on this. A paper that reports it is stronger for having found it.

---

## 6. Novelty, stated at its true width

**School D is novel as an ISA mechanism on a SIMT GPGPU. The layout is not.**

- **cuDilithium / Shen et al. already do the layout.** *"each thread stores 64-bit
  of the state in registers … warp-shuffle is employed in each round"*
  **[全文，前序核]**. D's delta over them is "same layout, but the cross-lane step
  is an ISA primitive rather than a library call." That is real but narrow, and a
  reviewer who knows cuDilithium will say the layout is theirs. **The paper must
  claim the fusion, not the layout** — and must quantify what fusion buys over
  stock shuffles, which this repo has never measured (there is no `vx_shfl`-based
  Keccak anywhere in `pqc/results/`).
- **Adams et al. [49] is the only GPU crypto ISE in the survey**, confirmed
  independently by SoK [54]. It goes the other way — per-thread replication of an
  AES S-box. So "SIMT GPGPU + crypto ISE" contains exactly one prior point, and it
  is school A.
- **Li/Mentens/Picek's `EleNum=5`** is the closest structural precedent for the
  five-lane grouping.

### 6.1 A correction to how the 5-vs-25 evidence has been used

Lee et al. (*IEEE Access* 6:37991-38002, 2018) is the only work with a head-to-head
1 vs 5 vs 25 threads-per-permutation comparison, and one thread wins at 28.51 Gb/s
**[全文，前序核]**. **This has been over-cited in this project, including by me.**
The survey already records the caveat at line 753: their 5T and 25T variants use
**shared memory**, not warp shuffle — *"5T-Keccak and 25T-Keccak need to use shared
memory for sharing intermediate state values … involved a lot of overhead"* — so
the result **does not refute a shuffle-based cooperative design**. Line 754 adds
that the ordering reverses with batch size: *"1T-Keccak only outperform the other
two implementations when the tree height reach certain level."*

The load-bearing claim for a five-lane group is survey line 756, which does **not**
cite Lee et al.: θ's `C[x]` reduces across one plane, Li/Mentens/Picek's
`EleNum = 5` is one state, Ye et al. independently sized a SIMD register file at
5 × 64 bit. SG5 is supported by that convergence, not by Lee et al.

---

## 7. Hardware facts, checked against the RTL

**Good news — the packet-width worry is unfounded:**
- `NUM_ALU_LANES` traces to `SIMD_WIDTH` traces to `NUM_THREADS` with no default
  override anywhere in the tree. **At W = 16 the ALU packet is 16 lanes**, so
  cross-lane reach is not limited and SG5 is not broken by packet splitting.
- `LANE_BITS = CLOG2(16) = 4`, passing `VX_alu_int.sv:181`'s `<= 6` assert with slack.
- **`SHFL_IDX` with `mask = 0` already gives arbitrary per-lane indexing across all
  16 lanes**, so a five-lane column reduce is buildable *today* with no new
  hardware. That is the plain-ISA SG5 row of §4, and it is the cheapest possible
  first experiment.

**Constraints that must be respected:**
- **`vx_wgather` is quad-scoped, not warp-scoped.** `VX_alu_int.sv:160-168`:
  *"each group of 4 lanes operates independently. Source lane = (i & ~3) |
  wg_src_offset"*. It cannot move a value across a quad boundary and must be
  dropped from any school D feasibility argument.
- **SHFL's segmentation cannot express a 5-lane group.** `minLane = i & mask`
  (`:187`) only expresses power-of-two-aligned groups. `mask = 0` (full-warp
  indexing) is the way in.
- **Lane 15 is a silent-wrong-answer trap.** `VX_alu_int.sv:221` makes a shuffle
  from an inactive lane return *the reader's own value* rather than faulting, so a
  descriptor that accidentally names lane 15 produces plausible wrong bytes
  instead of an error.
- **The `alu_op[3]` tmask trap at `VX_alu_int.sv:286` is RTL-only** — SimX keys on
  a distinct variant alternative. A new school D op that trips it would **pass
  simx and fail rtlsim**, surfacing as a `model_parity` divergence rather than an
  obvious bug.

---

## 8. Risks, in order

1. **The fence may kill plain-ISA SG5 outright.** The transpose at plain ISA is a
   store-then-load between lanes of one warp. The repo's only cooperative kernel
   does exactly this and puts a barrier on it
   (`tests/pqc/mlkem_ntt_xn/kernel.cpp:74-76`). Every §4 number assumes the
   barrier is free. It is not. `KXPOSE` exists to delete it.
2. **Every number here is an instruction count, not a cycle count, and the machine
   is latency-bound at CPI 9.537.** The ranking can invert. SG1's 218 memory
   instructions per round each carry 16 divergent addresses into 16 × 200 B of
   live state against a 16 KB D-cache — SG5 might win on cycles by more than the
   instruction ratio, or lose to macro-op expansion.
3. **`KXPOSE` and `KRHO` are macro-ops and §4 counts issue slots, not cycles.** A
   10-register window against a 4-port collector is plausibly 10–20 cycles, which
   would move the round well above 63. **This is the least certain number in the
   document.**
4. **SG5's memory traffic is 35 instructions per round, not zero.** 15 ρ-constant
   loads + 20 transpose, against SG1's 218 — a 6.2× reduction, not elimination.
   "The state never touches memory" is true only between rounds, and only becomes
   true outright with `KRHO` and `KXPOSE`.
5. **The ρ constant budget is the softest number in the 176.** 15 per-lane
   constants against ~30 usable GPRs; budgeted as 15 loads per round. That is the
   whole ±15 band, and it disappears entirely with `KRHO`.
6. **Peak register pressure during π is not proven to fit.** π is architecturally
   in-place (`VX_alu_int.sv:221` computes combinationally from the whole warp
   vector), so it needs no extra registers — but whether the compiler chooses
   in-place SHFL is a codegen question a real compile settles. χ runs with zero
   temporary slack.
7. **The survey's own recorded prior is against this design.** §13.2 R4: the
   verified evidence *"only says the shared-memory 25-thread version loses to 1
   thread"*, and *"on the measured saturation curve it is a priori unlikely to
   win: four independent states already need zero communication and still only
   reach 1.485×."* School D appears in none of §12's starred experiments.

---

## 9. First step, and it costs nothing

**Write plain-ISA SG5 as a Vortex kernel using `vx_shfl_idx` with `mask = 0`.**
No RTL, no SimX, no new instruction — §7 establishes it is buildable today.

It produces:
- the first measured cycle count for a cooperative Keccak on this machine;
- the first measurement of `vx_shfl`-based Keccak anywhere in `pqc/results/`,
  which §6 identifies as the number the novelty claim depends on;
- a check of whether the fence (risk 1) is fatal;
- the plain-ISA row of §4, converting three derived numbers into measured ones.

Harness: `tests/pqc/mlkem_width` is the closest skeleton, but **wire its stack and
arena probes into `errors` first** — `main.cpp:101-137` counts only KEM status,
`ss_enc != ss_dec` and the four FNV checksums; `h_stk[0..3]` are printed at `:144`
and never compared. W = 16 quadruples the hart count that could hide an overflow.

If plain-ISA SG5 loses on cycles as badly as §4 predicts it loses on instruction
count, that is a publishable result and the ISE case rests on §5's 25%.
