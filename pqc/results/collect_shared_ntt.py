#!/usr/bin/env python3
"""Archive matched ML-KEM and ML-DSA NTT measurements from raw logs."""

import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN = ROOT / "build32_im/mldsa_ntt_shared"
OUT = Path(__file__).with_name("shared_ntt_xrt.csv")
rtl_config = (RUN / "runtime/xrtsim_config.stamp").read_text()
assert "-DVX_CFG_EXT_F_DISABLE" in rtl_config
assert "-DVX_CFG_EXT_D_DISABLE" in rtl_config
FIELDS = [
    "scheme", "ntt", "keccak", "driver", "requests", "first_input",
    "keypair_cycles", "operation", "operation_cycles", "final_operation",
    "final_cycles", "total_cycles", "retired_instructions", "launch_cycles",
    "result", "kernel_vxbin_sha256", "runtime_sha256", "log_sha256", "raw_log",
]
CASES = [
    ("K", "software", "simx", "mlkem_sw", "mlkem_sw_simx_m1.log"),
    ("K", "software", "xrt", "mlkem_sw", "mlkem_sw_inputpipe_xrt_m1.log"),
    ("K", "shared", "simx", "mlkem", "mlkem_simx_m1.log"),
    ("K", "shared", "xrt", "mlkem", "mlkem_inputpipe_xrt_m1.log"),
    ("D", "software", "simx", "sw_ntt", "sw_ntt_simx_m1.log"),
    ("D", "software", "xrt", "sw_ntt", "sw_ntt_inputpipe_xrt_m1.log"),
    ("D", "shared", "simx", "hw_ntt", "mldsa_d_simx_m1.log"),
    ("D", "shared", "xrt", "hw_ntt", "hw_ntt_inputpipe_xrt_m1.log"),
]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find(pattern, content):
    match = re.search(pattern, content, re.MULTILINE)
    assert match is not None, pattern
    return match


rows = []
for scheme, ntt, driver, binary_dir, log_name in CASES:
    log = RUN / log_name
    content = log.read_text()
    assert content.endswith("PASSED!\n")
    assert f"driver={driver}" in content and "M=1 L=32" in content
    assert "warps=8 threads=32" in content and "l2=0 l3=0" in content
    if scheme == "K":
        assert "pk/sk/ct/ss_enc/ss_dec match byte-for-byte" in content
        assert "keccak=pe" in content and "ntt=reg32" in content
        expected = "nttmul=k nttbf=sg2" if ntt == "shared" else "nttmul=c nttbf=shfl"
        assert expected in content
        operation, final_operation = "encaps", "decaps"
    else:
        assert "pk/sk/signature match portable C byte-for-byte" in content
        expected = "nttmul=d nttbf=d" if ntt == "shared" else "nttmul=c nttbf=shuffle"
        assert "keccak=pe" in content and "ntt=reg32" in content and expected in content
        operation, final_operation = "sign", "verify"
    cycles = find(rf"^CYCLES: keypair=(\d+) {operation}=(\d+) "
                  rf"{final_operation}=(\d+) total=(\d+)$", content)
    perf = find(r"^PERF: instrs=(\d+), cycles=(\d+), IPC=", content)
    parts = [int(cycles.group(i)) for i in range(1, 5)]
    assert sum(parts[:3]) == parts[3]
    runtime = RUN / "runtime" / ("libxrtsim.so" if driver == "xrt" else "libsimx.so")
    rows.append(dict(zip(FIELDS, [
        scheme, ntt, "pointer", driver, 1, 0, parts[0], operation, parts[1],
        final_operation, parts[2], parts[3], int(perf.group(1)),
        int(perf.group(2)), "PASS", sha256(RUN / binary_dir / "kernel.vxbin"),
        sha256(runtime), sha256(log), str(log.relative_to(ROOT)),
    ])))

assert len({r["runtime_sha256"] for r in rows if r["driver"] == "xrt"}) == 1
for scheme in ("K", "D"):
    for ntt in ("software", "shared"):
        model = next(r for r in rows if (r["scheme"], r["ntt"], r["driver"])
                     == (scheme, ntt, "simx"))
        rtl = next(r for r in rows if (r["scheme"], r["ntt"], r["driver"])
                   == (scheme, ntt, "xrt"))
        assert model["kernel_vxbin_sha256"] == rtl["kernel_vxbin_sha256"]
        assert model["retired_instructions"] == rtl["retired_instructions"]
        gap = abs(model["launch_cycles"] - rtl["launch_cycles"])
        assert gap / rtl["launch_cycles"] < 0.05
with OUT.open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
