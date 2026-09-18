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
phase_records = []
for i, backend in enumerate(["stage", "whole_round", "pointer"]):
    r = one(profile, measurement="phase", backend=backend, driver="xrt", requests=1)
    parts = [int(r["keccak_permute_cycles"]), int(r["keccak_absorb_cycles"]),
             int(r["keccak_squeeze_cycles"]), int(r["ntt_cycles"]) + int(r["intt_cycles"]),
             sum(int(r[k]) for k in ["mulcache_cycles", "basemul_cycles", "reduce_cycles"]),
             int(r["other_cycles"])]
    total = int(r["interval_or_makespan_cycles"])
    assert sum(parts) == total and r["kat"] == "PASS"
    phase_records.append([i] + [100 * v / total for v in parts])
table("profile.dat", ["index", "perm", "absorb", "squeeze", "ntt", "poly", "other"], phase_records)

ppa = rows("rv32im_rv64im_keccak_ppa.csv")
ppa_lines = []
for variant, label in [("ntt_only", "NTT only"), ("ntt_stage", "+ Stage"), ("ntt_kround", "+ Round")]:
    r = one(ppa, xlen=32, variant=variant)
    ppa_lines.append(f"{label} & {int(r['total_luts']):,} & {int(r['ffs']):,} & {r['dsps']} & {float(r['wns_ns']):+.3f} " + r"\\")
(OUT / "ppa_rows.tex").write_text("\n".join(ppa_lines) + "\n")
base = one(ppa, xlen=32, variant="ntt_only")
stage = one(ppa, xlen=32, variant="ntt_stage")
round_unit = one(ppa, xlen=32, variant="ntt_kround")
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
    "AllStageOne": stage_cost["000", 1] / stage_cost["111", 1],
    "AllStageEight": stage_cost["000", 8] / stage_cost["111", 8],
    "FusionOne": fusion["stage_unrolled", 1] / fusion["kround", 1],
    "FusionEight": fusion["stage_unrolled", 8] / fusion["kround", 8],
}
for m, name in [(1, "One"), (8, "Eight")]:
    s = one(kem_controls, arm="stage_unrolled", driver="simx", batch=m)
    r = one(kem_controls, arm="kround", driver="simx", batch=m)
    numbers["FusionKem" + name] = int(s["device_cycles"]) / int(r["device_cycles"])
table("fusion_gain.dat", ["index", "perm", "kem"],
      [(i, numbers["Fusion" + name], numbers["FusionKem" + name])
       for i, name in enumerate(["One", "Eight"])])
(OUT / "numbers.tex").write_text("\n".join(f"\\newcommand{{\\{k}}}{{{v:.3f}}}" for k, v in numbers.items()) + "\n")
(OUT / "source_manifest.json").write_text(json.dumps({
    "repository_source_commit": "084f79465",
    "mlkem_native_commit": "1d7b486c4db3bbc620b009d05e4f69eca9e68d22",
    "measurements": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(DATA.glob("*.csv"))},
    "derived_numbers": numbers,
}, indent=2) + "\n")
