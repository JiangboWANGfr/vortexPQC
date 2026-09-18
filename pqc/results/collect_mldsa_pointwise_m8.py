#!/usr/bin/env python3
"""Archive matched ML-DSA-65 M8 pointwise runs and model agreement."""

import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN = ROOT / "build32_im/tests/pqc"
RUNTIME = ROOT / "build32_im/mldsa_ntt_shared/runtime"
OUT = Path(__file__).with_name("mldsa_pointwise_m8.csv")
MANIFEST = json.loads((RUNTIME.parent / "m8_run_manifest.json").read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def match(pattern, content):
    found = re.search(pattern, content, re.MULTILINE)
    assert found, pattern
    return found


rows = []
for arm in ("c", "both"):
    directory = RUN / f"mldsa_m8_{arm}"
    for driver in ("simx", "xrt"):
        log = directory / f"m8_{driver}.log"
        content = log.read_text()
        assert content.endswith("PASSED!\n"), log
        assert f"driver={driver} clusters=1 cores=1 warps=8 threads=32 M=8 L=32" in content
        batch = match(r"^BATCH: requests=8 first_input=1 makespan=(\d+)$", content)
        perf = match(r"^PERF: instrs=(\d+), cycles=(\d+),", content)
        blocks = re.findall(r"^REQUEST:.*?(?=^REQUEST:|^BATCH:)", content,
                            re.MULTILINE | re.DOTALL)
        assert len(blocks) == 8
        expected = "pointwise=c l5=c" if arm == "c" else "pointwise=ise l5=w32"
        runtime = RUNTIME / ("libsimx.so" if driver == "simx" else "libxrtsim.so")
        hashes = {
            "kernel_sha256": digest(directory / "kernel.vxbin"),
            "host_sha256": digest(directory / "mldsa_profile"),
            "runtime_sha256": digest(runtime),
            "log_sha256": digest(log),
            "raw_log": str(log.relative_to(ROOT)),
        }
        for path in (directory / "kernel.vxbin", directory / "mldsa_profile", runtime):
            assert digest(path) == MANIFEST["files"][str(path.relative_to(ROOT))], path
        for req, block in enumerate(blocks):
            assert f"REQUEST: id={req} input={req + 1}\n" in block
            assert "REFERENCE: pk/sk/signature match portable C byte-for-byte" in block
            assert f"ntt=reg32 nttmul=d nttbf=d {expected} ablate=none" in block
            assert "ARM: keccak=pe unroll=0" in block
            arena = match(r"ARENA: peak=(\d+) of (\d+) fail=0$", block)
            assert int(arena.group(1)) <= int(arena.group(2))
            cycles = tuple(map(int, match(
                r"^CYCLES: keypair=(\d+) sign=(\d+) verify=(\d+) total=(\d+)$",
                block).groups()))
            assert sum(cycles[:3]) == cycles[3] <= int(batch.group(1))
            row = dict(zip(("keypair_cycles", "sign_cycles", "verify_cycles", "total_cycles"), cycles))
            for name in ("keccak_f1600_x1", "keccak_f1600_x4", "poly_ntt",
                         "poly_invntt", "poly_pointwise", "pointwise_acc_l5"):
                row[name + "_calls"] = int(match(rf"^{name}\s+(\d+)$", block).group(1))
            # Full-RAM K=6 uses six L5 calls in keypair, verify, and each attempt.
            l5_calls = row["pointwise_acc_l5_calls"]
            assert l5_calls >= 18 and l5_calls % 6 == 0
            row["sign_attempts_from_l5"] = l5_calls // 6 - 2
            row.update(arm=arm, driver=driver, requests=8, request_id=req, input_id=req + 1,
                       makespan_cycles=int(batch.group(1)), instructions=int(perf.group(1)),
                       device_cycles=int(perf.group(2)), result="PASS", **hashes)
            rows.append(row)

for arm in ("c", "both"):
    model = [r for r in rows if r["arm"] == arm and r["driver"] == "simx"]
    rtl = [r for r in rows if r["arm"] == arm and r["driver"] == "xrt"]
    for left, right in zip(model, rtl):
        for field in ("kernel_sha256", "host_sha256", "input_id", "instructions"):
            assert left[field] == right[field], (arm, field)
        for field in left:
            if field.endswith("_calls"):
                assert left[field] == right[field], (arm, field, left["input_id"])
    for field in ("device_cycles", "makespan_cycles"):
        gap = abs(model[0][field] / rtl[0][field] - 1) * 100
        assert gap <= 5, (arm, field, gap)
        print(f"{arm}: {field} model gap {gap:.3f}%")

for driver in ("simx", "xrt"):
    control = [r for r in rows if r["arm"] == "c" and r["driver"] == driver]
    mapped = [r for r in rows if r["arm"] == "both" and r["driver"] == driver]
    for left, right in zip(control, mapped):
        for field in left:
            if field.endswith("_calls"):
                assert left[field] == right[field], (driver, field, left["input_id"])
    saved = (1 - mapped[0]["makespan_cycles"] / control[0]["makespan_cycles"]) * 100
    print(f"{driver}: M8 makespan reduction {saved:.3f}%")

with OUT.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
