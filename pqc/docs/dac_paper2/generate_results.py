#!/usr/bin/env python3
"""Derive every number, table row, and pgfplots data file from the archived
measurement snapshots in data/.  Nothing in main.tex is typed by hand."""

import csv
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
OUT = HERE / "assets"
OUT.mkdir(exist_ok=True)


def rows(name):
    with (DATA / name).open() as stream:
        return list(csv.DictReader(line for line in stream if not line.startswith("#")))


def one(records, **filters):
    found = [r for r in records if all(r[k] == str(v) for k, v in filters.items())]
    assert len(found) == 1, (filters, len(found))
    return found[0]


def write(name, text):
    (OUT / name).write_text(text)


numbers = {}
formatted = {}


def macro(name, value, fmt="{:.3f}"):
    numbers[name] = value
    formatted[name] = fmt.format(value)
    return value


# ---------------------------------------------------------------- complete KEM
kem = rows("keccak_ntt_unified_xrt.csv")
assert len(kem) == 12 and all(r["result"] == "PASS" for r in kem)
assert len({r["xrtsim_sha256"] for r in kem}) == 1
backends = ["sg1_serial", "pqrv_asm", "sg25_sw", "sg25_stages", "kround25", "pointer_keccakf"]
labels = ["Scalar C", "PQRV asm", "Shuffle", "Stage", "Round", "Pointer"]
cyc = {(b, m): int(one(kem, backend=b, requests=m)["device_cycles"]) for b in backends for m in (1, 8)}
write("kem_xrt.dat", "backend m1 m8\n" + "".join(
    f"{{{lab}}} {cyc[b, 1] / 1e6:.3f} {cyc[b, 8] / 1e6:.3f}\n" for b, lab in zip(backends, labels)))

for m, tag in ((1, "One"), (8, "Eight")):
    macro(f"StageVsShuffle{tag}", cyc["sg25_sw", m] / cyc["sg25_stages", m])
    macro(f"RoundVsShuffle{tag}", cyc["sg25_sw", m] / cyc["kround25", m])
    macro(f"StageVsScalar{tag}", cyc["sg1_serial", m] / cyc["sg25_stages", m])
    macro(f"RoundVsScalar{tag}", cyc["sg1_serial", m] / cyc["kround25", m])
    macro(f"RoundVsPqrv{tag}", cyc["pqrv_asm", m] / cyc["kround25", m])
    macro(f"ShuffleVsPqrv{tag}", cyc["pqrv_asm", m] / cyc["sg25_sw", m])
    macro(f"PointerVsScalar{tag}", cyc["sg1_serial", m] / cyc["pointer_keccakf", m])
macro("RoundVsPointerOne", cyc["pointer_keccakf", 1] / cyc["kround25", 1])
macro("StageVsPointerOne", cyc["pointer_keccakf", 1] / cyc["sg25_stages", 1])
macro("PointerVsRoundEight", cyc["kround25", 8] / cyc["pointer_keccakf", 8])
macro("PointerVsStageEight", cyc["sg25_stages", 8] / cyc["pointer_keccakf", 8])
# throughput gain of eight concurrent requests over one request, per backend
for b, name in zip(backends, ["Scalar", "Pqrv", "Shuffle", "Stage", "Round", "Pointer"]):
    macro(f"Batch{name}", 8 * cyc[b, 1] / cyc[b, 8], "{:.2f}")
for b, name in zip(backends, ["Scalar", "Pqrv", "Shuffle", "Stage", "Round", "Pointer"]):
    macro(f"Kem{name}One", cyc[b, 1] / 1e6, "{:.2f}")
    macro(f"Kem{name}Eight", cyc[b, 8] / 1e6, "{:.2f}")

# ---------------------------------------------------------------- fusion control
ctrl = rows("keccak_w32_controls.csv")
perm = {}
for arm in ("stage_loop", "stage_unrolled", "kround"):
    for m in (1, 8):
        r = one(ctrl, arm=arm, driver="xrt", batch=m, states_per_warp=1, permutations_per_state=64)
        perm[arm, m] = int(r["span_cycles"]) / (64 * m)
macro("PermStageLoopOne", perm["stage_loop", 1], "{:.0f}")
macro("PermStageLoopEight", perm["stage_loop", 8], "{:.0f}")
macro("PermStageExpOne", perm["stage_unrolled", 1], "{:.0f}")
macro("PermStageExpEight", perm["stage_unrolled", 8], "{:.0f}")
macro("PermRoundOne", perm["kround", 1], "{:.0f}")
macro("PermRoundEight", perm["kround", 8], "{:.0f}")
macro("FusionPrimOne", perm["stage_unrolled", 1] / perm["kround", 1])
macro("FusionPrimEight", perm["stage_unrolled", 8] / perm["kround", 8])

w32 = rows("mlkem_w32_controls.csv")
app = {}
for arm in ("stage_unrolled", "kround"):
    for m in (1, 8):
        app[arm, m] = int(one(w32, arm=arm, driver="simx", batch=m)["device_cycles"])
macro("FusionAppOne", app["stage_unrolled", 1] / app["kround", 1])
macro("FusionAppEight", app["stage_unrolled", 8] / app["kround", 8])
macro("FusionAppOnePct", 100 * (app["stage_unrolled", 1] / app["kround", 1] - 1), "{:.1f}")
macro("FusionAppEightPct", 100 * (app["stage_unrolled", 8] / app["kround", 8] - 1), "{:.1f}")
write("fusion.dat", "scope one eight\n"
      f"{{Permutation}} {numbers['FusionPrimOne']:.3f} {numbers['FusionPrimEight']:.3f}\n"
      f"{{Complete KEM}} {numbers['FusionAppOne']:.3f} {numbers['FusionAppEight']:.3f}\n")

# ---------------------------------------------------------------- phase profile
prof = rows("mlkem_phase_profile.csv")
phase_rows = []
for name, lab in (("stage", "Stage"), ("whole_round", "Round"), ("pointer", "Pointer")):
    r = one(prof, measurement="phase", backend=name, driver="xrt", requests=1)
    total = int(r["interval_or_makespan_cycles"])
    permute = int(r["keccak_permute_cycles"])
    sponge = int(r["keccak_absorb_cycles"]) + int(r["keccak_squeeze_cycles"])
    ntt = int(r["ntt_cycles"]) + int(r["intt_cycles"])
    poly = sum(int(r[k]) for k in ("mulcache_cycles", "basemul_cycles", "reduce_cycles"))
    other = int(r["other_cycles"])
    assert permute + sponge + ntt + poly + other == total
    pct = [100 * v / total for v in (permute, sponge, ntt, poly, other)]
    phase_rows.append((lab, pct))
    macro(f"Perm{lab}Pct", pct[0], "{:.1f}")
    macro(f"Sponge{lab}Pct", pct[1], "{:.1f}")
    macro(f"Ntt{lab}Pct", pct[2], "{:.1f}")
    macro(f"Poly{lab}Pct", pct[3], "{:.1f}")
    macro(f"Other{lab}Pct", pct[4], "{:.1f}")
write("profile.dat", "backend permute sponge ntt poly other\n" + "".join(
    f"{{{lab}}} " + " ".join(f"{v:.2f}" for v in pct) + "\n" for lab, pct in phase_rows))
overheads = [float(one(prof, measurement="phase", backend=b, driver="simx", requests=1)["instrumentation_overhead_pct"])
             for b in ("stage", "whole_round", "pointer")]
macro("ProbeMinPct", min(overheads), "{:.1f}")
macro("ProbeMaxPct", max(overheads), "{:.1f}")

# ---------------------------------------------------------------- NTT transform + KEM
sg2 = rows("nttbf_sg2_validation.csv")
ntt_variants = [("reg32+SHFL+C", "Shuffle+C"), ("reg32+SHFL+NTTMUL.K", "Shuffle+MUL"),
                ("smem32+NTTMUL.K", "SharedMem+MUL"), ("reg32+SG2+NTTMUL.K", "Pair+MUL")]
direct = {}
for var, lab in ntt_variants:
    for m in (1, 8):
        for d in ("forward", "inverse"):
            direct[lab, m, d] = int(one(sg2, measurement="direct_transform_ablation", variant=var,
                                        driver="simx", requests=m, direction=d)["cycles"])
write("ntt_direct.dat", "variant fwd1 inv1 fwd8 inv8\n" + "".join(
    f"{{{lab}}} {direct[lab, 1, 'forward'] / 1e3:.2f} {direct[lab, 1, 'inverse'] / 1e3:.2f} "
    f"{direct[lab, 8, 'forward'] / 1e3:.2f} {direct[lab, 8, 'inverse'] / 1e3:.2f}\n"
    for _, lab in ntt_variants))
macro("NttPairVsShuffleFwdOne", direct["Shuffle+MUL", 1, "forward"] / direct["Pair+MUL", 1, "forward"], "{:.2f}")
macro("NttPairVsShuffleInvOne", direct["Shuffle+MUL", 1, "inverse"] / direct["Pair+MUL", 1, "inverse"], "{:.2f}")
macro("NttPairVsSmemFwdOne", direct["SharedMem+MUL", 1, "forward"] / direct["Pair+MUL", 1, "forward"], "{:.2f}")
macro("NttPairVsSmemFwdEight", direct["SharedMem+MUL", 8, "forward"] / direct["Pair+MUL", 8, "forward"], "{:.2f}")
macro("NttMulVsCFwdOne", direct["Shuffle+C", 1, "forward"] / direct["Shuffle+MUL", 1, "forward"], "{:.2f}")
macro("NttPairVsCFwdOne", direct["Shuffle+C", 1, "forward"] / direct["Pair+MUL", 1, "forward"], "{:.2f}")

e2e = {}
for var, lab in ntt_variants[1:]:
    for m in (1, 8):
        e2e[lab, m] = int(one(sg2, measurement="mlkem768_e2e_ablation", variant="keccak_pe+" + var,
                              driver="simx", requests=m)["cycles"])
macro("NttKemPairVsShuffleOnePct", 100 * (1 - e2e["Pair+MUL", 1] / e2e["Shuffle+MUL", 1]), "{:.1f}")
macro("NttKemPairVsShuffleEightPct", 100 * (1 - e2e["Pair+MUL", 8] / e2e["Shuffle+MUL", 8]), "{:.1f}")
macro("NttKemPairVsSmemOnePct", 100 * (1 - e2e["Pair+MUL", 1] / e2e["SharedMem+MUL", 1]), "{:.1f}")
macro("NttKemPairVsSmemEightPct", 100 * (1 - e2e["Pair+MUL", 8] / e2e["SharedMem+MUL", 8]), "{:.1f}")

half = rows("ntt_halfbank_validation.csv")
gaps = []
for drv in ("simx", "rtlsim", "xrt"):
    for m in (1, 8):
        full = int(one(half, measurement="kem", variant="reg32_sg2_nttmul", bank_multipliers=32, driver=drv, requests=m)["measured_cycles"])
        halfb = int(one(half, measurement="kem", variant="reg32_sg2_nttmul", bank_multipliers=16, driver=drv, requests=m)["measured_cycles"])
        gaps.append(abs(halfb - full) / full * 100)
macro("HalfBankMaxCyclePct", max(gaps), "{:.3f}")
ppa_ntt = rows("ntt_v80_ppa.csv")
fb = one(ppa_ntt, variant="keccak_pe_static_agu_nttmul_sg2_reducepipe")
hb = one(ppa_ntt, variant="keccak_pe_static_agu_nttmul_sg2_halfbank")
macro("HalfBankLutSave", int(fb["total_luts"]) - int(hb["total_luts"]), "{:,.0f}")
macro("HalfBankLutSavePct", 100 * (1 - int(hb["total_luts"]) / int(fb["total_luts"])), "{:.1f}")
macro("HalfBankFfSave", int(fb["ffs"]) - int(hb["ffs"]), "{:,.0f}")
macro("HalfBankDspSave", int(fb["dsps"]) - int(hb["dsps"]), "{:.0f}")

# ---------------------------------------------------------------- PPA + XLEN
ppa = rows("rv32im_rv64im_keccak_ppa.csv")
lines = []
for xlen in (32, 64):
    for variant, lab in (("ntt_only", "NTT only"), ("ntt_stage", "+Stage"), ("ntt_kround", "+Round")):
        r = one(ppa, xlen=xlen, variant=variant)
        lines.append(f"RV{xlen} & {lab} & {int(r['total_luts']):,} & {int(r['ffs']):,} & "
                     f"{int(r['keccak_hier_luts']):,} & {int(r['keccak_hier_ffs']):,} & {float(r['wns_ns']):+.3f} \\\\")
write("ppa_rows.tex", "\n".join(lines) + "\n")
for xlen, tag in ((32, "ThirtyTwo"), (64, "SixtyFour")):
    base = one(ppa, xlen=xlen, variant="ntt_only")
    for variant, lab in (("ntt_stage", "Stage"), ("ntt_kround", "Round")):
        r = one(ppa, xlen=xlen, variant=variant)
        macro(f"{lab}Lut{tag}Pct", 100 * (int(r["total_luts"]) / int(base["total_luts"]) - 1))
        macro(f"{lab}Ff{tag}Pct", 100 * (int(r["ffs"]) / int(base["ffs"]) - 1))
        macro(f"{lab}Wns{tag}", float(r["wns_ns"]))
b32, b64 = one(ppa, xlen=32, variant="ntt_only"), one(ppa, xlen=64, variant="ntt_only")
macro("RvSixtyFourLutRatio", int(b64["total_luts"]) / int(b32["total_luts"]), "{:.2f}")
macro("NttHierLut", int(b32["ntt_hier_luts"]), "{:,.0f}")
macro("NttHierFf", int(b32["ntt_hier_ffs"]), "{:,.0f}")
macro("NttHierDsp", int(b32["ntt_hier_dsps"]), "{:.0f}")
for variant, lab in (("ntt_stage", "Stage"), ("ntt_kround", "Round")):
    r = one(ppa, xlen=32, variant=variant)
    macro(f"{lab}HierLut", int(r["keccak_hier_luts"]), "{:,.0f}")
    macro(f"{lab}HierFf", int(r["keccak_hier_ffs"]), "{:,.0f}")

xlen = rows("rv32im_rv64im_keccak.csv")
m8 = rows("rv32im_rv64im_keccak_xrt_m8.csv")
for b, lab in (("stage", "Stage"), ("whole_round", "Round")):
    i32 = int(one(xlen, xlen=32, driver="simx", requests=8, keccak_backend=b)["retired_instructions"])
    i64 = int(one(xlen, xlen=64, driver="simx", requests=8, keccak_backend=b)["retired_instructions"])
    macro(f"RvSixtyFour{lab}InstrPct", 100 * (1 - i64 / i32), "{:.1f}")
    c32 = int(one(xlen, xlen=32, driver="simx", requests=1, keccak_backend=b)["device_cycles"])
    c64 = int(one(xlen, xlen=64, driver="simx", requests=1, keccak_backend=b)["device_cycles"])
    macro(f"RvSixtyFour{lab}MOnePct", 100 * (1 - c64 / c32), "{:.1f}")
    x32 = int(one(m8, xlen=32, backend=b)["device_cycles"])
    x64 = int(one(m8, xlen=64, backend=b)["device_cycles"])
    macro(f"RvSixtyFour{lab}MEightPct", 100 * (x64 / x32 - 1), "{:+.1f}")


# ---------------------------------------------------------------- scalar profile (motivation)
abl = rows("ablation_mlkem.csv")
macro("ScalarKeccakSharePct", 100 * float(one(abl, variant="ablate_keccak")["fraction"]), "{:.0f}")
macro("ScalarNttSharePct", 100 * float(one(abl, variant="ablate_ntt")["fraction"]), "{:.0f}")

# ---------------------------------------------------------------- matched permutation microbenchmark
mx = rows("keccak_w32_matched_xrt.csv")
for b, lab in (("sg1", "ScalarC"), ("asm", "Pqrv"), ("sg5_barrier", "SgFive"), ("sg25_sw", "Shuffle"), ("stage", "Stage"), ("kround", "Round")):
    macro(f"MicroPerm{lab}", float(one(mx, backend=b)["cycles_per_completed_permutation"]), "{:,.0f}")
macro("MicroShuffleVsPqrv", float(one(mx, backend="asm")["cycles_per_completed_permutation"]) / float(one(mx, backend="sg25_sw")["cycles_per_completed_permutation"]), "{:.1f}")
macro("MicroStageVsShuffle", float(one(mx, backend="sg25_sw")["cycles_per_completed_permutation"]) / float(one(mx, backend="stage")["cycles_per_completed_permutation"]), "{:.1f}")
macro("MicroRoundVsShuffle", float(one(mx, backend="sg25_sw")["cycles_per_completed_permutation"]) / float(one(mx, backend="kround")["cycles_per_completed_permutation"]), "{:.1f}")

# ---------------------------------------------------------------- matched backend area incl. pointer (F-enabled cores)
bp = rows("ntt_keccak_backend_ppa.csv")
base = one(bp, variant="ntt_only")
for variant, lab in (("ntt_stage", "Stage"), ("ntt_kround", "Round"), ("ntt_pointer", "Pointer")):
    r = one(bp, variant=variant)
    macro(f"Backend{lab}LutDelta", int(r["total_luts"]) - int(base["total_luts"]), "{:,.0f}")
    macro(f"Backend{lab}LutPct", 100 * (int(r["total_luts"]) / int(base["total_luts"]) - 1), "{:.2f}")
    macro(f"Backend{lab}FfDelta", int(r["ffs"]) - int(base["ffs"]), "{:,.0f}")
    macro(f"Backend{lab}FfPct", 100 * (int(r["ffs"]) / int(base["ffs"]) - 1), "{:.2f}")
    macro(f"Backend{lab}HierLut", int(r["keccak_hier_luts"]), "{:,.0f}")
    macro(f"Backend{lab}HierFf", int(r["keccak_hier_ffs"]), "{:,.0f}")
macro("PointerVsRoundLutDelta", (int(one(bp, variant="ntt_pointer")["total_luts"]) - int(base["total_luts"])) / (int(one(bp, variant="ntt_kround")["total_luts"]) - int(base["total_luts"])), "{:.1f}")

# ---------------------------------------------------------------- IPC at M8 (interpretation of the crossover)
for b, name in zip(backends, ["Scalar", "Pqrv", "Shuffle", "Stage", "Round", "Pointer"]):
    r8 = one(kem, backend=b, requests=8)
    macro(f"Ipc{name}Eight", int(r8["retired_instructions"]) / int(r8["device_cycles"]), "{:.2f}")
    r1 = one(kem, backend=b, requests=1)
    macro(f"Ipc{name}One", int(r1["retired_instructions"]) / int(r1["device_cycles"]), "{:.3f}")


# ---------------------------------------------------------------- design-point table (RV32IM area, common-runtime cycles)
for variant, lab in (("ntt_stage", "Stage"), ("ntt_kround", "Round")):
    r = one(ppa, xlen=32, variant=variant)
    macro(f"{lab}LutDeltaThirtyTwo", int(r["total_luts"]) - int(b32["total_luts"]), "{:,.0f}")
    macro(f"{lab}FfDeltaThirtyTwo", int(r["ffs"]) - int(b32["ffs"]), "{:,.0f}")
# permutation throughput per added LUT, round relative to stage (looped stage, as programmed in the KEM)
macro("RoundPerLutVsStage", (perm["stage_loop", 8] / perm["kround", 8]) *
      ((int(one(ppa, xlen=32, variant="ntt_stage")["total_luts"]) - int(b32["total_luts"])) /
       (int(one(ppa, xlen=32, variant="ntt_kround")["total_luts"]) - int(b32["total_luts"]))), "{:.1f}")
# instructions at M8 for the crossover discussion
for b, name in zip(backends, ["Scalar", "Pqrv", "Shuffle", "Stage", "Round", "Pointer"]):
    macro(f"Instr{name}Eight", int(one(kem, backend=b, requests=8)["retired_instructions"]) / 1e6, "{:.2f}")
    macro(f"Instr{name}One", int(one(kem, backend=b, requests=1)["retired_instructions"]) / 1e6, "{:.2f}")

# ---------------------------------------------------------------- model parity
gap = max(abs(int(one(w32, arm=a, driver="simx", batch=1)["device_cycles"])
              - int(one(w32, arm=a, driver="xrt", batch=1)["device_cycles"]))
          / int(one(w32, arm=a, driver="xrt", batch=1)["device_cycles"]) * 100
          for a in ("stage_unrolled", "kround", "stage_loop", "asm"))
macro("ModelGapPct", gap, "{:.2f}")

write("numbers.tex", "".join(f"\\newcommand{{\\{k}}}{{{v}}}\n" for k, v in formatted.items()))
write("source_manifest.json", json.dumps({
    "repository_source_commit": "084f79465",
    "mlkem_native_commit": "1d7b486c4db3bbc620b009d05e4f69eca9e68d22",
    "measurements": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(DATA.glob("*.csv"))},
    "derived_numbers": numbers,
}, indent=2) + "\n")
