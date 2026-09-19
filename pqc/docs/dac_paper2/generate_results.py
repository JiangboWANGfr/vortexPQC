#!/usr/bin/env python3
"""Derive TeX numbers and PGFPlots tables from archived measurements."""

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


def table(name, header, records):
    lines = [" ".join(header)]
    lines += [" ".join(f"{v:.6f}" if isinstance(v, float) else str(v) for v in r)
              for r in records]
    (OUT / name).write_text("\n".join(lines) + "\n")


kem = rows("keccak_ntt_unified_xrt.csv")
assert len(kem) == 12 and all(r["result"] == "PASS" for r in kem)
assert len({r["xrtsim_sha256"] for r in kem}) == 1
backends = ["sg1_serial", "pqrv_asm", "sg25_sw", "sg25_stages", "kround25", "pointer_keccakf"]
cycles = {(b, m): int(one(kem, backend=b, requests=m)["device_cycles"])
          for b in backends for m in (1, 8)}
table("kem.dat", ["index", "mone", "meight"],
      [(i, cycles[b, 1] / 1e6, cycles[b, 8] / 1e6) for i, b in enumerate(backends)])

shared = rows("shared_ntt_xrt.csv")
assert len(shared) == 8 and all(r["result"] == "PASS" for r in shared)
assert len({r["runtime_sha256"] for r in shared if r["driver"] == "xrt"}) == 1
shared_reduction = []
parity_gaps = []
for scheme in ["K", "D"]:
    sw = one(shared, scheme=scheme, ntt="software", driver="xrt")
    hw = one(shared, scheme=scheme, ntt="shared", driver="xrt")
    assert sw["keccak"] == hw["keccak"] == "pointer"
    assert sw["requests"] == hw["requests"] == "1"
    assert sw["first_input"] == hw["first_input"] == "0"
    for key in ["keypair_cycles", "operation_cycles", "final_cycles", "total_cycles"]:
        shared_reduction.append(100 * (1 - int(hw[key]) / int(sw[key])))
    for ntt in ["software", "shared"]:
        rtl = one(shared, scheme=scheme, ntt=ntt, driver="xrt")
        model = one(shared, scheme=scheme, ntt=ntt, driver="simx")
        assert rtl["kernel_vxbin_sha256"] == model["kernel_vxbin_sha256"]
        assert rtl["retired_instructions"] == model["retired_instructions"]
        parity_gaps.append(100 * abs(int(model["launch_cycles"]) - int(rtl["launch_cycles"]))
                           / int(rtl["launch_cycles"]))
assert max(parity_gaps) < 5
table("shared_ntt.dat", ["index", "reduction"], list(enumerate(shared_reduction)))

pointwise = rows("mldsa_pointwise.csv")
assert all(r["result"] == "PASS" for r in pointwise)
pointwise_control = one(pointwise, arm="c", driver="xrt", requests=1, request_id=0)
pointwise_mapped = one(pointwise, arm="both", driver="xrt", requests=1, request_id=0)
assert pointwise_control["runtime_sha256"] == pointwise_mapped["runtime_sha256"]
for arm in ("c", "both"):
    model = one(pointwise, arm=arm, driver="simx", requests=1, request_id=0)
    rtl = one(pointwise, arm=arm, driver="xrt", requests=1, request_id=0)
    assert model["kernel_sha256"] == rtl["kernel_sha256"]
    assert abs(int(model["total_cycles"]) - int(rtl["total_cycles"])) / int(rtl["total_cycles"]) < 0.05
pointwise_m8 = rows("mldsa_pointwise_m8.csv")
assert len(pointwise_m8) == 32 and all(r["result"] == "PASS" for r in pointwise_m8)
pointwise_m8_gaps = []
for arm in ("c", "both"):
    for req in range(8):
        model = one(pointwise_m8, arm=arm, driver="simx", requests=8, request_id=req)
        rtl = one(pointwise_m8, arm=arm, driver="xrt", requests=8, request_id=req)
        assert model["input_id"] == rtl["input_id"] == str(req + 1)
        assert model["kernel_sha256"] == rtl["kernel_sha256"]
        assert model["instructions"] == rtl["instructions"]
        for key in ("device_cycles", "makespan_cycles"):
            pointwise_m8_gaps.append(100 * abs(int(model[key]) / int(rtl[key]) - 1))
assert max(pointwise_m8_gaps) < 5
assert len({r["runtime_sha256"] for r in pointwise_m8 if r["driver"] == "xrt"}) == 1
assert one(pointwise_m8, arm="both", driver="xrt", request_id=0)["kernel_sha256"] == pointwise_mapped["kernel_sha256"]
pointwise_control_m8 = one(pointwise_m8, arm="c", driver="xrt", requests=8, request_id=0)
pointwise_mapped_m8 = one(pointwise_m8, arm="both", driver="xrt", requests=8, request_id=0)
pointwise_numbers = {
    "DsaPointwiseXrtSaved": 100 * (1 - int(pointwise_mapped["total_cycles"]) /
                                      int(pointwise_control["total_cycles"])),
    "DsaPointwiseEightSaved": 100 * (1 - int(pointwise_mapped_m8["makespan_cycles"]) /
                                        int(pointwise_control_m8["makespan_cycles"])),
    "DsaPointwiseEightParity": max(pointwise_m8_gaps),
}
table("pointwise.dat", ["index", "reduction"],
      [(9, pointwise_numbers["DsaPointwiseXrtSaved"]),
       (10, pointwise_numbers["DsaPointwiseEightSaved"])])

final_profile = rows("final_phase_profile.csv")
final_phase_numbers = {}
phase_lines = []
for scheme, label in (("mlkem", "KEM"), ("mldsa", "DSA")):
    parts = [r for r in final_profile if r["scheme"] == scheme and r["driver"] == "xrt"]
    total = int(parts[0]["request_total"])
    keys = ("permute", "absorb", "squeeze", "ntt", "intt", "pointwise",
            "mulcache", "basemul", "reduce", "residual")
    sums = {key: sum(int(r[key]) for r in parts) for key in keys}
    assert sum(sums.values()) == sum(int(r["phase_total"]) for r in parts) == total
    for rtl in parts:
        model = one(final_profile, scheme=scheme, driver="simx", phase=rtl["phase"])
        assert model["binary_sha256"] == rtl["binary_sha256"]
        assert model["instrs"] == rtl["instrs"]
        assert abs(int(model["request_total"]) / total - 1) < 0.05
    permute = 100 * sums["permute"] / total
    ntt = 100 * (sums["ntt"] + sums["intt"]) / total
    products = 100 * (sums["pointwise"] + sums["mulcache"] + sums["basemul"]) / total
    sponge = 100 * (sums["absorb"] + sums["squeeze"]) / total
    rest = 100 * (sums["residual"] + sums["reduce"]) / total
    sponge_text = f"{sponge:.3f}" if scheme == "mlkem" else "--"
    phase_lines.append(f"{label} & {permute:.3f} & {ntt:.3f} & {products:.3f} & {sponge_text} & {rest:.3f} " + r"\\")
    overhead = 100 * (total / int(parts[0]["headline_total"]) - 1)
    assert abs(overhead - float(parts[0]["instrumentation_overhead_pct"])) < 0.001
    final_phase_numbers[label.title() + "FinalProbePct"] = overhead
(OUT / "final_phase_rows.tex").write_text("\n".join(phase_lines) + "\n")

controls = rows("keccak_w32_controls.csv")
fusion = {}
for arm in ["stage_loop", "stage_unrolled", "kround"]:
    for m in (1, 8):
        r = one(controls, arm=arm, driver="xrt", batch=m, states_per_warp=1,
                permutations_per_state=64)
        assert r["result"] == "PASS"
        fusion[arm, m] = int(r["span_cycles"]) / (64 * m)
table("fusion.dat", ["warps", "loop", "expanded", "round"],
      [(m, fusion["stage_loop", m], fusion["stage_unrolled", m], fusion["kround", m])
       for m in (1, 8)])

stages = rows("keccak_sg25_stages.csv")
stage_cost = {}
for arm in ["000", "100", "010", "001", "110", "101", "011", "111"]:
    for m in (1, 8):
        low = one(stages, driver="rtlsim", arm=arm, batch_warps=m, permutations_per_state=4)
        high = one(stages, driver="rtlsim", arm=arm, batch_warps=m, permutations_per_state=8)
        assert low["elf_sha256"] == high["elf_sha256"]
        assert low["passed"] == high["passed"] == "1"
        stage_cost[arm, m] = (int(high["span_cycles"]) - int(low["span_cycles"])) / (4 * m)
table("stage_ablation.dat", ["index", "one", "eight"],
      [(i, stage_cost["000", 1] / stage_cost[arm, 1], stage_cost["000", 8] / stage_cost[arm, 8])
       for i, arm in enumerate(["000", "100", "010", "001", "110", "101", "011", "111"])])

profile = rows("mlkem_phase_profile.csv")
phase_pct = {}
for backend in ["stage", "whole_round", "pointer"]:
    r = one(profile, measurement="phase", backend=backend, driver="xrt", requests=1)
    parts = [int(r["keccak_permute_cycles"]), int(r["keccak_absorb_cycles"]),
             int(r["keccak_squeeze_cycles"]), int(r["ntt_cycles"]) + int(r["intt_cycles"]),
             sum(int(r[k]) for k in ["mulcache_cycles", "basemul_cycles", "reduce_cycles"]),
             int(r["other_cycles"])]
    total = int(r["interval_or_makespan_cycles"])
    assert sum(parts) == total and r["kat"] == "PASS"
    if backend in ("stage", "whole_round"):
        name = "Stage" if backend == "stage" else "Round"
        phase_pct[name + "PermPct"] = 100 * parts[0] / total
        phase_pct[name + "SpongePct"] = 100 * (parts[1] + parts[2]) / total

ppa = rows("rv32im_rv64im_keccak_ppa.csv")
shared_ppa = rows("shared_ntt_ppa.csv")
assert len(shared_ppa) == 2
for r in shared_ppa:
    assert r["xlen"] == "32" and r["warps"] == "8" and r["threads"] == "32"
    assert r["target_mhz"] == "250" and r["f_enabled"] == r["d_enabled"] == "0"
    assert r["routing_errors"] == "0" and r["status"] == "met"

bank_nttbf = rows("ntt_multiplier_bank_nttbf.csv")
bank_ppa = rows("ntt_multiplier_bank_ppa.csv")
bank_perf = rows("ntt_multiplier_bank_performance.csv")
bank_order = [16, 8, 4, 2, 1]
assert [int(r["multipliers"]) for r in bank_nttbf] == bank_order
assert [int(r["multipliers"]) for r in bank_ppa] == bank_order
assert len(bank_perf) == 40 and all(r["result"] == "PASS" for r in bank_perf)
assert all(r["instructions"] == "59534" and r["result"] == "PASS"
           for r in bank_nttbf)
assert all(r["status"] == "met" and r["routing_errors"] == "0"
           and r["target_mhz"] == "250" for r in bank_ppa)
nttbf_base = int(bank_nttbf[0]["rtl_cycles"])
ppa_base = bank_ppa[0]
table("ntt_bank.dat",
      ["bank", "cycles", "nttlut", "nttff", "nttdsp"],
      [(bank,
        100 * int(one(bank_nttbf, multipliers=bank)["rtl_cycles"]) / nttbf_base,
        100 * int(one(bank_ppa, multipliers=bank)["ntt_luts"]) / int(ppa_base["ntt_luts"]),
        100 * int(one(bank_ppa, multipliers=bank)["ntt_ffs"]) / int(ppa_base["ntt_ffs"]),
        100 * int(one(bank_ppa, multipliers=bank)["ntt_dsps"]) / int(ppa_base["ntt_dsps"]))
       for bank in bank_order])
bank_two_ppa = one(bank_ppa, multipliers=2)
bank_two_perf = [r for r in bank_perf
                 if r["multipliers"] == "2" and r["driver"] == "xrt"]
assert len(bank_two_perf) == 4
bank_numbers = {
    "BankEightNttPenalty": float(one(bank_nttbf, multipliers=8)["change_vs16_pct"]),
    "BankFourNttPenalty": float(one(bank_nttbf, multipliers=4)["change_vs16_pct"]),
    "BankTwoNttPenalty": float(one(bank_nttbf, multipliers=2)["change_vs16_pct"]),
    "BankOneNttPenalty": float(one(bank_nttbf, multipliers=1)["change_vs16_pct"]),
    "BankTwoNttLutSaved": 100 * (1 - int(bank_two_ppa["ntt_luts"]) /
                                      int(ppa_base["ntt_luts"])),
    "BankTwoNttFfSaved": 100 * (1 - int(bank_two_ppa["ntt_ffs"]) /
                                     int(ppa_base["ntt_ffs"])),
    "BankTwoNttDspSaved": 100 * (1 - int(bank_two_ppa["ntt_dsps"]) /
                                      int(ppa_base["ntt_dsps"])),
    "BankTwoCoreLutSaved": 100 * (1 - int(bank_two_ppa["total_luts"]) /
                                       int(ppa_base["total_luts"])),
    "BankTwoCoreFfSaved": 100 * (1 - int(bank_two_ppa["ffs"]) /
                                      int(ppa_base["ffs"])),
    "BankTwoCoreDspSaved": 100 * (1 - int(bank_two_ppa["dsps"]) /
                                       int(ppa_base["dsps"])),
    "BankTwoEndToEndMax": max(float(r["change_vs16_pct"]) for r in bank_two_perf),
    "BankParityMax": max(float(r["model_gap_pct"]) for r in bank_nttbf),
}
ppa_lines = []
for variant, label in [("ntt_only", "K-only NTT"), ("ntt_stage", "K + Stage"),
                       ("ntt_kround", "K + Round")]:
    r = one(ppa, xlen=32, variant=variant)
    ppa_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & {r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
for variant, label in [("shared_ntt", "Shared K/D"),
                       ("shared_ntt_stage", "Shared + Stage")]:
    r = one(shared_ppa, variant=variant)
    ppa_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & {r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
(OUT / "ppa_rows.tex").write_text("\n".join(ppa_lines) + "\n")
cost_lines = []
for variant, label in [("ntt_only", "K-only base"),
                       ("ntt_stage", "+ Stage"),
                       ("ntt_kround", "+ Round")]:
    r = one(ppa, xlen=32, variant=variant)
    cost_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & "
                      f"{r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
cost_lines.append(r"\midrule")
for multipliers, label in [(16, "K/D M16"), (2, "K/D M2")]:
    r = one(bank_ppa, multipliers=multipliers)
    cost_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & "
                      f"{r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
(OUT / "cost_rows.tex").write_text("\n".join(cost_lines) + "\n")
base = one(ppa, xlen=32, variant="ntt_only")
stage = one(ppa, xlen=32, variant="ntt_stage")
round_unit = one(ppa, xlen=32, variant="ntt_kround")
shared_base = one(shared_ppa, variant="shared_ntt")
shared_stage = one(shared_ppa, variant="shared_ntt_stage")
kem_controls = rows("mlkem_w32_controls.csv")
numbers = {
    "StageVsShuffleOne": cycles["sg25_sw", 1] / cycles["sg25_stages", 1],
    "StageVsShuffleEight": cycles["sg25_sw", 8] / cycles["sg25_stages", 8],
    "RoundVsShuffleOne": cycles["sg25_sw", 1] / cycles["kround25", 1],
    "RoundVsShuffleEight": cycles["sg25_sw", 8] / cycles["kround25", 8],
    "RoundVsPqrvOne": cycles["pqrv_asm", 1] / cycles["kround25", 1],
    "RoundVsPqrvEight": cycles["pqrv_asm", 8] / cycles["kround25", 8],
    "RoundVsPointerOne": cycles["pointer_keccakf", 1] / cycles["kround25", 1],
    "PointerVsRoundEight": cycles["kround25", 8] / cycles["pointer_keccakf", 8],
    "StageLutPct": 100 * (int(stage["total_luts"]) / int(base["total_luts"]) - 1),
    "RoundLutPct": 100 * (int(round_unit["total_luts"]) / int(base["total_luts"]) - 1),
    "StageFfPct": 100 * (int(stage["ffs"]) / int(base["ffs"]) - 1),
    "RoundFfPct": 100 * (int(round_unit["ffs"]) / int(base["ffs"]) - 1),
    "SharedLutPct": 100 * (int(shared_base["total_luts"]) / int(base["total_luts"]) - 1),
    "SharedFfPct": 100 * (int(shared_base["ffs"]) / int(base["ffs"]) - 1),
    "SharedStageLutPct": 100 * (int(shared_stage["total_luts"]) / int(shared_base["total_luts"]) - 1),
    "SharedStageFfPct": 100 * (int(shared_stage["ffs"]) / int(shared_base["ffs"]) - 1),
    "AllStageOne": stage_cost["000", 1] / stage_cost["111", 1],
    "AllStageEight": stage_cost["000", 8] / stage_cost["111", 8],
    "FusionOne": fusion["stage_unrolled", 1] / fusion["kround", 1],
    "FusionEight": fusion["stage_unrolled", 8] / fusion["kround", 8],
}
numbers.update(phase_pct)
numbers.update(pointwise_numbers)
numbers.update(final_phase_numbers)
numbers.update(bank_numbers)
numbers.update({
    "KemNttSaved": shared_reduction[3],
    "DsaNttSaved": shared_reduction[7],
    "SharedParityMax": max(parity_gaps),
})
for m, name in [(1, "One"), (8, "Eight")]:
    s = one(kem_controls, arm="stage_unrolled", driver="simx", batch=m)
    r = one(kem_controls, arm="kround", driver="simx", batch=m)
    numbers["FusionKem" + name] = int(s["device_cycles"]) / int(r["device_cycles"])
table("fusion_gain.dat", ["index", "perm", "kem"],
      [(i, numbers["Fusion" + name], numbers["FusionKem" + name])
       for i, name in enumerate(["One", "Eight"])])
(OUT / "numbers.tex").write_text("\n".join(f"\\newcommand{{\\{k}}}{{{v:.3f}}}" for k, v in numbers.items()) + "\n")
(OUT / "source_manifest.json").write_text(json.dumps({
    "mlkem_native_commit": "1d7b486c4db3bbc620b009d05e4f69eca9e68d22",
    "measurements": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(DATA.glob("*.csv"))},
    "derived_numbers": numbers,
}, indent=2) + "\n")
