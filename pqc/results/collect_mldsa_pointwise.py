#!/usr/bin/env python3
"""Archive ML-DSA-65 pointwise ablation from byte-exact request logs."""

import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN = ROOT / "build32_im/mldsa_ntt_shared"
BIN = ROOT / "build32_im/mldsa_pointwise"
OUT = Path(__file__).with_name("mldsa_pointwise.csv")
CASES = [
    ("c", "simx", 1, 0),
    ("ise", "simx", 1, 0),
    ("l5", "simx", 1, 0),
    ("both", "simx", 1, 0),
    ("c", "xrt", 1, 0),
    ("both", "xrt", 1, 0),
    ("c", "simx", 8, 1),
    ("both", "simx", 8, 1),
    ("low", "simx", 1, 1),
]
FIELDS = [
    "arm", "driver", "requests", "request_id", "input_id", "keypair_cycles",
    "sign_cycles", "verify_cycles", "total_cycles", "simple_keypair_cycles",
    "l5_keypair_cycles", "simple_sign_cycles", "l5_sign_cycles",
    "simple_verify_cycles", "l5_verify_cycles", "simple_calls", "l5_calls",
    "makespan_cycles", "result", "kernel_sha256", "runtime_sha256",
    "log_sha256", "raw_log",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def match(pattern, source):
    found = re.search(pattern, source, re.MULTILINE)
    assert found, pattern
    return found


rows = []
for arm, driver, requests, first_input in CASES:
    log = RUN / f"pointwise_{arm}_{driver}_m{requests}.log"
    content = log.read_text()
    assert content.endswith("PASSED!\n") and f"driver={driver}" in content
    assert f"M={requests} L=32" in content
    blocks = re.findall(r"^REQUEST:.*?(?=^REQUEST:|^BATCH:)", content,
                        re.MULTILINE | re.DOTALL)
    assert len(blocks) == requests
    makespan = int(match(r"^BATCH: requests=(\d+) first_input=(\d+) makespan=(\d+)$",
                         content).group(3))
    assert f"BATCH: requests={requests} first_input={first_input}" in content
    binary = BIN / arm / "kernel.vxbin"
    runtime = RUN / "runtime" / ("libxrtsim.so" if driver == "xrt" else "libsimx.so")
    for req, block in enumerate(blocks):
        request = match(r"^REQUEST: id=(\d+) input=(\d+)$", block)
        assert int(request.group(1)) == req
        assert int(request.group(2)) == first_input + req
        assert "REFERENCE: pk/sk/signature match portable C byte-for-byte" in block
        assert "keccak=pe" in block and "ntt=reg32 nttmul=d nttbf=d" in block
        expected = {"c": ("c", "c"), "ise": ("ise", "c"),
                    "l5": ("c", "w32"), "both": ("ise", "w32"),
                    "low": ("ise", "w32")}[arm]
        if arm in ("c", "ise"):
            assert f"pointwise={expected[0]} ablate=none" in block
        else:
            assert f"pointwise={expected[0]} l5={expected[1]}" in block
        cycles = tuple(map(int, match(
            r"^CYCLES: keypair=(\d+) sign=(\d+) verify=(\d+) total=(\d+)$",
            block).groups()))
        assert sum(cycles[:3]) == cycles[3]
        phases = []
        for phase in ("keypair", "sign", "verify"):
            phase_match = match(
                rf"^POINTWISE: phase={phase} simple=(\d+) l5=(\d+) share=", block)
            phases.extend(map(int, phase_match.groups()))
        simple_calls = int(match(r"^poly_pointwise\s+(\d+)$", block).group(1))
        l5_calls = int(match(r"^pointwise_acc_l5\s+(\d+)$", block).group(1))
        assert simple_calls > 0 and (l5_calls == 0 if arm == "low" else l5_calls > 0)
        rows.append(dict(zip(FIELDS, [
            arm, driver, requests, req, first_input + req, *cycles, *phases,
            simple_calls, l5_calls, makespan, "PASS", digest(binary),
            digest(runtime), digest(log), str(log.relative_to(ROOT)),
        ])))

for arm in ("c", "both"):
    pair = [r for r in rows if r["arm"] == arm and r["requests"] == 1]
    assert len(pair) == 2 and pair[0]["kernel_sha256"] == pair[1]["kernel_sha256"]
    model = next(r for r in pair if r["driver"] == "simx")
    rtl = next(r for r in pair if r["driver"] == "xrt")
    assert abs(int(model["total_cycles"]) - int(rtl["total_cycles"])) / int(rtl["total_cycles"]) < 0.05
assert len({r["runtime_sha256"] for r in rows if r["driver"] == "xrt"}) == 1

with OUT.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
