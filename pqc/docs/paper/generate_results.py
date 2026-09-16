#!/usr/bin/env python3
"""Build manuscript tables and vector figures from archived measurements."""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / "pqc/results"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
OUT = parser.parse_args().output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
sources = {}


def read(name):
    path = RESULTS / (name + ".csv")
    sources[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    with path.open() as stream:
        rows = list(csv.DictReader(line for line in stream
                                   if line.strip() and not line.startswith("#")))
    assert all(None not in row and None not in row.values() for row in rows), name
    return rows


def table(name, rows):
    (OUT / (name + ".tex")).write_text("".join(" & ".join(row) + " \\\\\n" for row in rows))


def integer(value):
    return f"{int(value):,}"


def number(value, places=3):
    return f"{float(value):,.{places}f}"


def percent(value):
    return f"{100 * value:.2f}\\%"


def million(value):
    return f"{int(value) / 1e6:.3f}"


plt.rcParams.update({"font.family": "DejaVu Serif", "font.size": 8,
                     "axes.labelsize": 8, "legend.fontsize": 7,
                     "xtick.labelsize": 7, "ytick.labelsize": 7,
                     "pdf.fonttype": 42, "ps.fonttype": 42,
                     "axes.spines.top": False, "axes.spines.right": False})
colors = ["#235789", "#d28231", "#a6adb5"]
macros = {}

mlk = read("ablation_mlkem")
base = int(mlk[0]["cycles"])
keccak = int(mlk[1]["delta_vs_baseline"])
ntt = int(mlk[2]["delta_vs_baseline"])
assert base - int(mlk[1]["cycles"]) == keccak
assert base - int(mlk[2]["cycles"]) == ntt
assert {row["keccak_x1"] for row in mlk} == {"144"}
macros.update(MlkProfileCycles=integer(base), MlkKeccakCycles=integer(keccak),
              MlkNttCycles=integer(ntt), MlkResidualCycles=integer(base-keccak-ntt),
              MlkKeccakShare=percent(keccak/base), MlkNttShare=percent(ntt/base),
              MlkResidualShare=percent((base-keccak-ntt)/base),
              MlkKeccakBound=number(base/(base-keccak)),
              MlkNttBound=number(base/(base-ntt)))
table("profile_mlk", [["Profile baseline", integer(base), "--", "--", "--"],
    ["Keccak removed", integer(base-keccak), integer(keccak), percent(keccak/base), number(base/(base-keccak))],
    ["NTT/INTT removed", integer(base-ntt), integer(ntt), percent(ntt/base), number(base/(base-ntt))]])

dsa = read("ablation_mldsa")
dsa_rows = []
breakdowns = [[keccak/base*100, ntt/base*100, (base-keccak-ntt)/base*100]]
for index in (0, 4, 2, 6):
    a, b = dsa[index:index+2]
    total, dk, dn = int(a["baseline_cycles"]), int(a["delta"]), int(b["delta"])
    is_measured = a["scope"] == "keypair"
    scope = "KeyGen" if is_measured else "Round trip"
    dsa_rows.append([scope+" / "+a["ram"], million(total), percent(dk/total),
                     percent(dn/total), "M" if is_measured else "E"])
    if is_measured:
        assert total-int(a["ablated_cycles"]) == dk
        assert total-int(b["ablated_cycles"]) == dn
        breakdowns.append([100*dk/total, 100*dn/total, 100*(total-dk-dn)/total])
table("profile_dsa", dsa_rows)
fig, ax = plt.subplots(figsize=(3.45, 2.15), layout="constrained")
left = np.zeros(3)
for index, label in enumerate(["Keccak-f", "NTT + INTT", "Residual (derived)"]):
    values = np.array(breakdowns)[:, index]
    ax.barh(range(3), values, left=left, height=.57, label=label,
            color=colors[index], edgecolor="white", hatch="//" if index==2 else None)
    for y, (x, width) in enumerate(zip(left, values)):
        ax.text(x+width/2, y, f"{width:.1f}%", va="center", ha="center", fontsize=7,
                color="white" if index==0 else "black")
    left += values
ax.set_yticks(range(3), ["ML-KEM\nround trip", "ML-DSA\nKeyGen / low", "ML-DSA\nKeyGen / full"])
ax.invert_yaxis()
ax.set_xlim(0, 100)
ax.set_xlabel("Fraction of each instrumented baseline (%)")
ax.legend(loc="lower center", bbox_to_anchor=(.38, 1.02), ncol=2, frameon=False)
fig.savefig(OUT/"profile.pdf")
plt.close(fig)

software = {r["metric"]: r for r in read("keccak_baseline_columns")}
table("software", [[label, integer(software[key]["reference_c"]) if kind=="int" else number(software[key]["reference_c"],1),
                    integer(software[key]["pqrv_asm"]) if kind=="int" else number(software[key]["pqrv_asm"],1),
                    number(float(software[key]["reference_c"])/float(software[key]["pqrv_asm"]))]
                   for label, key, kind in [
                       ("Permutation (cycles)", "keccakf1600_permute_cycles_per_call", "float"),
                       ("KEM launch (cycles)", "mlkem768_device_cycles", "int"),
                       ("Retired instructions", "mlkem768_instructions", "int"),
                       ("Stack peak (bytes)", "mlkem768_stack_peak_bytes", "int")]])
ntt_rows = read("ntt_cooperative")
assert all(row["mismatch"] == "0" for row in ntt_rows)
table("ntt", [[r["L"], integer(r["coop_cycles"]),
                number(int(r["ref_cycles"])/int(r["coop_cycles"])),
                number(int(ntt_rows[0]["coop_cycles"])/int(r["coop_cycles"]))] for r in ntt_rows])
batch = read("batch_baseline")
batch_bases = {r["scheme"]: int(r["device_cycles"]) for r in batch if r["M"] == "1"}
table("batch", [[r["scheme"], r["M"], million(r["device_cycles"]),
                number(int(r["M"])*batch_bases[r["scheme"]]/int(r["device_cycles"]))]
                for r in batch])
ram = read("mldsa_ram_x_lane")
table("ram", [[r["ram"], r["L"], million(r["cycles"]), integer(r["arena_peak"]),
              number(int(ram[0]["cycles"])/int(r["cycles"]))] for r in ram])

grid = read("mlkem_MxL")
fig, axes = plt.subplots(1, 2, figsize=(3.45, 2.15), sharey=True, layout="constrained")
grid_base = int(grid[0]["cycles"])
for ax, cache in zip(axes, ["off", "on"]):
    for lanes, marker, color in zip([1,2,4], ["o","s","^"], colors):
        selected = sorted([r for r in grid if r["l2"]==cache and int(r["L"])==lanes], key=lambda r:int(r["M"]))
        ax.plot([int(r["M"]) for r in selected],
                [int(r["M"])*grid_base/int(r["cycles"]) for r in selected],
                marker=marker, markersize=4, linewidth=1.2, color=color, label=f"L={lanes}")
    ax.set_title("L2 "+cache, fontsize=9)
    ax.set_xticks([1,2,4,8])
    ax.set_xlabel("Requests M")
    ax.grid(axis="y", alpha=.2)
axes[0].set_ylabel("Throughput gain")
axes[0].set_ylim(0,9)
axes[1].legend(frameon=False, fontsize=7, loc="upper left")
fig.savefig(OUT/"batch_width.pdf")
plt.close(fig)

stages = read("keccak_sg25_stages")
index = {(r["driver"], r["arm"], int(r["batch_warps"]), int(r["permutations_per_state"])):r for r in stages}
stage_cost = {}
max_gap = 0
for key, sim in index.items():
    if key[0] != "simx":
        continue
    rtl = index[("rtlsim", *key[1:])]
    assert sim["instrs"] == rtl["instrs"]
    assert sim["passed"] == rtl["passed"] == "1"
    max_gap = max(max_gap, abs(int(sim["cycles"])-int(rtl["cycles"]))/int(rtl["cycles"]))
for arm in ["000", "100", "010", "001", "110", "101", "011", "111"]:
    stage_cost[arm] = []
    for warps in (1,8):
        low, high = [index[("rtlsim",arm,warps,p)] for p in (4,8)]
        assert low["elf_sha256"] == high["elf_sha256"]
        stage_cost[arm].append((int(high["span_cycles"])-int(low["span_cycles"]))/ (4*warps))
labels = {a:"".join(letter if bit=="1" else "-" for bit,letter in zip(a,"TRC")) for a in stage_cost}
table("stages", [[r"\texttt{"+labels[arm]+"}", number(v[0],2), number(stage_cost["000"][0]/v[0]),
                  number(v[1],2), number(stage_cost["000"][1]/v[1])] for arm,v in stage_cost.items()])
macros["StageLatencyGain"] = number(stage_cost["000"][0]/stage_cost["111"][0],2)
macros["StageThroughputGain"] = number(stage_cost["000"][1]/stage_cost["111"][1],2)
macros["StageMaxModelGap"] = percent(max_gap)
fig, ax = plt.subplots(figsize=(3.45,2.2), layout="constrained")
x = np.arange(8)
for i, label in enumerate(["One state", "Eight warps"]):
    gains = [stage_cost["000"][i]/value[i] for value in stage_cost.values()]
    ax.bar(x+(i-.5)*.38, gains, .36, color=colors[i], label=label)
ax.set_xticks(x, list(labels.values()))
ax.set_ylabel("RTL gain over shuffle-only SG25")
ax.set_ylim(0,15)
ax.legend(frameon=False, loc="upper left")
ax.grid(axis="y", alpha=.2)
fig.savefig(OUT/"stage_gains.pdf")
plt.close(fig)

rounds = read("keccak_kround25")
assert all(r["permutations_per_state"] == "64" and r["result"] == "PASS" for r in rounds)
round_index = {(r["driver"],r["design"],int(r["batch_warps"])):r for r in rounds}
table("rounds", [[driver, "Stages" if design=="sg25_stages" else "Whole round",
                   number(int(round_index[driver,design,1]["span_cycles"])/64),
                   number(int(round_index[driver,design,8]["span_cycles"])/512)]
                  for driver in ["simx","rtlsim"] for design in ["sg25_stages","kround25"]])
round_gain = int(round_index["rtlsim","sg25_stages",8]["span_cycles"])/int(round_index["rtlsim","kround25",8]["span_cycles"])
macros["RoundThroughputGain"] = number(round_gain)

kem = read("keccak_sg25_mlkem")
kem_index = {(r["arm"],int(r["blocks"])):r for r in kem}
names = {"sg1_serial":"SG1 C", "sg25_sw":"SG25 shuffle", "sg25_leader":"SG25 stages",
         "kround25":"SG25 whole round", "pointer_keccakf":"Pointer PE", "sg25_redundant":"Redundant outer KEM"}
for r in kem:
    assert r["result"] == "PASS" and r["permutations_per_request"] == "143"
    phase_sum = sum(int(r[k]) for k in ["keypair_cycles_request0","encaps_cycles_request0","decaps_cycles_request0"])
    assert phase_sum == int(r["phase_total_request0"])
table("kem", [[name, million(kem_index[arm,1]["device_cycles"]),
                number(int(kem_index["sg1_serial",1]["device_cycles"])/int(kem_index[arm,1]["device_cycles"])),
                million(kem_index[arm,8]["device_cycles"]),
                number(int(kem_index["sg1_serial",8]["device_cycles"])/int(kem_index[arm,8]["device_cycles"]))]
               for arm,name in names.items()])
table("phases", [[name, million(kem_index[arm,1]["keypair_cycles_request0"]),
                   million(kem_index[arm,1]["encaps_cycles_request0"]),
                   million(kem_index[arm,1]["decaps_cycles_request0"]),
                   million(int(kem_index[arm,1]["device_cycles"])-int(kem_index[arm,1]["phase_total_request0"]))]
                  for arm,name in names.items() if arm!="sg25_redundant"])
table("traffic", [[names[arm],integer(kem_index[arm,1]["lsu_loads"]),integer(kem_index[arm,1]["lsu_stores"]),
                   integer(kem_index[arm,1]["stack_peak_bytes"])] for arm in names])
for warps in [1,8]:
    suffix = "One" if warps==1 else "Eight"
    def cyc(arm): return int(kem_index[arm,warps]["device_cycles"])
    macros["KemStage"+suffix] = number(cyc("sg1_serial")/cyc("sg25_leader"))
    macros["KemMapping"+suffix] = number(cyc("sg1_serial")/cyc("sg25_sw"))
    macros["KemIse"+suffix] = number(cyc("sg25_sw")/cyc("sg25_leader"))
    macros["KemRound"+suffix] = number(cyc("sg25_leader")/cyc("kround25"))
fig, axes = plt.subplots(1,2,figsize=(3.45,2.3),sharey=True,layout="constrained")
for ax, warps in zip(axes,[1,8]):
    arms = list(names)[:5]
    gains = [int(kem_index["sg1_serial",warps]["device_cycles"])/int(kem_index[arm,warps]["device_cycles"]) for arm in arms]
    bars = ax.bar(range(5),gains,color=["#a6adb5", "#82a9c9", colors[0], colors[1], "#528a67"])
    ax.bar_label(bars,labels=[f"{v:.2f}" for v in gains],fontsize=6,padding=2)
    ax.set_xticks(range(5),["SG1", "Shuffle", "Stages", "Round", "Pointer"],rotation=50,ha="right")
    ax.set_title(f"M = {warps}",fontsize=9)
    ax.set_ylim(0,3.6)
    ax.grid(axis="y",alpha=.2)
axes[0].set_ylabel("Whole-launch speedup over SG1")
fig.savefig(OUT/"kem_gains.pdf")
plt.close(fig)

unified = read("keccak_ntt_unified_mlkem")
assert all(r["result"] == "PASS" and r["kat"] == "PASS" for r in unified)
for r in unified:
    assert r["permutations_per_request"] == "140"
    assert r["ntt_per_request"] == "15" and r["intt_per_request"] == "9"
    assert sum(int(r[k]) for k in ["keypair_cycles_request0",
                                   "encaps_cycles_request0",
                                   "decaps_cycles_request0"]) == int(r["request0_total"])
unified_simx = {(r["backend"], int(r["requests"])): r
                for r in unified if r["driver"] == "simx"}
assert len(unified_simx) == 10
unified_names = {"sg1_serial": "SG1 C", "sg25_sw": "SG25 shuffle",
                 "sg25_stages": "SG25 stages", "kround25": "SG25 whole round",
                 "pointer_keccakf": "Pointer PE"}

def unified_cycles(backend, requests):
    return int(unified_simx[backend, requests]["device_cycles"])

table("unified_kem", [[name, million(unified_cycles(backend, 1)),
                       number(unified_cycles("sg1_serial", 1) /
                              unified_cycles(backend, 1)),
                       million(unified_cycles(backend, 8)),
                       number(unified_cycles("sg1_serial", 8) /
                              unified_cycles(backend, 8))]
                      for backend, name in unified_names.items()])
for requests, suffix in [(1, "One"), (8, "Eight")]:
    macros["UnifiedStage" + suffix] = number(
        unified_cycles("sg1_serial", requests) /
        unified_cycles("sg25_stages", requests))
    macros["UnifiedIse" + suffix] = number(
        unified_cycles("sg25_sw", requests) /
        unified_cycles("sg25_stages", requests))
    macros["UnifiedRound" + suffix] = number(
        unified_cycles("sg25_stages", requests) /
        unified_cycles("kround25", requests))
macros["UnifiedStageVsPointerOne"] = number(
    unified_cycles("pointer_keccakf", 1) /
    unified_cycles("sg25_stages", 1))
macros["UnifiedRoundVsPointerOne"] = number(
    unified_cycles("pointer_keccakf", 1) /
    unified_cycles("kround25", 1))
macros["UnifiedPointerVsStageEight"] = number(
    unified_cycles("sg25_stages", 8) /
    unified_cycles("pointer_keccakf", 8))
macros["UnifiedPointerVsRoundEight"] = number(
    unified_cycles("kround25", 8) /
    unified_cycles("pointer_keccakf", 8))
unified_xrt = {r["backend"]: r for r in unified if r["driver"] == "xrt"}
assert set(unified_xrt) == {"sg25_stages", "kround25"}
for backend, xrt_row in unified_xrt.items():
    simx_row = unified_simx[backend, 1]
    assert xrt_row["retired_instructions"] == simx_row["retired_instructions"]
    gap = abs(int(xrt_row["device_cycles"]) - int(simx_row["device_cycles"])) \
          / int(xrt_row["device_cycles"])
    assert gap < 0.01
    macros["UnifiedXrtGap" + ("Stage" if backend == "sg25_stages" else "Round")] = \
        number(100 * gap, 3) + "\\%"

xlen_rows = read("rv32im_rv64im_keccak")
assert all(r["result"] == "PASS" and r["kat"] == "PASS" for r in xlen_rows)
for field in ("git_commit", "warps", "threads", "ntt_backend", "arith_backend"):
    assert len({r[field] for r in xlen_rows}) == 1, field
xlen_simx = {(r["keccak_backend"], int(r["xlen"]), int(r["requests"])): r
             for r in xlen_rows if r["driver"] == "simx"}
assert set(xlen_simx) == {(backend, xlen, requests)
                         for backend in ("stage", "whole_round")
                         for xlen in (32, 64) for requests in (1, 8)}
xlen_names = {"stage": "Stage", "whole_round": "KROUND"}
table("xlen", [[xlen_names[backend] + " RV" + str(xlen) + "IM",
                 integer(xlen_simx[backend, xlen, 1]["retired_instructions"]),
                 million(xlen_simx[backend, xlen, 1]["device_cycles"]),
                 integer(xlen_simx[backend, xlen, 8]["retired_instructions"]),
                 million(xlen_simx[backend, xlen, 8]["device_cycles"])]
                for backend in ("stage", "whole_round")
                for xlen in (32, 64)])
for backend, prefix in (("stage", "Stage"), ("whole_round", "Round")):
    rv32_m1, rv32_m8 = (xlen_simx[backend, 32, requests]
                         for requests in (1, 8))
    rv64_m1, rv64_m8 = (xlen_simx[backend, 64, requests]
                         for requests in (1, 8))
    macros["RvSixtyFour" + prefix + "InstructionReduction"] = percent(
        1 - int(rv64_m1["retired_instructions"]) /
        int(rv32_m1["retired_instructions"]))
    assert macros["RvSixtyFour" + prefix + "InstructionReduction"] == percent(
        1 - int(rv64_m8["retired_instructions"]) /
        int(rv32_m8["retired_instructions"]))
    macros["RvSixtyFour" + prefix + "MOneCycleReduction"] = percent(
        1 - int(rv64_m1["device_cycles"]) / int(rv32_m1["device_cycles"]))
    macros["RvSixtyFour" + prefix + "MEightCycleIncrease"] = percent(
        int(rv64_m8["device_cycles"]) / int(rv32_m8["device_cycles"]) - 1)
    controls = {(int(r["xlen"]), r["driver"]): r for r in xlen_rows
                if r["keccak_backend"] == backend and r["requests"] == "1"
                and r["driver"] != "simx"}
    assert set(controls) == {(xlen, driver) for xlen in (32, 64)
                            for driver in ("rtlsim", "xrt")}
    for (xlen, _), row in controls.items():
        simx_row = xlen_simx[backend, xlen, 1]
        assert row["retired_instructions"] == simx_row["retired_instructions"]
        assert abs(int(row["device_cycles"]) - int(simx_row["device_cycles"])) / \
            int(row["device_cycles"]) < 0.01

phase_profile = read("mlkem_phase_profile")
assert all(r["kat"] == "PASS" and r["status"] == "PASS"
           for r in phase_profile)
phase_rows = [r for r in phase_profile if r["measurement"] == "phase"]
headline_rows = [r for r in phase_profile if r["measurement"] == "headline"]
phase_index = {(r["backend"], r["driver"]): r for r in phase_rows}
headline_index = {(r["backend"], int(r["requests"])): r
                  for r in headline_rows}
phase_backends = ("stage", "whole_round", "pointer")
assert set(phase_index) == {(backend, driver)
                           for backend in phase_backends
                           for driver in ("simx", "rtlsim", "xrt")}
assert set(headline_index) == {(backend, requests)
                              for backend in phase_backends
                              for requests in (1, 8)}
phase_buckets = ("keccak_permute_cycles", "keccak_absorb_cycles",
                 "keccak_squeeze_cycles", "ntt_cycles", "intt_cycles",
                 "mulcache_cycles", "basemul_cycles", "reduce_cycles",
                 "other_cycles")
phase_simx_rtl_gap = 0.0
phase_xrt_rtl_gap = 0.0
for backend in phase_backends:
    rows = [phase_index[backend, driver]
            for driver in ("simx", "rtlsim", "xrt")]
    assert {r["requests"] for r in rows} == {"1"}
    assert len({r["instructions"] for r in rows}) == 1
    for row in rows:
        assert sum(int(row[field]) for field in phase_buckets) == \
            int(row["interval_or_makespan_cycles"])
    simx, rtl, xrt = rows
    rtl_total = int(rtl["interval_or_makespan_cycles"])
    phase_simx_rtl_gap = max(
        phase_simx_rtl_gap,
        abs(int(simx["interval_or_makespan_cycles"]) - rtl_total) / rtl_total)
    phase_xrt_rtl_gap = max(
        phase_xrt_rtl_gap,
        abs(int(xrt["interval_or_makespan_cycles"]) - rtl_total) / rtl_total)
    headline = headline_index[backend, 1]
    overhead = 100 * (int(simx["interval_or_makespan_cycles"]) /
                      int(headline["interval_or_makespan_cycles"]) - 1)
    assert abs(overhead - float(simx["instrumentation_overhead_pct"])) < 0.001

phase_names = {"stage": "Stages", "whole_round": "Whole round",
               "pointer": "Pointer PE"}
phase_percent_fields = (
    ("keccak_permute_cycles",),
    ("keccak_absorb_cycles",),
    ("keccak_squeeze_cycles",),
    ("ntt_cycles", "intt_cycles"),
    ("mulcache_cycles", "basemul_cycles", "reduce_cycles"),
    ("other_cycles",),
)
table("direct_phases", [[phase_names[backend]] + [
    percent(sum(int(row[field]) for field in fields) / total)
    for fields in phase_percent_fields]
    for backend in phase_backends
    for row in [phase_index[backend, "xrt"]]
    for total in [int(row["interval_or_makespan_cycles"])]])
for backend, prefix in (("stage", "Stage"),
                        ("whole_round", "Round"),
                        ("pointer", "Pointer")):
    row = phase_index[backend, "xrt"]
    total = int(row["interval_or_makespan_cycles"])
    macros["Direct" + prefix + "ProbeOverhead"] = number(
        phase_index[backend, "simx"]["instrumentation_overhead_pct"], 3) + "\\%"
    macros["Direct" + prefix + "SpongeShare"] = percent(
        (int(row["keccak_absorb_cycles"]) +
         int(row["keccak_squeeze_cycles"])) / total)
macros["DirectPointerPermuteShare"] = percent(
    int(phase_index["pointer", "xrt"]["keccak_permute_cycles"]) /
    int(phase_index["pointer", "xrt"]["interval_or_makespan_cycles"]))
macros["DirectMaxSimxRtlGap"] = percent(phase_simx_rtl_gap)
macros["DirectMaxXrtRtlGap"] = percent(phase_xrt_rtl_gap)

backend_ppa = {r["variant"]: r for r in read("ntt_keccak_backend_ppa")}
backend_names = {"ntt_only": "NTT only", "ntt_stage": "NTT + stages",
                 "ntt_kround": "NTT + whole round", "ntt_pointer": "NTT + pointer PE"}
assert set(backend_ppa) == set(backend_names)
assert {r["target_mhz"] for r in backend_ppa.values()} == {"250"}
assert {r["status"] for r in backend_ppa.values()} == {"met"}
assert {r["routing_errors"] for r in backend_ppa.values()} == {"0"}
for field in ("git_commit", "tool", "device", "stage", "warps", "threads",
              "ramb36", "ramb18", "uram", "dsps"):
    assert len({r[field] for r in backend_ppa.values()}) == 1, field
backend_base = backend_ppa["ntt_only"]
base_lut, base_ff = int(backend_base["total_luts"]), int(backend_base["ffs"])
table("core_ppa", [[name, integer(r["total_luts"]),
                    "--" if key == "ntt_only" else "+" + integer(int(r["total_luts"])-base_lut),
                    integer(r["ffs"]),
                    "--" if key == "ntt_only" else "+" + integer(int(r["ffs"])-base_ff),
                    integer(r["dsps"]), r["wns_ns"]]
                   for key, name in backend_names.items() for r in [backend_ppa[key]]])

xlen_ppa_rows = read("rv32im_rv64im_keccak_ppa")
xlen_ppa = {(int(r["xlen"]), r["variant"]): r for r in xlen_ppa_rows}
assert set(xlen_ppa) == {(xlen, variant)
                         for xlen in (32, 64)
                         for variant in ("ntt_only", "ntt_stage", "ntt_kround")}
assert all(r["routing_errors"] == "0" for r in xlen_ppa_rows)
for field in ("tool", "device", "stage", "target_mhz", "warps", "threads"):
    assert len({r[field] for r in xlen_ppa_rows}) == 1, field
for r in xlen_ppa_rows:
    expected = "met" if float(r["wns_ns"]) >= 0 else "timing_unmet"
    assert r["status"] == expected
    assert r["f_enabled"] == r["d_enabled"] == "0"
    assert r["ntt_hier_dsps"] == "16"
    assert r["fpu_hier_luts"] == r["fpu_hier_ffs"] == r["fpu_hier_dsps"] == "0"
for xlen in (32, 64):
    ntt_only = xlen_ppa[xlen, "ntt_only"]
    for variant in ("ntt_stage", "ntt_kround"):
        row = xlen_ppa[xlen, variant]
        assert ntt_only["bram_tiles"] == row["bram_tiles"]
        assert ntt_only["dsps"] == row["dsps"]
        assert ntt_only["muldiv_hier_dsps"] == row["muldiv_hier_dsps"]
assert {xlen_ppa[32, variant]["muldiv_hier_dsps"]
        for variant in ("ntt_only", "ntt_stage", "ntt_kround")} == {"96"}
assert {xlen_ppa[64, variant]["muldiv_hier_dsps"]
        for variant in ("ntt_only", "ntt_stage", "ntt_kround")} == {"0"}
xlen_ppa_names = {"ntt_only": "NTT only", "ntt_stage": "NTT + stages",
                  "ntt_kround": "NTT + whole round"}
table("xlen_ppa", [["RV" + str(xlen) + " " + xlen_ppa_names[variant],
                     integer(r["total_luts"]), integer(r["ffs"]),
                     number(r["bram_tiles"], 1), integer(r["dsps"]),
                     number(r["wns_ns"], 3), number(r["fmax_mhz"], 1)]
                    for xlen in (32, 64)
                    for variant in ("ntt_only", "ntt_stage", "ntt_kround")
                    for r in [xlen_ppa[xlen, variant]]])
rv32_ntt = xlen_ppa[32, "ntt_only"]
rv64_ntt = xlen_ppa[64, "ntt_only"]
macros["RvSixtyFourNttLutOverhead"] = percent(
    int(rv64_ntt["total_luts"]) / int(rv32_ntt["total_luts"]) - 1)
macros["RvSixtyFourNttFfOverhead"] = percent(
    int(rv64_ntt["ffs"]) / int(rv32_ntt["ffs"]) - 1)
macros["RvSixtyFourNttBramOverhead"] = percent(
    float(rv64_ntt["bram_tiles"]) / float(rv32_ntt["bram_tiles"]) - 1)
macros["RvSixtyFourNttDspOverhead"] = percent(
    int(rv64_ntt["dsps"]) / int(rv32_ntt["dsps"]) - 1)
macros["RvThirtyTwoMuldivDsps"] = integer(rv32_ntt["muldiv_hier_dsps"])
macros["RvSixtyFourMuldivDsps"] = integer(rv64_ntt["muldiv_hier_dsps"])
for variant, name in (("ntt_stage", "Stage"),
                      ("ntt_kround", "Round")):
    row = xlen_ppa[64, variant]
    macros["RvSixtyFour" + name + "Wns"] = number(row["wns_ns"], 3)
    macros["RvSixtyFour" + name + "Fmax"] = number(row["fmax_mhz"], 1)
for xlen, prefix in ((32, "RvThirtyTwo"), (64, "RvSixtyFour")):
    base = xlen_ppa[xlen, "ntt_only"]
    for variant, name in (("ntt_stage", "Stage"),
                          ("ntt_kround", "Round")):
        row = xlen_ppa[xlen, variant]
        for field, suffix in (("total_luts", "Lut"), ("ffs", "Ff")):
            value, base_value = int(row[field]), int(base[field])
            macros[prefix + name + suffix + "Delta"] = integer(value - base_value)
            macros[prefix + name + suffix + "Overhead"] = percent(
                value / base_value - 1)
macros["FinalNttDsps"] = integer(rv64_ntt["ntt_hier_dsps"])

ntt_ppa_rows = read("ntt_v80_ppa")
ntt_ppa = {r["variant"]: r for r in ntt_ppa_rows}
ntt_names = {"keccak_pe_static_agu": "No NTT ISE",
             "keccak_pe_static_agu_nttmul_sg2_reducepipe": "Full bank",
             "keccak_pe_static_agu_nttmul_sg2_halfbank": "Half bank"}
assert set(ntt_names) <= set(ntt_ppa)
assert {ntt_ppa[key]["status"] for key in ntt_names} == {"met"}
assert {ntt_ppa[key]["constraint_mhz"] for key in ntt_names} == {"250"}
ntt_base = ntt_ppa["keccak_pe_static_agu"]
ntt_base_lut, ntt_base_ff = int(ntt_base["total_luts"]), int(ntt_base["ffs"])
table("ntt_ppa", [[name, integer(r["total_luts"]),
                   "--" if key == "keccak_pe_static_agu" else "+" + integer(int(r["total_luts"])-ntt_base_lut),
                   integer(r["ffs"]),
                   "--" if key == "keccak_pe_static_agu" else "+" + integer(int(r["ffs"])-ntt_base_ff),
                   integer(r["dsps"]), r["wns_ns"]]
                  for key, name in ntt_names.items() for r in [ntt_ppa[key]]])

ppa = read("keccak_sg25_ppa")
unit_names = {"pointer_keccakf":"Pointer PE", "sg25_stage_unit":"Stages", "sg25_round_unit":"Whole round"}
units = [r for r in ppa if r["design"] in unit_names]
table("fpga_unit", [[unit_names[r["design"]],integer(r["lut"]),integer(r["ff"]),r["fmax_mhz"]] for r in units if r["technology"]=="fpga"])
table("asic_unit", [[unit_names[r["design"]],number(r["cell_area_um2"]),number(r["seq_area_um2"]),number(r["fmax_mhz"],1)] for r in units if r["technology"]=="asic"])
macros["RoundLutEfficiency"] = number(round_gain*int(backend_ppa["ntt_stage"]["total_luts"])/int(backend_ppa["ntt_kround"]["total_luts"]))
macros["StageLutOverhead"] = percent(int(backend_ppa["ntt_stage"]["total_luts"])/base_lut-1)
macros["StageFfOverhead"] = percent(int(backend_ppa["ntt_stage"]["ffs"])/base_ff-1)
macros["StageLutDelta"] = integer(int(backend_ppa["ntt_stage"]["total_luts"])-base_lut)
macros["StageFfDelta"] = integer(int(backend_ppa["ntt_stage"]["ffs"])-base_ff)
macros["RoundLutOverhead"] = percent(int(backend_ppa["ntt_kround"]["total_luts"])/base_lut-1)
macros["RoundFfOverhead"] = percent(int(backend_ppa["ntt_kround"]["ffs"])/base_ff-1)
macros["RoundLutDelta"] = integer(int(backend_ppa["ntt_kround"]["total_luts"])-base_lut)
macros["RoundFfDelta"] = integer(int(backend_ppa["ntt_kround"]["ffs"])-base_ff)
macros["PointerLutOverhead"] = percent(int(backend_ppa["ntt_pointer"]["total_luts"])/base_lut-1)
macros["PointerFfOverhead"] = percent(int(backend_ppa["ntt_pointer"]["ffs"])/base_ff-1)
macros["PointerLutDelta"] = integer(int(backend_ppa["ntt_pointer"]["total_luts"])-base_lut)
macros["PointerFfDelta"] = integer(int(backend_ppa["ntt_pointer"]["ffs"])-base_ff)
macros["StageCoreLutsMath"] = integer(backend_ppa["ntt_stage"]["total_luts"]).replace(",", "{,}")
macros["RoundCoreLutsMath"] = integer(backend_ppa["ntt_kround"]["total_luts"]).replace(",", "{,}")
macros["FinalNttLutOverhead"] = percent(int(ntt_ppa["keccak_pe_static_agu_nttmul_sg2_halfbank"]["total_luts"])/ntt_base_lut-1)
macros["FinalNttFfOverhead"] = percent(int(ntt_ppa["keccak_pe_static_agu_nttmul_sg2_halfbank"]["ffs"])/ntt_base_ff-1)
macros["HalfbankLutSaving"] = percent(1-int(ntt_ppa["keccak_pe_static_agu_nttmul_sg2_halfbank"]["total_luts"])/int(ntt_ppa["keccak_pe_static_agu_nttmul_sg2_reducepipe"]["total_luts"]))

controls = read("keccak_w32_controls")
control_map = {(r["arm"], r["driver"], int(r["batch"]),
                int(r["states_per_warp"]), int(r["permutations_per_state"])): r
               for r in controls}
assert len(control_map) == len(controls)
assert {r["result"] for r in controls} == {"PASS"}


def control(arm, batch, states=1, permutations=8, driver="simx"):
    return control_map[arm, driver, batch, states, permutations]


def per_permutation(row):
    count = int(row["batch"]) * int(row["states_per_warp"]) * int(row["permutations_per_state"])
    assert count == int(row["completed_permutations"])
    return int(row["span_cycles"]) / count


table("w32_mapping", [[label, str(states),
                       number(per_permutation(control(arm, 1)), 1),
                       number(per_permutation(control(arm, 8)), 1),
                       number(per_permutation(control(arm, 8, states)), 1)]
                      for arm, label, states in (("sg1", "SG1 C", 32),
                                                ("asm", "PQRV", 32),
                                                ("sg5", "SG5", 6),
                                                ("sg25_sw", "SG25", 1))])
table("w32_unroll", [[label, str(issues),
                      number(per_permutation(control(arm, 1, permutations=64)), 1),
                      number(per_permutation(control(arm, 8, permutations=64)), 1)]
                     for arm, label, issues in (("stage_loop", "Stage / loop", 144),
                                               ("stage_unrolled", "Stage / unrolled", 144),
                                               ("kround", "Whole round", 48))])
macros["MatchedRoundGainOne"] = number(
    per_permutation(control("stage_unrolled", 1, permutations=64)) /
    per_permutation(control("kround", 1, permutations=64)))
macros["MatchedRoundGainEight"] = number(
    per_permutation(control("stage_unrolled", 8, permutations=64)) /
    per_permutation(control("kround", 8, permutations=64)))

control_kem_rows = read("mlkem_w32_controls")
control_kem = {(r["arm"], r["driver"], int(r["batch"])): r for r in control_kem_rows}
assert len(control_kem) == len(control_kem_rows)
assert {r["kat"] for r in control_kem_rows} == {"PASS"}
assert {(r["keccak_x1"], r["keccak_x4"], r["ntt"], r["intt"])
        for r in control_kem_rows} == {("140", "0", "15", "9")}


def kem_control_cycles(arm, batch):
    return int(control_kem[arm, "simx", batch]["device_cycles"])


table("w32_kem", [[label, million(kem_control_cycles(arm, 1)),
                   number(kem_control_cycles("asm", 1) / kem_control_cycles(arm, 1)),
                   million(kem_control_cycles(arm, 8)),
                   number(kem_control_cycles("asm", 8) / kem_control_cycles(arm, 8))]
                  for arm, label in (("sg1", "SG1 C"), ("asm", "PQRV"),
                                     ("sg25_sw", "SG25 shuffle"),
                                     ("stage_loop", "Stage / loop"),
                                     ("stage_unrolled", "Stage / unrolled"),
                                     ("kround", "Whole round"))])
for batch, suffix in ((1, "One"), (8, "Eight")):
    macros["MatchedAsmGain" + suffix] = number(
        kem_control_cycles("sg1", batch) / kem_control_cycles("asm", batch))
    macros["MatchedRoundKemGain" + suffix] = number(
        kem_control_cycles("stage_unrolled", batch) / kem_control_cycles("kround", batch))
    macros["MatchedStageAsmGain" + suffix] = number(
        kem_control_cycles("asm", batch) / kem_control_cycles("stage_unrolled", batch))
macros["MatchedUnrollOneReduction"] = percent(
    1 - kem_control_cycles("stage_unrolled", 1) / kem_control_cycles("stage_loop", 1))
macros["MatchedUnrollEightIncrease"] = percent(
    kem_control_cycles("stage_unrolled", 8) / kem_control_cycles("stage_loop", 8) - 1)
control_gaps = []
sg5_gaps = []
for dataset, indexed in ((controls, control_map), (control_kem_rows, control_kem)):
    for r in dataset:
        if r["driver"] != "xrt":
            continue
        key = (r["arm"], "simx", int(r["batch"]))
        if dataset is controls:
            key += (int(r["states_per_warp"]), int(r["permutations_per_state"]))
        s = indexed[key]
        assert r["kernel_sha256"] == s["kernel_sha256"]
        assert r["retired_instructions"] == s["retired_instructions"]
        gap = abs(int(r["device_cycles"])-int(s["device_cycles"])) / int(r["device_cycles"])
        expected_parity = "PASS" if gap <= 0.05 else "FAIL"
        assert r["parity_status"] == s["parity_status"] == expected_parity
        assert gap <= 0.05 or r["arm"] == "sg5"
        (sg5_gaps if r["arm"] == "sg5" else control_gaps).append(gap)
assert len(sg5_gaps) == 2 and min(sg5_gaps) > 0.05
macros["MatchedControlMaxXrtGap"] = percent(max(control_gaps))
macros["MatchedSgFiveMinGap"] = percent(min(sg5_gaps))
macros["MatchedSgFiveMaxGap"] = percent(max(sg5_gaps))

sync_rows = read("keccak_sg5_sync")
sync_map = {(r["arm"], r["driver"], int(r["batch"]),
             int(r["states_per_warp"]), int(r["permutations_per_state"])): r
            for r in sync_rows}
assert len(sync_rows) == len(sync_map) == 62
assert {r["result"] for r in sync_rows} == {"PASS"}
sync_passes = 0
for r in sync_rows:
    if r["driver"] != "xrt":
        continue
    s = sync_map[r["arm"], "simx", int(r["batch"]),
                 int(r["states_per_warp"]), int(r["permutations_per_state"])]
    assert r["kernel_sha256"] == s["kernel_sha256"]
    assert r["host_sha256"] == s["host_sha256"]
    assert r["retired_instructions"] == s["retired_instructions"]
    gap = abs(int(r["device_cycles"]) - int(s["device_cycles"])) / int(r["device_cycles"])
    assert r["parity_status"] == s["parity_status"] == ("PASS" if gap <= 0.05 else "FAIL")
    assert abs(float(r["cycle_gap_pct"]) - 100 * gap) < 1e-6
    sync_passes += gap <= 0.05
assert sync_passes == 20
for name, arm, batch, states, permutations in (
        ("SgFiveWarpGapOne", "warp_barrier", 1, 1, 8),
        ("SgFiveWarpGapTwo", "warp_barrier", 2, 6, 2),
        ("SgFiveWarpGapPacked", "warp_barrier", 8, 6, 2),
        ("SgFiveWarpGapSingle", "warp_barrier", 8, 1, 8),
        ("SgFiveFenceGapEight", "fence", 8, 1, 8),
        ("SgFiveLmemGapEight", "lmem_barrier", 8, 1, 8)):
    row = sync_map[arm, "xrt", batch, states, permutations]
    macros[name] = percent(float(row["cycle_gap_pct"]) / 100)
sync_sources = RESULTS / "keccak_sg5_sync_sources.zip"
sources[str(sync_sources.relative_to(ROOT))] = hashlib.sha256(sync_sources.read_bytes()).hexdigest()

for name in ("keccak_ise_simx", "keccak_ise_mldsa", "core_config_v80", "keccak_sg5"):
    path = RESULTS / (name + ".csv")
    sources[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()

(OUT/"numbers.tex").write_text("".join("\\newcommand{\\"+key+"}{"+value+"}\n" for key,value in macros.items()))
manifest = {"description":"Hashes identify the manuscript input snapshot, not historical experiment source revisions.",
            "repository_head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
            "working_tree_has_changes":bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True).strip()),
            "source_sha256":sources, "derived_macros":macros,
            "stage_model_pairs":32, "stage_max_cycle_gap_fraction":max_gap}
(OUT/"provenance.json").write_text(json.dumps(manifest,indent=2)+"\n")
print(f"Recorded {len(sources)} source files; generated checked tables, 4 PDF figures, and provenance in {OUT}")
