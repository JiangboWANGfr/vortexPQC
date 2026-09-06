# Upstream defect reports — Vortex core

Defects in the Vortex core itself, as opposed to the AMD V80 / SLASH platform
issues in [`v80_upstream_defects.md`](v80_upstream_defects.md). Filed here so
they can be sent to `vortexgpgpu/vortex` without re-deriving them.

Found against this fork; each is checked to exist in the corresponding upstream
file and is not something this fork introduced.

---

## 1. Unallocated OP-IMM `funct7` encodings decode as SRLI/SRAI, and the RTL and SimX disagree about which

**Severity: medium.** No wrong answer is possible today, because nothing emits
these encodings. It becomes a silent wrong-answer bug the moment anyone builds
with a `-march` string the hardware does not implement — and the two models
disagree, so RTL and SimX would produce *different* wrong answers, which is the
one failure mode a dual-model project most needs to avoid.

### What happens

For OP-IMM (`opcode = 0010011`) with `funct3 = 101`, RISC-V allocates the
`imm[11:5]` field to distinguish shift variants:

| `imm[11:5]` | instruction | extension |
|---|---|---|
| `0000000` (0x00) | `SRLI` | RV32I |
| `0100000` (0x20) | `SRAI` | RV32I |
| `0110000` (0x30) | `RORI` | **Zbb** |

Vortex implements neither Zbb nor an illegal-instruction path for this field.
Both models therefore fold `0x30` into one of the two shifts they do know — and
they fold it differently.

**RTL — `hw/rtl/core/VX_decode.sv:112`**

```systemverilog
3'h5: r_type = funct7[5] ? INST_ALU_SRA : INST_ALU_SRL;
```

`0x30 = 0b0110000`, so `funct7[5] = 1` → **SRA**.

**SimX — `sim/simx/decode.cpp:560`**

```cpp
case 5: instr->set_op_type((((funct7 >> 1) == 0x10)) ? AluType::SRA : AluType::SRL); break;
```

`0x30 >> 1 = 0x18`, which is not `0x10` → **SRL**.

So the same instruction word is an arithmetic shift in hardware and a logical
shift in the simulator. On a negative operand the two produce different results.

Neither model raises an illegal-instruction exception: the RTL line is a bare
ternary with no default, and the SimX `switch` reaches `std::abort()` only for a
`funct3` outside 0–7, never for an unallocated `funct7`.

### Why the two lines differ

The SimX form is deliberate and is correct for what it was written to fix — the
comment above it (`decode.cpp:556-559`) explains that on RV64 `SRAI` with
`shamt >= 32` puts `shamt[5]` in bit 25, so `funct7` reads back as `0x21` rather
than `0x20`, and matching on `funct6` keeps that from decoding as `SRLI`. The
RTL's `funct7[5]` test happens to accept both `0x20` and `0x21` as well, so the
two agree on every *allocated* encoding. They diverge only outside the allocated
set, which is exactly where neither was thinking.

### Reproduction

Static, from the two lines above. To see it dynamically, assemble any `rori`
(the vendored toolchain accepts `-march=rv32imaf_zbb` and emits it) and run the
same binary under `--driver=simx` and `--driver=rtlsim` with a negative input.

```sh
# the toolchain will happily produce an instruction the hardware cannot execute
echo 'unsigned f(unsigned x){return (x>>7)|(x<<25);}' > /tmp/z.c
$TOOLDIR/llvm-vortex/bin/clang --target=riscv32-unknown-elf \
  -march=rv32imaf_zbb -mabi=ilp32f -O2 -S -o - /tmp/z.c | grep rori
```

### How we hit it

We were costing whether to add Zbb to Vortex so that a hand-written Keccak could
use `rori`, and checked what the existing decoders do with the encoding. We
abandoned Zbb for unrelated reasons — on RV32 a 64-bit rotate needs a funnel
shift, which Zbb does not provide, so it was worth only ~1.03–1.07× on Keccak —
but the decode divergence is worth reporting on its own.

### Suggested fix

Either is fine, and they are independent:

1. **Make the two models agree.** Pick one predicate. `funct7[5]`
   (equivalently `(funct7 >> 5) & 1`) matches the RTL and still handles the RV64
   `shamt[5]` case the SimX comment is about, since `0x21` also has bit 5 set.
2. **Raise illegal-instruction on unallocated encodings.** Better, and it is
   what turns "we do not implement Zbb" from a silent miscompute into a
   diagnosable trap — but it needs an exception path that does not exist in this
   decoder today, so it is the larger change.

At minimum the two lines should not disagree, because a project that validates
SimX against RTL cannot validate a case where both are wrong in different
directions.

---

## Reporting checklist

- [ ] Confirm against upstream `vortexgpgpu/vortex` `master`, not this fork
- [ ] File as an issue with the two file:line citations and the encoding table
- [ ] Mention that no current build emits the encoding, so this is latent
