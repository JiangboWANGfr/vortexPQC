#!/usr/bin/env python3
"""Measure correct baseline-A primitive intervals on the resident V80 image."""

import argparse
import csv
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import time
import zipfile


REPO = Path(__file__).resolve().parents[3]
BUILD = REPO / "build32_pqc_board_profile"
BOARD = REPO / "build32_v80_pqc_hw"
SLASH = REPO.parent / "slash"
VBIN = BOARD / "hw/syn/xilinx/aved/v80_pqc_w8t32_200_id16_wt_multiout_aved_hw/bin/vortex_afu.vbin"
KINDS = ("permute", "ntt", "intt", "other")
OPERATIONS = {"K768": ("keypair", "encaps", "decaps"),
              "D65": ("keypair", "sign", "verify")}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def fields(line):
    return {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)\b", line)}


def parse_log(text, parameter, profiled, expected_requests=None):
    """Validate complete requests; detailed residual excludes only three primitives."""
    assert text.rstrip().endswith("PASSED!"), "missing successful completion"
    assert "ABLATION BUILD" not in text and "***" not in text
    schedule, = re.findall(r"^SCHEDULING: (.*)$", text, re.M)
    schedule = fields(schedule)
    count = schedule["requests"]
    assert schedule["workers"] == 1 and schedule["waves"] == count
    assert expected_requests is None or count == expected_requests
    if parameter == "K768":
        assert count == 1 and "PARAMETER_SET: ML-KEM-768" in text
        assert "KAT: id=0 pk/sk/ct/ss_enc/ss_dec match byte-for-byte" in text
        blocks = [(0, 0, text)]
    else:
        assert parameter == "D65" and "ML-DSA-65 " in text
        matches = list(re.finditer(r"^REQUEST: id=(\d+) input=(\d+)$", text, re.M))
        assert len(matches) == count
        blocks = [(int(m[1]), int(m[2]), text[m.end():matches[i + 1].start()
                  if i + 1 < len(matches) else len(text)]) for i, m in enumerate(matches)]
    records = []
    for request, input_id, block in blocks:
        assert request == len(records)
        assert "keccak=sg25_sw" in block and "ntt=reg32 nttmul=c" in block
        assert ("nttbf=shfl" if parameter == "K768" else "nttbf=shuffle pointwise=c") in block
        line, = re.findall(r"^CYCLES: (.*)$", block, re.M)
        timings = fields(line)
        assert timings["total"] == sum(timings[op] for op in OPERATIONS[parameter])
        assert all(timings[op] > 0 for op in OPERATIONS[parameter])
        if parameter == "K768":
            line, = re.findall(r"^COUNTERS: (.*)$", block, re.M)
            calls = fields(line)
            assert calls.pop("id") == request
        else:
            assert "REFERENCE: pk/sk/signature match portable C byte-for-byte" in block
            assert re.search(r"ARENA: peak=\d+ of \d+ fail=0\b", block)
            calls = {name: int(value) for name, value in re.findall(
                r"^(keccak_f1600_x[14]|poly_ntt|poly_invntt|rej_uniform|poly_pointwise|pointwise_acc_l5)\s+(\d+)$",
                block, re.M)}
            assert len(calls) == 7
        details = re.findall(r"^DETAIL: (.*)$", block, re.M)
        assert len(details) == (3 if profiled else 0)
        operations = {op: {"total": timings[op]} for op in OPERATIONS[parameter]}
        seen = set()
        for line in details:
            phase, = re.findall(r"\bphase=(\w+)", line)
            assert phase in operations and phase not in seen
            seen.add(phase)
            detail = fields(line)
            assert detail.get("id", request) == request
            assert detail["total"] == timings[phase]
            values = {kind: detail[kind] for kind in KINDS[:3]}
            values["other"] = detail["total"] - sum(values.values())
            assert values["other"] >= 0
            operations[phase].update(values)
        if profiled and parameter == "K768":
            cumulative = {name: int(value) for name, value in re.findall(
                r"^PHASE_PROFILE: id=0 phase=(\w+) cycles=(\d+)\b", block, re.M)}
            for kind, name in (("permute", "keccak_permute"), ("ntt", "ntt"), ("intt", "intt")):
                assert sum(op[kind] for op in operations.values()) == cumulative[name]
        records.append(dict(id=request, input=input_id, calls=calls,
                            total=timings["total"], operations=operations))
    return records


def summarize(measured):
    result = {}
    for parameter in OPERATIONS:
        runs = [run for run in measured if run["parameter"] == parameter]
        result[parameter] = {"inputs": [], "profiles": {}}
        for request in range(len(runs[0]["requests"])):
            pair = {mode: [run["requests"][request]["total"] for run in runs
                           if run["mode"] == mode] for mode in ("control", "profile")}
            assert all(len(values) == 5 for values in pair.values())
            control, profile = (statistics.median(pair[mode]) for mode in ("control", "profile"))
            result[parameter]["inputs"].append(dict(
                input=runs[0]["requests"][request]["input"], control_median_cycles=control,
                profile_median_cycles=profile, overhead_pct=100 * (profile / control - 1)))
        profiled = [request for run in runs if run["mode"] == "profile"
                    for request in run["requests"]]
        for scope in ("complete", *OPERATIONS[parameter]):
            intervals = [op for request in profiled for name, op in request["operations"].items()
                         if scope == "complete" or name == scope]
            total = sum(op["total"] for op in intervals)
            cycles = {kind: sum(op[kind] for op in intervals) for kind in KINDS}
            assert sum(cycles.values()) == total
            result[parameter]["profiles"][scope] = dict(
                total_cycles=total, cycles=cycles,
                shares_pct={kind: 100 * value / total for kind, value in cycles.items()})
    return result


def write_intervals(path, measured):
    with Path(path).open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("parameter", "mode", "repeat", "input_id", "operation",
                         "total_cycles", *(kind + "_cycles" for kind in KINDS),
                         "log", "log_sha256"))
        for run in measured:
            for request in run["requests"]:
                for operation, values in request["operations"].items():
                    writer.writerow((run["parameter"], run["mode"], run["repeat"],
                        request["input"], operation, values["total"],
                        *(values.get(kind, "") for kind in KINDS),
                        run["log"], run["log_sha256"]))


def build_apps():
    assert Path.cwd() == BUILD, "Run from build32_pqc_board_profile"
    reference_path = REPO / "pqc/docs/dac_paper2/data/full_mapping_build.json"
    reference = json.loads(reference_path.read_text())
    env = dict(os.environ, LIBC_PATH=str(REPO.parent / "toolchains-im/libc32"),
               LIBCRT_PATH=str(REPO.parent / "toolchains-im/libcrt32"))
    tool_directory = env.get("TOOLDIR", str(Path.home() / "tools-pqc-v3.0.1"))
    commands = [[str(REPO / "configure"), "--xlen=32", f"--tooldir={tool_directory}"],
                ["make", "-C", "sw/kernel", "-j2", "CONFIGS=" + reference["flags"]]]
    for index, command in enumerate(commands):
        with (BUILD / f"profile_setup_{index}.log").open("w") as log:
            subprocess.run(command, cwd=BUILD, env=env, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
    shutil.copy2(BOARD / "sw/runtime/libvortex.so", BUILD / "sw/runtime/libvortex.so")
    sources = {relative for relative in reference["sources"]
               if relative.startswith(("tests/", "sw/kernel/", "VX_"))}
    sources.update(("configure", "config.mk.in", "tests/pqc/common.mk", "tests/regression/common.mk"))
    for directory in ("tests/pqc/mlkem_profile", "tests/pqc/mldsa_profile",
                      "tests/pqc/mlkem_width", "sw/kernel/include",
                      "pqc/third_party/mlkem-native", "pqc/third_party/mldsa-native"):
        sources.update(str(path.relative_to(REPO)) for path in
                       (REPO / directory).rglob("*")
                       if path.is_file() and path.suffix in (".c", ".cpp", ".h", ".S"))
    source_hashes = {relative: digest(REPO / relative) for relative in sorted(sources)}
    manifest = dict(flags=reference["flags"], apps={}, sources=source_hashes,
        reference_manifest_sha256=digest(reference_path), setup_commands=commands,
        build_environment={key: env[key] for key in ("LIBC_PATH", "LIBCRT_PATH")},
        link_runtime_sha256=digest(BUILD / "sw/runtime/libvortex.so"))
    for parameter in OPERATIONS:
        base = reference["apps"][parameter + "_A"]
        for mode in ("control", "profile"):
            name = f"{parameter}_A_{mode}"
            folder = BUILD / "tests/pqc" / name
            folder.mkdir(exist_ok=True)
            shutil.copy2(BUILD / "tests/pqc" / base["project"] / "Makefile", folder / "Makefile")
            command = ["make", "-j2", "CONFIGS=" + reference["flags"], *base["options"]]
            if mode == "profile":
                command.append("PROFILE_PHASES=primitives" if parameter == "K768" else "PROFILE_PHASES=1")
            with (BUILD / f"build_{name}.log").open("w") as log:
                subprocess.run(command, cwd=folder, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True)
            app = dict(folder=str(folder), project=base["project"], command=command,
                       kernel_sha256=digest(folder / "kernel.vxbin"),
                       host_sha256=digest(folder / base["project"]))
            if mode == "control":
                assert app["kernel_sha256"] == base["kernel_sha256"], name
                assert app["host_sha256"] == base["host_sha256"], name
            manifest["apps"][name] = app
            print("BUILT", name, flush=True)
    assert all(digest(REPO / path) == value for path, value in source_hashes.items())
    write_json(BUILD / "profile_build.json", manifest)


def run_board():
    assert Path.cwd() == BUILD, "Run from build32_pqc_board_profile"
    build_path = BUILD / "profile_build.json"
    build = json.loads(build_path.read_text())
    sources = build["sources"]
    for relative, expected in sources.items():
        assert digest(REPO / relative) == expected, relative
    for app in build["apps"].values():
        folder = Path(app["folder"])
        assert digest(folder / "kernel.vxbin") == app["kernel_sha256"]
        assert digest(folder / app["project"]) == app["host_sha256"]
    libraries = (SLASH / "vrt/vrtd/build/libvrtd/src", SLASH / "vrt/build/lib",
                 BOARD / "sw/runtime")
    env = dict(os.environ, LD_LIBRARY_PATH=":".join(map(str, libraries)),
               VORTEX_DRIVER="aved", VRT_DEVICE_BDF="02:00", VRT_VBIN_PATH=str(VBIN),
               VORTEX_AVED_NO_PROGRAM="1", VORTEX_PROFILING="0")
    runtime = {str(path): digest(path) for path in (
        BOARD / "sw/runtime/libvortex.so", BOARD / "sw/runtime/libvortex-aved.so",
        SLASH / "vrt/build/lib/libvrt.so.1.0.0",
        SLASH / "vrt/vrtd/build/libvrtd/src/libvrtd.so.1.0.0")}
    image_sha = digest(VBIN)
    previous = json.loads((Path(__file__).parent / "dsa65_ablation_200mhz/summary.json").read_text())
    assert image_sha == previous["vbin_sha256"]

    def clock():
        value = subprocess.check_output([str(SLASH / "smi/build/src/v80-smi"), "debug",
            "clockwiz", "-d", "02:00", "--get", "--region", "user"], env=env, text=True).strip()
        assert value == "200000000", value
        return int(value)

    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = BUILD / "profile_board" / stamp
    out.mkdir(parents=True, exist_ok=False)
    manifest = dict(build=build, build_manifest_sha256=digest(build_path), runtime=runtime,
        driver="aved", image_path=str(VBIN), image_sha256=image_sha,
        runner_sha256=digest(__file__), warmups=1, repetitions=5,
        requests={"K768": 1, "D65": 32}, resident_workers=1, dsa_input_start=1,
        environment={key: env[key] for key in ("LD_LIBRARY_PATH", "VORTEX_DRIVER",
            "VRT_DEVICE_BDF", "VRT_VBIN_PATH", "VORTEX_AVED_NO_PROGRAM", "VORTEX_PROFILING")},
        scope="Per-request API intervals, not batch makespan; shares sum all five measured repetitions.",
        instrumented_boundaries={
            "K768": "Keccak: 24 rounds with state in lane registers; NTT/INTT: transform dispatch intervals.",
            "D65": "Keccak: permutation call including dispatch and state load/store; NTT/INTT: transform calls.",
            "shares": "Instrumented request denominators; Keccak excludes absorb/squeeze; no overhead subtraction.",
            "overhead": "Incremental cost of added phase probes; baseline counters and DSA pointwise timers are retained in both modes."},
        image_note="Resident image reused without programming; metadata cannot attest its PDI identity.")
    write_json(out / "manifest.json", manifest)
    measured, expected_calls = [], {}
    with (BOARD / "pqc_board.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        manifest["clock_before_hz"] = clock()
        write_json(out / "manifest.json", manifest)
        for parameter, count in manifest["requests"].items():
            for repeat in range(6):
                for mode in ("control", "profile"):
                    app = build["apps"][f"{parameter}_A_{mode}"]
                    command = ["./" + app["project"], f"-b{count}", "-w1",
                               "-t32" if parameter == "K768" else "-s1"]
                    label = "warmup" if repeat == 0 else f"r{repeat}"
                    log = out / f"{parameter}_{mode}_{label}.log"
                    before = clock()
                    start = time.monotonic()
                    with log.open("w") as stream:
                        completed = subprocess.run(command, cwd=app["folder"], env=env,
                            stdout=stream, stderr=subprocess.STDOUT, timeout=600)
                    assert completed.returncode == 0, log
                    after = clock()
                    content = log.read_text()
                    requests = parse_log(content, parameter, mode == "profile", count)
                    assert "CONFIG: driver=aved clusters=1 cores=1 warps=8 threads=32" in content
                    for index, request in enumerate(requests):
                        assert request["input"] == (index + 1 if parameter == "D65" else 0)
                        key = (parameter, request["input"])
                        assert expected_calls.setdefault(key, request["calls"]) == request["calls"], key
                    run = dict(parameter=parameter, mode=mode, repeat=repeat, command=command,
                        cwd=app["folder"], requests=requests, clock_before_hz=before,
                        clock_after_hz=after, elapsed_seconds=time.monotonic() - start,
                        log=log.name, log_sha256=digest(log))
                    write_json(log.with_suffix(".json"), run)
                    if repeat:
                        measured.append(run)
                    print("PASS", log.name, flush=True)
        manifest["clock_after_hz"] = clock()
    for path, expected in runtime.items():
        assert digest(path) == expected, path
    assert digest(VBIN) == image_sha and digest(build_path) == manifest["build_manifest_sha256"]
    write_json(out / "manifest.json", manifest)
    write_json(out / "summary.json", summarize(measured))
    write_intervals(out / "intervals.csv", measured)
    with zipfile.ZipFile(out / "baseline_profile_sources.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.glob("*.log")) + sorted(out.glob("*.json")):
            archive.write(path, path.name)
        archive.write(out / "intervals.csv", "intervals.csv")
        archive.write(__file__, Path(__file__).name)
        for relative, expected in sources.items():
            assert digest(REPO / relative) == expected, relative
            archive.write(REPO / relative, "source/" + relative)
        for name, app in build["apps"].items():
            folder = Path(app["folder"])
            assert digest(folder / "kernel.vxbin") == app["kernel_sha256"]
            assert digest(folder / app["project"]) == app["host_sha256"]
            for filename in (app["project"], "kernel.vxbin", "config.stamp"):
                archive.write(folder / filename, name + "/" + filename)
    print("COMPLETE", out, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--build", action="store_true")
    mode.add_argument("--run", action="store_true")
    mode.add_argument("--check-logs", nargs="+", type=Path)
    args = parser.parse_args()
    if args.build:
        build_apps()
    elif args.run:
        run_board()
    else:
        for path in args.check_logs:
            text = path.read_text()
            parameter = "K768" if "PARAMETER_SET: ML-KEM-768" in text else "D65"
            records = parse_log(text, parameter, bool(re.search(r"^DETAIL:", text, re.M)))
            print(json.dumps(dict(log=str(path), parameter=parameter, requests=records)))
