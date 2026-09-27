#!/usr/bin/env python3
"""Measure parameter scaling on the resident 200-MHz V80 image."""

import argparse
import csv
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time
import zipfile


REPO = Path(__file__).resolve().parents[3]
BUILD = REPO / "build32_pqc_parameters"
BOARD = REPO / "build32_v80_pqc_hw"
SLASH = REPO.parent / "slash"
OUT = Path(__file__).resolve().parent / "parameter_board_200mhz"
VBIN = BOARD / "hw/syn/xilinx/aved/v80_pqc_w8t32_200_id16_wt_multiout_aved_hw/bin/vortex_afu.vbin"
SETS = ("K512", "K768", "K1024", "D44", "D65", "D87")
CELLS = ((1, 1), (2, 2), (4, 4), (8, 1), (8, 2), (8, 4), (8, 8),
         (16, 8), (32, 8), (64, 8))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(text, parameter, batch, workers):
    assert text.rstrip().endswith("PASSED!")
    assert f"SCHEDULING: requests={batch} workers={workers} waves={batch // workers}" in text
    if parameter.startswith("K"):
        assert f"PARAMETER_SET: ML-KEM-{parameter[1:]}" in text
        assert text.count("pk/sk/ct/ss_enc/ss_dec match byte-for-byte") == batch
    else:
        assert text.startswith(f"ML-DSA-{parameter[1:]} ")
        assert text.count("pk/sk/signature match portable C byte-for-byte") == batch
        assert len(re.findall(r"ARENA: peak=\d+ of \d+ fail=0", text)) == batch
    cycles, = re.findall(r"^BATCH:.*makespan=(\d+)", text, re.M)
    instructions, device_cycles = re.findall(r"PERF: instrs=(\d+), cycles=(\d+)", text)[0]
    return int(cycles), int(instructions), int(device_cycles)


def main(full_mapping=False):
    assert VBIN.exists()
    assert Path.cwd() == BUILD, 'Run from the selected configured build tree'
    manifest = json.loads((BUILD / "parameter_sweep/manifest.json").read_text())
    assert bool(manifest.get('full_mapping')) == full_mapping
    audit = json.loads((BUILD / 'parameter_sweep/opcode_audit.json').read_text())
    if full_mapping:
        assert all(app.get('software_mapping_macros') for app in audit.values())

    def arms(batch, workers):
        return 'ABCDE' if full_mapping and (batch, workers) in ((1, 1), (8, 8)) else 'AE'
    libraries = (SLASH / "vrt/vrtd/build/libvrtd/src", SLASH / "vrt/build/lib",
                 BOARD / "sw/runtime")
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

    OUT.mkdir(exist_ok=True)
    planned = [(parameter, arm, batch, workers) for parameter in SETS
               for batch, workers in CELLS for arm in arms(batch, workers)]
    (OUT / 'plan.json').write_text(json.dumps(dict(full_mapping=full_mapping,
        cells=planned, warmups=1, repetitions=5), indent=2) + '\n')
    before = clock()
    image_sha = digest(VBIN)
    previous = json.loads((OUT.parent / "dsa65_ablation_200mhz/summary.json").read_text())
    assert image_sha == previous["vbin_sha256"]
    runtime = {
        str(path.relative_to(REPO.parent)): digest(path)
        for path in (BOARD / "sw/runtime/libvortex.so", BOARD / "sw/runtime/libvortex-aved.so",
                     SLASH / "vrt/build/lib/libvrt.so.1.0.0",
                     SLASH / "vrt/vrtd/build/libvrtd/src/libvrtd.so.1.0.0")
    }
    apps = {}
    for parameter in SETS:
        for arm in ('ABCDE' if full_mapping else 'AE'):
            name = f"{parameter}_{arm}"
            app = manifest["apps"][name]
            folder = BUILD / "tests/pqc" / ("parameters_" + name)
            assert digest(folder / "kernel.vxbin") == app["kernel_sha256"]
            assert digest(folder / app["project"]) == app["host_sha256"]
            apps[name] = dict(folder=folder, project=app["project"],
                              kernel_sha256=app["kernel_sha256"],
                              host_sha256=app["host_sha256"])

    rows = []
    for parameter in SETS:
        for batch, workers in CELLS:
            for repeat in range(6):
                for arm in arms(batch, workers):
                    name = f"{parameter}_{arm}"
                    app = apps[name]
                    label = "warmup" if repeat == 0 else f"r{repeat}"
                    stem = f"{name}_m{batch}_w{workers}_{label}"
                    log = OUT / (stem + ".log")
                    state_path = OUT / (stem + ".json")
                    command = ["./" + app["project"], f"-b{batch}", f"-w{workers}",
                               "-t32" if parameter.startswith("K") else "-s1"]
                    identity = dict(parameter=parameter, arm=arm, batch=batch, workers=workers,
                                    repeat=repeat, command=command,
                                    kernel_sha256=app["kernel_sha256"],
                                    host_sha256=app["host_sha256"], image_sha256=image_sha,
                                    runtime=runtime)
                    if state_path.exists() and log.exists():
                        old = json.loads(state_path.read_text())
                        if all(old.get(key) == value for key, value in identity.items()) \
                                and old.get("log_sha256") == digest(log):
                            cycles, instructions, device_cycles = validate(
                                log.read_text(), parameter, batch, workers)
                            assert (cycles, instructions, device_cycles) == (
                                old["cycles"], old["instructions"], old["device_cycles"])
                            print("SKIP", stem, flush=True)
                            if repeat:
                                rows.append(dict(parameter=parameter, arm=arm, requests=batch,
                                                 resident=workers, input_start=1, driver="aved",
                                                 repeat=repeat, cycles=cycles,
                                                 instructions=instructions,
                                                 device_cycles=device_cycles,
                                                 log_sha256=old["log_sha256"], log=log.name))
                            continue
                    start = time.monotonic()
                    with log.open("w") as stream:
                        result = subprocess.run(command, cwd=app["folder"], env=env,
                                                stdout=stream, stderr=subprocess.STDOUT,
                                                timeout=180)
                    assert result.returncode == 0, log
                    cycles, instructions, device_cycles = validate(
                        log.read_text(), parameter, batch, workers)
                    state = dict(identity, cycles=cycles, instructions=instructions,
                                 device_cycles=device_cycles, elapsed_seconds=time.monotonic() - start,
                                 log_sha256=digest(log))
                    state_path.write_text(json.dumps(state, indent=2) + "\n")
                    print("PASS", stem, cycles, flush=True)
                    if repeat:
                        rows.append(dict(parameter=parameter, arm=arm, requests=batch,
                                         resident=workers, input_start=1, driver="aved",
                                         repeat=repeat, cycles=cycles,
                                         instructions=instructions,
                                         device_cycles=device_cycles,
                                         log_sha256=state["log_sha256"], log=log.name))
                    (OUT / 'progress.json').write_text(json.dumps(dict(
                        planned_measurements=5 * len(planned), completed_measurements=len(rows),
                        last_pass=stem, updated_unix=time.time()), indent=2) + '\n')

    after = clock()
    with (OUT / "raw_runs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    medians = []
    for parameter in SETS:
        for batch, workers in CELLS:
            for arm in arms(batch, workers):
                group = [r for r in rows if (r["parameter"], r["arm"], r["requests"], r["resident"])
                         == (parameter, arm, batch, workers)]
                assert len(group) == 5
                cycles = int(statistics.median(r["cycles"] for r in group))
                medians.append(dict(parameter=parameter, arm=arm, requests=batch,
                                    resident=workers, input_start=1, driver="aved", cycles=cycles,
                                    cycles_per_request=cycles / batch,
                                    requests_per_mcycle=batch * 1e6 / cycles))
    with (OUT / "pqc_parameters_runs.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=medians[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(medians)
    call_hashes = {}
    for parameter in SETS:
        for batch, workers in CELLS:
            hashes = set()
            for arm in arms(batch, workers):
                for repeat in range(1, 6):
                    text = (OUT / f"{parameter}_{arm}_m{batch}_w{workers}_r{repeat}.log").read_text()
                    pattern = (r"^COUNTERS:.*$" if parameter.startswith("K") else
                               r"^(?:keccak_f1600_x[14]|poly_ntt|poly_invntt|rej_uniform|poly_pointwise|pointwise_acc_l5)\s+\d+$")
                    calls = re.findall(pattern, text, re.M)
                    assert len(calls) == batch * (1 if parameter.startswith("K") else 7)
                    hashes.add(hashlib.sha256("\n".join(calls).encode()).hexdigest())
            assert len(hashes) == 1, (parameter, batch, workers)
            call_hashes[f"{parameter}_m{batch}_w{workers}"] = hashes.pop()
    summary = dict(board_clock_hz_before=before, board_clock_hz_after=after,
                   image_path=str(VBIN.relative_to(REPO)), image_sha256=image_sha,
                   note="Resident image reused without programming; VRT metadata cannot attest its PDI identity.",
                   repetitions=5, warmups=1, cells=len(medians), runtime=runtime,
                   full_mapping=full_mapping,
                   call_count_sha256=call_hashes,
                   apps={name: {key: value for key, value in app.items() if key != "folder"}
                         for name, app in apps.items()})
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    archive = OUT / "parameter_board_sources.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for path in sorted(OUT.glob("*.log")) + sorted(OUT.glob("*.json")) + \
                    sorted(OUT.glob("*.csv")):
            zipped.write(path, path.name)
        zipped.write(Path(__file__), Path(__file__).name)
        zipped.write(BUILD / "parameter_sweep/manifest.json", "manifest.json")
        zipped.write(BUILD / "parameter_sweep/opcode_audit.json", "opcode_audit.json")
        if full_mapping:
            for relative, expected in manifest['sources'].items():
                path = BUILD / 'parameter_sweep/source' / relative
                assert digest(path) == expected, path
                zipped.write(path, 'source/' + relative)
        for name, app in apps.items():
            for path in (app["folder"] / app["project"], app["folder"] / "kernel.vxbin",
                         app["folder"] / 'config.stamp', app["folder"] / 'kernel.dump'):
                zipped.write(path, name + "/" + path.name)
    with zipfile.ZipFile(archive) as zipped:
        assert zipped.testzip() is None
    print("COMPLETE", len(rows), "measured runs", len(medians), "cells", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--full-mapping', action='store_true')
    args = parser.parse_args()
    if args.full_mapping:
        BUILD = REPO / 'build32_pqc_parameters_full_wt'
        OUT = OUT.parent / 'parameter_full_board_200mhz'
    with (BOARD / 'pqc_board.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        main(args.full_mapping)
