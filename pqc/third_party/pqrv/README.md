# PQRV — vendored RV32 Keccak-f1600 assembly

One file, copied verbatim, used as the *optimized software* column of this
project's Keccak baseline. Nothing here is modified; if it ever needs to be,
fork it into `tests/pqc/` instead so this directory stays a faithful copy.

## Provenance

| | |
|---|---|
| Upstream | https://github.com/Ji-Peng/PQRV |
| File | `sha3/fips202_rv32im.S` |
| Fetched | 2026-09-06 from `raw.githubusercontent.com/Ji-Peng/PQRV/main/sha3/fips202_rv32im.S` |
| main @ | `005d55dcafa95cefd4d2463c06d3d7591a258113` |
| sha256 | `e6e7e67a48760785891102d046d5206a55ee9dca558c8a44641860ceeb59ca42` |
| Lines | 707 |
| Licence | MIT, Copyright (c) 2024 Jipeng Zhang — see `LICENSE` |

The upstream `.S` carries no per-file licence header, which is why `LICENSE`
is vendored beside it. Re-verify with:

```sh
curl -sL https://raw.githubusercontent.com/Ji-Peng/PQRV/main/sha3/fips202_rv32im.S \
  | sha256sum   # expect e6e7e67a48760785891102d046d5206a55ee9dca558c8a44641860ceeb59ca42
```

## Paper

Jipeng Zhang, Yuxing Yan, Junhao Huang, Çetin Kaya Koç, "Optimized Software
Implementation of Keccak, Kyber, and Dilithium on RV{32,64}IM{B}{V}",
IACR TCHES 2025(1):632-655. ePrint 2024/1515.

## Why it is here

The project's Keccak baseline was mlkem-native's reference C at
-march=rv32imaf: 151,872 cycles per permutation on one Vortex lane, and Keccak
is 68% of ML-KEM-768, so that single number is effectively the denominator of
every speedup the PQC extension will claim. A reviewer who believes the
baseline is unoptimized discounts the whole result, and the literature shows
baseline choice is worth 2.5x-5.8x. Rather than argue about it, this is the
fastest published RV32 Keccak assembly, dropped in through mlkem-native's own
native-backend hook so the submodule stays pristine, and measured.

## Entry point

```
.globl KeccakF1600_StatePermute_RV32ASM   // void f(uint64_t state[25])
```

Plain RV32IM: no B-extension instructions (`grep` finds none), lane-complementing
rather than bit-interleaved, so the state layout is mlkem-native's own — a true
drop-in. It uses `gp`/`tp` as scratch with save/restore, and a 112-byte frame.
