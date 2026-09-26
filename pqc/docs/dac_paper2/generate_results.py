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

board_kem = json.loads((DATA / "mlkem768_board_200mhz.json").read_text())
board_dsa = json.loads((DATA / "mldsa65_board_200mhz.json").read_text())
board_resources = json.loads((DATA / "aved_resources_200mhz.json").read_text())
assert board_kem["configuration"]["board_clock_hz"] == 200_000_000
assert board_dsa["clock_hz_before"] == board_dsa["clock_hz_after"] == 200_000_000
assert board_resources["clock_mhz"] == 200 and board_resources["wns_ns"] >= 0
assert board_kem["image"]["sha256"] == board_dsa["vbin_sha256"] == board_resources["vbin_sha256"]
assert len(board_dsa["runs"]) == 50
for batch in (1, 8):
    for arm in "ABCD":
        assert board_kem["measurements"][f"M{batch}"][arm]["all_kat_pass"]
    for arm in "ABCDE":
        assert sorted(r["repeat"] for r in board_dsa["runs"]
                      if r["batch"] == batch and r["arm"] == arm) == [1, 2, 3, 4, 5]
board_numbers = {}
for batch, suffix in ((1, "One"), (8, "Eight")):
    kem = board_kem["measurements"][f"M{batch}"]
    dsa = board_dsa["medians"][str(batch)]
    board_numbers.update({
        "BoardKemStage" + suffix: kem["A"]["median_makespan_cycles"] /
                                   kem["B"]["median_makespan_cycles"],
        "BoardKemNtt" + suffix: kem["B"]["median_makespan_cycles"] /
                                 kem["D"]["median_makespan_cycles"],
        "BoardKem" + suffix: kem["A"]["median_makespan_cycles"] /
                              kem["D"]["median_makespan_cycles"],
        "BoardDsaStage" + suffix: dsa["A"] / dsa["B"],
        "BoardDsaNtt" + suffix: dsa["B"] / dsa["D"],
        "BoardDsaPointwise" + suffix: dsa["D"] / dsa["E"],
        "BoardDsaHardware" + suffix: dsa["A"] / dsa["D"],
        "BoardDsaFinal" + suffix: dsa["A"] / dsa["E"],
    })
table("board_kem.dat", ["index", "one", "eight"],
      [(i, board_kem["measurements"]["M1"]["A"]["median_makespan_cycles"] /
        board_kem["measurements"]["M1"][arm]["median_makespan_cycles"],
        board_kem["measurements"]["M8"]["A"]["median_makespan_cycles"] /
        board_kem["measurements"]["M8"][arm]["median_makespan_cycles"])
       for i, arm in enumerate("ABCD")])
table("board_dsa.dat", ["index", "one", "eight"],
      [(i, board_dsa["medians"]["1"]["A"] / board_dsa["medians"]["1"][arm],
        board_dsa["medians"]["8"]["A"] / board_dsa["medians"]["8"][arm])
       for i, arm in enumerate("ABCDE")])
board_numbers["BoardCoreLuts"] = board_resources["resources"]["vortex_core"]["luts"]
board_numbers["BoardCoreFfs"] = board_resources["resources"]["vortex_core"]["ffs"]
board_numbers["BoardCoreDsps"] = board_resources["resources"]["vortex_core"]["dsps"]
board_numbers["BoardAfuLuts"] = board_resources["resources"]["reconfigurable_afu"]["luts"]
board_numbers["BoardAfuFfs"] = board_resources["resources"]["reconfigurable_afu"]["ffs"]
board_numbers["BoardAfuDsps"] = board_resources["resources"]["reconfigurable_afu"]["dsps"]
board_numbers["BoardWns"] = board_resources["wns_ns"]

parameter_board = rows("parameter_board_200mhz.csv")
parameter_summary = json.loads((DATA / "parameter_board_summary_200mhz.json").read_text())
assert parameter_summary["board_clock_hz_before"] == parameter_summary["board_clock_hz_after"] == 200_000_000
assert parameter_summary["image_sha256"] == board_resources["vbin_sha256"]
assert parameter_summary["repetitions"] == 5 and parameter_summary["warmups"] == 1
assert parameter_summary["cells"] == len(parameter_board) == 120
assert len(parameter_summary["call_count_sha256"]) == 60
assert {r["parameter"] for r in parameter_board} == {
    "K512", "K768", "K1024", "D44", "D65", "D87"}
assert all(r["driver"] == "aved" and r["input_start"] == "1" for r in parameter_board)


def parameter_cycles(parameter, arm, batch, workers):
    return int(one(parameter_board, parameter=parameter, arm=arm,
                   requests=batch, resident=workers)["cycles"])


parameter_numbers = {}
parameter_plot = []
for index, parameter in enumerate(("K512", "K768", "K1024", "D44", "D65", "D87")):
    parameter_plot.append((index,
                           parameter_cycles(parameter, "A", 1, 1) /
                           parameter_cycles(parameter, "E", 1, 1),
                           parameter_cycles(parameter, "A", 8, 8) /
                           parameter_cycles(parameter, "E", 8, 8)))
table("parameter_speedup.dat", ["index", "one", "eight"], parameter_plot)
for batch, suffix in ((1, "One"), (8, "Eight")):
    speedups = [parameter_cycles(parameter, "A", batch, batch) /
                parameter_cycles(parameter, "E", batch, batch)
                for parameter in ("K512", "K768", "K1024", "D44", "D65", "D87")]
    parameter_numbers["ParameterMin" + suffix] = min(speedups)
    parameter_numbers["ParameterMax" + suffix] = max(speedups)
occupancy = [parameter_cycles(parameter, "E", 8, 1) /
             parameter_cycles(parameter, "E", 8, 8)
             for parameter in ("K512", "K768", "K1024", "D44", "D65", "D87")]
batch_throughput = [8 * parameter_cycles(parameter, "E", 8, 8) /
                    parameter_cycles(parameter, "E", 64, 8)
                    for parameter in ("K512", "K768", "K1024", "D44", "D65", "D87")]
parameter_numbers.update(ParameterOccupancyMin=min(occupancy),
                         ParameterOccupancyMax=max(occupancy),
                         ParameterBatchMin=min(batch_throughput),
                         ParameterBatchMax=max(batch_throughput))

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

ppa = rows("core_ppa_200mhz.csv")
assert len(ppa) == 9
assert all(r["target_mhz"] == "200" and r["dcache_writeback"] == "0"
           and r["routing_errors"] == r["drc_errors"] == "0" and r["status"] == "met"
           and float(r["wns_ns"]) >= 0 and float(r["whs_ns"]) >= 0 for r in ppa)
assert {(int(r["multipliers"]), r["keccak"]) for r in ppa} == {
    (16, "base"), (16, "stage"), (16, "round"), (16, "pointer"), (8, "base"),
    (4, "base"), (2, "base"), (1, "base"), (2, "stage")}

bank_nttbf = rows("ntt_multiplier_bank_nttbf.csv")
bank_perf = rows("ntt_multiplier_bank_performance.csv")
bank_order = [16, 8, 4, 2, 1]
assert [int(r["multipliers"]) for r in bank_nttbf] == bank_order
assert len(bank_perf) == 40 and all(r["result"] == "PASS" for r in bank_perf)
assert all(r["instructions"] == "59534" and r["result"] == "PASS"
           for r in bank_nttbf)
nttbf_base = int(bank_nttbf[0]["rtl_cycles"])
ppa_base = one(ppa, multipliers=16, keccak="base")
table("ntt_bank.dat",
      ["bank", "cycles", "nttlut", "nttff", "nttdsp"],
      [(bank,
        100 * int(one(bank_nttbf, multipliers=bank)["rtl_cycles"]) / nttbf_base,
        100 * int(one(ppa, multipliers=bank, keccak="base")["ntt_luts"]) / int(ppa_base["ntt_luts"]),
        100 * int(one(ppa, multipliers=bank, keccak="base")["ntt_ffs"]) / int(ppa_base["ntt_ffs"]),
        100 * int(one(ppa, multipliers=bank, keccak="base")["ntt_dsps"]) / int(ppa_base["ntt_dsps"]))
       for bank in bank_order])
bank_two_ppa = one(ppa, multipliers=2, keccak="base")
bank_two_perf = [r for r in bank_perf
                 if r["multipliers"] == "2" and r["driver"] == "xrt"]
assert len(bank_two_perf) == 4
bank_numbers = {
    "BankTwoDspPct": 100 * int(bank_two_ppa["ntt_dsps"]) / int(ppa_base["ntt_dsps"]),
    "BankTwoCyclesPct": 100 * int(one(bank_nttbf, multipliers=2)["rtl_cycles"]) / nttbf_base,
    "BankOneDspPct": 100 * int(one(ppa, multipliers=1, keccak="base")["ntt_dsps"]) /
                     int(ppa_base["ntt_dsps"]),
    "BankOneCyclesPct": 100 * int(one(bank_nttbf, multipliers=1)["rtl_cycles"]) / nttbf_base,
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
cost_lines = []
for bank, keccak, label in [(16, "base", "M16 base"),
                            (16, "stage", "M16 + Stage"),
                            (16, "round", "M16 + Round"),
                            (16, "pointer", "M16 + Pointer")]:
    r = one(ppa, multipliers=bank, keccak=keccak)
    cost_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & "
                      f"{r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
cost_lines.append(r"\midrule")
for keccak, label in [("base", "M2 base"), ("stage", "M2 + Stage")]:
    r = one(ppa, multipliers=2, keccak=keccak)
    cost_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & "
                      f"{r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
(OUT / "cost_rows.tex").write_text("\n".join(cost_lines) + "\n")
base = ppa_base
stage = one(ppa, multipliers=16, keccak="stage")
round_unit = one(ppa, multipliers=16, keccak="round")
pointer_unit = one(ppa, multipliers=16, keccak="pointer")
joint_stage = one(ppa, multipliers=2, keccak="stage")
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
    "PointerLutPct": 100 * (int(pointer_unit["total_luts"]) / int(base["total_luts"]) - 1),
    "StageFfPct": 100 * (int(stage["ffs"]) / int(base["ffs"]) - 1),
    "RoundFfPct": 100 * (int(round_unit["ffs"]) / int(base["ffs"]) - 1),
    "PointerFfPct": 100 * (int(pointer_unit["ffs"]) / int(base["ffs"]) - 1),
    "JointStageLutPct": 100 * (int(joint_stage["total_luts"]) / int(bank_two_ppa["total_luts"]) - 1),
    "JointStageFfPct": 100 * (int(joint_stage["ffs"]) / int(bank_two_ppa["ffs"]) - 1),
    "AllStageOne": stage_cost["000", 1] / stage_cost["111", 1],
    "AllStageEight": stage_cost["000", 8] / stage_cost["111", 8],
    "FusionOne": fusion["stage_unrolled", 1] / fusion["kround", 1],
    "FusionEight": fusion["stage_unrolled", 8] / fusion["kround", 8],
}
numbers.update(phase_pct)
numbers.update(pointwise_numbers)
numbers.update(final_phase_numbers)
numbers.update(bank_numbers)
numbers.update(board_numbers)
numbers.update(parameter_numbers)
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
def latex_number(key, value):
    return f"{value:,}" if key.startswith(("BoardCore", "BoardAfu")) else f"{value:.3f}"


(OUT / "numbers.tex").write_text("\n".join(
    f"\\newcommand{{\\{key}}}{{{latex_number(key, value)}}}"
    for key, value in numbers.items()) + "\n")
(OUT / "source_manifest.json").write_text(json.dumps({
    "mlkem_native_commit": "1d7b486c4db3bbc620b009d05e4f69eca9e68d22",
    "measurements": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in sorted(DATA.iterdir()) if p.suffix in (".csv", ".json")},
    "derived_numbers": numbers,
}, indent=2) + "\n")
