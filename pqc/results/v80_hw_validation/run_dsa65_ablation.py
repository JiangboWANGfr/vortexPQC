#!/usr/bin/env python3
"""Measure the matched full-RAM ML-DSA-65 arms on the resident 200 MHz V80 image."""

import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


BUILD = Path.cwd()
REPO = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent / "dsa65_ablation_200mhz"
VBIN = BUILD / "hw/syn/xilinx/aved/v80_pqc_w8t32_200_id16_wt_multiout_aved_hw/bin/vortex_afu.vbin"
SLASH = REPO.parent / "slash"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    assert BUILD == REPO / "build32_v80_pqc_hw"
    assert VBIN.exists()
    OUT.mkdir(exist_ok=True)
    libraries = [SLASH / "vrt/vrtd/build/libvrtd/src", SLASH / "vrt/build/lib",
                 BUILD / "sw/runtime"]
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, libraries)),
               VORTEX_DRIVER="aved", VRT_DEVICE_BDF="02:00", VRT_VBIN_PATH=str(VBIN),
               VORTEX_AVED_NO_PROGRAM="1", VORTEX_PROFILING="0")
    smi = SLASH / "smi/build/src/v80-smi"

    def clock():
        value = subprocess.check_output(
            [str(smi), "debug", "clockwiz", "-d", "02:00", "--get", "--region", "user"],
            env=env, text=True).strip()
        assert value == "200000000", value
        return int(value)

    summary = {
        "clock_hz_before": clock(),
        "vbin_path": str(VBIN.relative_to(REPO)),
        "vbin_sha256": digest(VBIN),
        "source_head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip(),
        "note": "Resident image reused without programming; VRT metadata cannot attest its PDI identity.",
        "arms": {}, "runs": [],
    }
    for arm in "ABCDE":
        folder = BUILD / "tests/pqc" / ("v80_dsa65_" + arm)
        summary["arms"][arm] = {
            "folder": str(folder.relative_to(REPO)),
            "kernel_sha256": digest(folder / "kernel.vxbin"),
            "host_sha256": digest(folder / "mldsa_profile"),
            "config_sha256": digest(folder / "config.stamp"),
        }

    for batch in (1, 8):
        for repeat in range(6):
            for arm in "ABCDE":
                folder = BUILD / "tests/pqc" / ("v80_dsa65_" + arm)
                log = OUT / f"{arm}_m{batch}_{'warmup' if repeat == 0 else 'r' + str(repeat)}.log"
                start = time.time()
                with log.open("w") as stream:
                    result = subprocess.run(
                        ["./mldsa_profile", f"-b{batch}", f"-w{batch}", "-s3"],
                        cwd=folder, env=env, stdout=stream, stderr=subprocess.STDOUT)
                data = log.read_text()
                assert result.returncode == 0 and data.rstrip().endswith("PASSED!"), log
                assert data.count("pk/sk/signature match portable C byte-for-byte") == batch, log
                assert f"SCHEDULING: requests={batch} workers={batch} waves=1" in data, log
                assert len(re.findall(r"^REQUEST: id=\d+ input=\d+", data, re.M)) == batch, log
                cycles = int(re.search(r"^BATCH:.*makespan=(\d+)", data, re.M).group(1))
                if repeat:
                    summary["runs"].append({
                        "arm": arm, "batch": batch, "repeat": repeat, "cycles": cycles,
                        "wall_seconds": time.time() - start, "log": log.name,
                        "log_sha256": digest(log),
                    })
                    print("PASS", arm, batch, repeat, cycles, flush=True)
    summary["clock_hz_after"] = clock()
    summary["medians"] = {
        str(batch): {
            arm: statistics.median(r["cycles"] for r in summary["runs"]
                                   if r["batch"] == batch and r["arm"] == arm)
            for arm in "ABCDE"
        } for batch in (1, 8)
    }
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary["medians"], indent=2))


if __name__ == "__main__":
    main()
