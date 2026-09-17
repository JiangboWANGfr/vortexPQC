#!/usr/bin/env python3
"""Build paper figures and tables from the archived measurement snapshots."""

import csv
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

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


def save(name):
    plt.savefig(OUT / name, bbox_inches="tight", pad_inches=0.025)
    plt.close()


plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 8,
    "axes.labelsize": 8, "axes.titlesize": 8, "legend.fontsize": 7,
    "xtick.labelsize": 7, "ytick.labelsize": 7,
    "axes.spines.top": False, "axes.spines.right": False,
    "pdf.fonttype": 42, "ps.fonttype": 42,
})
colors = ["#778899", "#315c85", "#00867d", "#d28527", "#873e75", "#748342"]
kem = rows("keccak_ntt_unified_xrt.csv")
assert len(kem) == 12 and all(r["result"] == "PASS" for r in kem)
assert len({r["xrtsim_sha256"] for r in kem}) == 1
backends = ["sg1_serial", "pqrv_asm", "sg25_sw", "sg25_stages", "kround25", "pointer_keccakf"]
labels = ["Scalar C", "PQRV", "Shuffle", "Stage", "Round", "Pointer"]
cycles = {(b, m): int(one(kem, backend=b, requests=m)["device_cycles"])
          for b in backends for m in (1, 8)}
fig, axs = plt.subplots(1, 2, figsize=(7.05, 1.9))
for ax, m in zip(axs, (1, 8)):
    vals = [cycles[b, m] / 1e6 for b in backends]
    bars = ax.bar(np.arange(6), vals, color=colors, width=0.7)
    ax.bar_label(bars, labels=[f"{v:.2f}" for v in vals], fontsize=7, padding=2)
    ax.set_xticks(np.arange(6), labels, rotation=22, ha="right")
    ax.set_ylim(0, max(vals) * 1.2)
    ax.set_ylabel("Whole-launch cycles (million)")
    ax.set_title(f"({'a' if m == 1 else 'b'}) M = {m} request{'s' if m > 1 else ''}; lower is better")
    ax.grid(axis="y", alpha=0.17)
    ax.set_axisbelow(True)
fig.tight_layout(pad=0.4, w_pad=1.5)
save("kem_xrt.pdf")

profile = rows("mlkem_phase_profile.csv")
names = ["stage", "whole_round", "pointer"]
phase_names = ["Permute", "Absorb", "Squeeze", "NTT/INTT", "Poly. arith.", "Other"]
phase_colors = ["#d28527", "#315c85", "#58a2c0", "#00867d", "#873e75", "#dadde1"]
fig, ax = plt.subplots(figsize=(3.42, 1.9))
for y, name in enumerate(names):
    r = one(profile, measurement="phase", backend=name, driver="xrt", requests=1)
    vals = [int(r["keccak_permute_cycles"]), int(r["keccak_absorb_cycles"]),
            int(r["keccak_squeeze_cycles"]), int(r["ntt_cycles"]) + int(r["intt_cycles"]),
            sum(int(r[k]) for k in ["mulcache_cycles", "basemul_cycles", "reduce_cycles"]),
            int(r["other_cycles"])]
    total = int(r["interval_or_makespan_cycles"])
    assert sum(vals) == total
    left = 0
    for i, v in enumerate(vals):
        percent = 100 * v / total
        ax.barh(y, percent, left=left, height=0.56, color=phase_colors[i], label=phase_names[i] if y == 0 else None)
        if i == 5:
            ax.text(left + percent / 2, y, f"{percent:.1f}%", ha="center", va="center", fontsize=7)
        left += percent
ax.set_yticks(range(3), ["Stage", "Round", "Pointer"])
ax.invert_yaxis()
ax.set_xlim(0, 100)
ax.set_xlabel("Share of instrumented request interval (%)")
ax.legend(loc="lower center", bbox_to_anchor=(0.43, 1.0), ncol=3, frameon=False, columnspacing=0.8, handlelength=1)
fig.tight_layout(pad=0.4)
save("phase_profile.pdf")

ctrl = rows("keccak_w32_controls.csv")
fig, ax = plt.subplots(figsize=(3.42, 1.8))
xx = np.arange(2)
for i, (arm, label, color) in enumerate(zip(["stage_loop", "stage_unrolled", "kround"], ["Stage loop", "Stage expanded", "Round expanded"], colors[2:5])):
    vals = [int(one(ctrl, arm=arm, driver="xrt", batch=m, states_per_warp=1,
                    permutations_per_state=64)["span_cycles"]) / (64 * m) for m in (1, 8)]
    bars = ax.bar(xx + (i - 1) * 0.24, vals, width=0.23, label=label, color=color)
    ax.bar_label(bars, labels=[f"{v:.0f}" for v in vals], padding=2, fontsize=7)
ax.set_xticks(xx, ["1 active warp", "8 active warps"])
ax.set_ylabel("Cycles / completed permutation")
ax.set_ylim(0, 2650)
ax.legend(frameon=False, fontsize=6.7, loc="upper right")
ax.grid(axis="y", alpha=0.17)
ax.set_axisbelow(True)
fig.tight_layout(pad=0.4)
save("granularity.pdf")

ppa = rows("rv32im_rv64im_keccak_ppa.csv")
table = []
for variant, label in [("ntt_only", "NTT only"), ("ntt_stage", "NTT + Stage"), ("ntt_kround", "NTT + Round")]:
    r = one(ppa, xlen=32, variant=variant)
    table.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & {r['dsps']} & {float(r['wns_ns']):+.3f} \\")
(OUT / "ppa_rows.tex").write_text("\n".join(s + "\\" for s in table) + "\n")

baseline = one(ppa, xlen=32, variant="ntt_only")
stage = one(ppa, xlen=32, variant="ntt_stage")
round_unit = one(ppa, xlen=32, variant="ntt_kround")
numbers = {
    "StageVsShuffleOne": cycles["sg25_sw", 1] / cycles["sg25_stages", 1],
    "StageVsShuffleEight": cycles["sg25_sw", 8] / cycles["sg25_stages", 8],
    "RoundVsShuffleOne": cycles["sg25_sw", 1] / cycles["kround25", 1],
    "RoundVsShuffleEight": cycles["sg25_sw", 8] / cycles["kround25", 8],
    "RoundVsPqrvOne": cycles["pqrv_asm", 1] / cycles["kround25", 1],
    "RoundVsPqrvEight": cycles["pqrv_asm", 8] / cycles["kround25", 8],
    "RoundVsPointerOne": cycles["pointer_keccakf", 1] / cycles["kround25", 1],
    "PointerVsRoundEight": cycles["kround25", 8] / cycles["pointer_keccakf", 8],
    "StageLutPct": 100 * (int(stage["total_luts"]) / int(baseline["total_luts"]) - 1),
    "RoundLutPct": 100 * (int(round_unit["total_luts"]) / int(baseline["total_luts"]) - 1),
    "StageFfPct": 100 * (int(stage["ffs"]) / int(baseline["ffs"]) - 1),
    "RoundFfPct": 100 * (int(round_unit["ffs"]) / int(baseline["ffs"]) - 1),
}
(OUT / "numbers.tex").write_text("\n".join(f"\\newcommand{{\\{k}}}{{{v:.3f}}}" for k, v in numbers.items()) + "\n")
(OUT / "source_manifest.json").write_text(json.dumps({
    "repository_source_commit": "084f79465",
    "mlkem_native_commit": "1d7b486c4db3bbc620b009d05e4f69eca9e68d22",
    "measurements": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(DATA.glob("*.csv"))},
    "derived_numbers": numbers,
}, indent=2) + "\n")
