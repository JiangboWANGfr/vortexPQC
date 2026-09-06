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
**11,443,248** cycles of a **35,447,271**-cycle round trip over **156**
permutations, so the Amdahl bound for a free Keccak is **3.098×**. Sweeping the
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

- **Do not optimise throughput.** A 4-wide engine turns 156 sequential
  permutations into 75 (69.2% arrive in 4-groups, `mlkem_MxL.csv`'s `keccak_x1`
  and `slots` columns) and is worth **1.7%** at the pessimistic end and 0.06% at
  the optimistic one. A single blocking engine at M=8 requests occupies **0.69%**
  of the measured 40,994,244 cycles. Neither is a bottleneck.
- **Optimise area, interface size, and evaluation cleanliness.** Those are the
  axes on which this design can still be wrong.

> **Denominator discipline.** The 11,443,248 / 35,447,271 pair is from
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

### 2.1 Why two funct3 codes, and not one

This is the one place where the obvious design is wrong, and it is a
wrong-answer bug rather than a slowdown. The two library hooks call with
**different pointer semantics**, verified in
`tests/pqc/mlkem_width/mlk_simt_fips202.h`:

| hook | how it is called | pointers across lanes |
|---|---|---|
| `mlk_keccak_f1600_x1_native` (`:39-44`) | every active lane calls it with the **same** `state` — the kernel is redundant SPMD | **identical** |
| `mlk_keccak_f1600_x4_native` (`:60-67`) | each lane copies its sub-state into a **per-lane stack `tmp[25]`** and permutes that | **distinct** |

A purely per-lane instruction is correct for the x4 path and **wrong** for the
x1 path: with four active lanes naming one address, a PE that serialises lanes
applies the permutation four times. In software this is harmless because every
lane computes and stores the same bytes; in hardware it is not, because pass 2
reads what pass 1 wrote.

So the caller says which it means:

- **`KECCAKF_U`** (funct3=0): execute **once**, using `rs1` from the lowest
  active lane. All lanes observe the same result because they named the same
  memory. This is what the x1 hook emits.
- **`KECCAKF_L`** (funct3=1): execute **once per active lane**, each on its own
  `rs1`. This is what the x4 SIMT backend emits.

One decode bit, no hardware deduplication, and both hooks stay one line. The
alternative — wrapping the x1 hook in `vx_tmc_one()`, which is what the earlier
`origin/keccak_warp` experiment was forced into
(`tests/regression/keccak_bench/kernel.cpp`) — pays a warp reconvergence per
permutation and hides the semantics in the software.

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
