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

ppa = read("keccak_sg25_ppa")
core = {(r["design"],int(r["target_mhz"])):r for r in ppa if "core_8w32" in r["design"]}
core_names = {"base_core_8w32":"Base", "sg25_core_8w32":"Stages", "kround25_core_8w32":"Whole round", "pointer_core_8w32":"Pointer PE"}
table("core_ppa", [[str(target),name,integer(core[key,target]["lut"]),
                    integer(core[key,target]["ff"]) if core[key,target]["ff"] else "--",
                    core[key,target]["wns_ns"],core[key,target]["fmax_mhz"]]
                   for target in [250,300] for key,name in core_names.items() if (key,target) in core])
unit_names = {"pointer_keccakf":"Pointer PE", "sg25_stage_unit":"Stages", "sg25_round_unit":"Whole round"}
units = [r for r in ppa if r["design"] in unit_names]
table("fpga_unit", [[unit_names[r["design"]],integer(r["lut"]),integer(r["ff"]),r["fmax_mhz"]] for r in units if r["technology"]=="fpga"])
table("asic_unit", [[unit_names[r["design"]],number(r["cell_area_um2"]),number(r["seq_area_um2"]),number(r["fmax_mhz"],1)] for r in units if r["technology"]=="asic"])
macros["RoundLutEfficiency"] = number(round_gain*int(core["sg25_core_8w32",250]["lut"])/int(core["kround25_core_8w32",250]["lut"]))
macros["StageLutOverhead"] = percent(int(core["sg25_core_8w32",250]["lut"])/int(core["base_core_8w32",250]["lut"])-1)
macros["StageFfOverhead"] = percent(int(core["sg25_core_8w32",250]["ff"])/int(core["base_core_8w32",250]["ff"])-1)

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
