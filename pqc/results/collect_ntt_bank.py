#!/usr/bin/env python3
"""Validate and archive the matched NTT multiplier-bank experiments."""

import argparse
import csv
import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build32_ntt_bank"
OUT = Path(__file__).resolve().parent
BANKS = (16, 8, 4, 2, 1)
PPA_BANKS = (16, 8, 4, 2, 1)
NTTBF_BUILDS = {
    16: ROOT / "build32_ntt_bank16_ci",
    8: ROOT / "build32_ntt_bank_ci",
    4: ROOT / "build32_ntt_bank4_ci",
    2: ROOT / "build32_ntt_bank2_ci",
    1: ROOT / "build32_ntt_bank1_ci",
}


def build_dir(bank, xlen=32):
    return ROOT / f"build{xlen}_ntt_bank{bank if bank < 8 else ''}"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def match(pattern, content):
    found = re.search(pattern, content, re.MULTILINE)
    assert found, pattern
    return found


def write_csv(name, rows):
    with (OUT / name).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def performance():
    manifests = {bank: json.loads((build_dir(bank) / "run_manifest.json").read_text())
                 for bank in BANKS}
    for manifest in manifests.values():
        for name, expected in manifest["files"].items():
            assert digest(ROOT / name) == expected, name
    for xlen in (32, 64):
        for bank in BANKS:
            for kind in ("kd", "d"):
                path = build_dir(bank, xlen) / f"unit_{kind}_{bank}.log"
                content = path.read_text()
                assert "PASSED" in content and "%Error" not in content, path
    for bank in BANKS:
        for name in ("simx_config.stamp", "xrtsim_config.stamp"):
            content = (build_dir(bank) / f"bank{bank}/runtime" / name).read_text()
            for flag in ("EXT_F_DISABLE", "EXT_D_DISABLE", "EXT_PQC_ENABLE", "EXT_NTT_ENABLE",
                         "EXT_KSG25_ENABLE", "EXT_KROUND25_ENABLE", "NUM_WARPS=8",
                         "NUM_THREADS=32", f"NTT_MUL_LANES={bank}"):
                assert f"-DVX_CFG_{flag} " in content, (bank, name, flag)
    rows = []
    for scheme in ("mlkem", "mldsa"):
        for bank in BANKS:
            directory = build_dir(bank) / f"bank{bank}"
            app = build_dir(bank) / f"tests/pqc/{scheme}_bank{bank}"
            for batch in (1, 8):
                for driver in ("simx", "xrt"):
                    stem = f"{scheme}_m{batch}_{driver}"
                    log = directory / f"{stem}.log"
                    status = json.loads((directory / f"{stem}.json").read_text())
                    assert status["state"] == "finished" and status["exit_code"] == 0, log
                    content = log.read_text()
                    assert content.endswith("PASSED!\n"), log
                    assert f"driver={driver} clusters=1 cores=1 warps=8 threads=32 M={batch} L=32" in content
                    makespan = int(match(rf"^BATCH: requests={batch} .*makespan=(\d+)$", content)[1])
                    instructions, cycles = map(int, match(
                        r"^PERF: instrs=(\d+), cycles=(\d+),", content).groups())
                    if scheme == "mlkem":
                        for req in range(batch):
                            assert f"KAT: id={req} pk/sk/ct/ss_enc/ss_dec match byte-for-byte" in content
                            assert f"STATUS: id={req} keypair=0 encaps=0 decaps=0" in content
                            assert f"ARITH_ARM: id={req} mulcache=w32 basemul=w32 reduce=w32 mul=ise profile=0" in content
                        calls = re.findall(r"^COUNTERS: id=\d+ (.*)$", content, re.M)
                    else:
                        blocks = re.findall(r"^REQUEST:.*?(?=^REQUEST:|^BATCH:)", content, re.M | re.S)
                        assert len(blocks) == batch
                        calls = []
                        for req, block in enumerate(blocks):
                            assert f"REQUEST: id={req} input={req + (batch != 1)}\n" in block
                            assert "REFERENCE: pk/sk/signature match portable C byte-for-byte" in block
                            assert "ARM: keccak=pe unroll=0 ntt=reg32 nttmul=d nttbf=d pointwise=ise l5=w32 ablate=none" in block
                            peak, capacity = map(int, match(r"ARENA: peak=(\d+) of (\d+) fail=0$", block).groups())
                            assert peak <= capacity
                            phases = tuple(map(int, match(r"^CYCLES: keypair=(\d+) sign=(\d+) verify=(\d+) total=(\d+)$", block).groups()))
                            assert sum(phases[:3]) == phases[3] <= makespan
                            calls.append(tuple(int(match(rf"^{name}\s+(\d+)$", block)[1]) for name in (
                                "keccak_f1600_x1", "keccak_f1600_x4", "poly_ntt",
                                "poly_invntt", "poly_pointwise", "pointwise_acc_l5")))
                    assert len(calls) == batch
                    runtime = directory / "runtime" / ("libsimx.so" if driver == "simx" else "libxrtsim.so")
                    rows.append(dict(
                        scheme=scheme, multipliers=bank, requests=batch, driver=driver,
                        makespan_cycles=makespan, instructions=instructions, device_cycles=cycles,
                        model_gap_pct="", change_vs16_pct="", change_vs8_pct="", result="PASS",
                        calls_sha256=hashlib.sha256(repr(calls).encode()).hexdigest(),
                        kernel_sha256=digest(app / "kernel.vxbin"),
                        host_sha256=digest(app / f"{scheme}_profile"),
                        runtime_sha256=digest(runtime), log_sha256=digest(log),
                        rtl_sha256=manifests[bank]["files"]["hw/rtl/pqc/VX_pqc_nttmul.sv"],
                        raw_log=str(log.relative_to(ROOT))))
    index = {(r["scheme"], r["multipliers"], r["requests"], r["driver"]): r for r in rows}
    for row in rows:
        rtl = index[row["scheme"], row["multipliers"], row["requests"], "xrt"]
        baseline = index[row["scheme"], 16, row["requests"], row["driver"]]
        bank8 = index[row["scheme"], 8, row["requests"], row["driver"]]
        for field in ("instructions", "calls_sha256", "kernel_sha256", "host_sha256"):
            assert row[field] == rtl[field] == baseline[field], (row, field)
        for field in ("makespan_cycles", "device_cycles"):
            gap = abs(row[field] / rtl[field] - 1) * 100
            assert gap <= 5, (row, field, gap)
        row["model_gap_pct"] = f'{abs(row["device_cycles"] / rtl["device_cycles"] - 1) * 100:.6f}'
        row["change_vs16_pct"] = f'{(row["makespan_cycles"] / baseline["makespan_cycles"] - 1) * 100:.6f}'
        row["change_vs8_pct"] = f'{(row["makespan_cycles"] / bank8["makespan_cycles"] - 1) * 100:.6f}'
        print(row["scheme"], row["multipliers"], row["requests"], row["driver"],
              row["makespan_cycles"], "change vs16", row["change_vs16_pct"],
              "% vs8", row["change_vs8_pct"], "%")
    write_csv("ntt_multiplier_bank_performance.csv", rows)


def hierarchy(report, instance):
    for line in report.splitlines():
        fields = [part.strip() for part in line.split("|")[1:-1]]
        if fields and fields[0] == instance:
            return tuple(int(re.match(r"\d+", fields[index])[0]) for index in (2, 6, 10))
    raise AssertionError(instance)


def nttbf():
    rows = []
    for bank in BANKS:
        log = NTTBF_BUILDS[bank] / "parity.log"
        content = log.read_text()
        case = "model_parity-nttbf_k" + ("" if bank == 16 else f"_bank{bank}")
        result = match(
            rf"^PARITY: pqc:{case}:rtlsim: instrs simx=(\d+) rtlsim=(\d+), "
            rf"cycles simx=(\d+) rtlsim=(\d+), gap=([\d.]+)% \(tolerance 5%\)$",
            content)
        simx_instrs, rtl_instrs, simx_cycles, rtl_cycles = map(int, result.groups()[:4])
        assert simx_instrs == rtl_instrs == 59534
        assert content.count("NTTBF.K: vectors=1044 outputs=334080 mismatches=0") == 2
        assert f"VX_CFG_NTT_MUL_LANES={bank}" in content
        gap = abs(simx_cycles / rtl_cycles - 1) * 100
        assert gap <= 5 and abs(gap - float(result[5])) < 0.01
        rows.append(dict(
            multipliers=bank, instructions=rtl_instrs, simx_cycles=simx_cycles,
            rtl_cycles=rtl_cycles, change_vs16_pct="", model_gap_pct=f"{gap:.6f}",
            result="PASS", log_sha256=digest(log), raw_log=str(log.relative_to(ROOT))))
    baseline = rows[0]["rtl_cycles"]
    for row in rows:
        row["change_vs16_pct"] = f'{(row["rtl_cycles"] / baseline - 1) * 100:.6f}'
        print(row["multipliers"], "multipliers NTTBF.K:", row["rtl_cycles"],
              "cycles, change vs16", row["change_vs16_pct"], "%")
    write_csv("ntt_multiplier_bank_nttbf.csv", rows)


def ppa():
    rows = []
    generated = []
    for bank in PPA_BANKS:
        directory = build_dir(bank) / f"hw/syn/xilinx/dut/v80_rv32im_ntt_bank{bank}_core"
        paths = {name: directory / name for name in
                 ("synth_summary.csv", "post_impl_util.rpt", "timing.rpt", "route.rpt")}
        with paths["synth_summary.csv"].open() as stream:
            summary, = list(csv.DictReader(stream))
        assert summary["design"] == "impl_1" and summary["period_ns"] == "4.000"
        sources = (directory / "project_1/sources.txt").read_text()
        for define in ("VX_CFG_EXT_F_DISABLE", "VX_CFG_EXT_D_DISABLE", "VX_CFG_EXT_NTT_ENABLE",
                       "VX_CFG_NUM_WARPS=8", "VX_CFG_NUM_THREADS=32", f"VX_CFG_NTT_MUL_LANES={bank}"):
            assert f"+define+{define}" in sources, (bank, define)
        command = json.loads((directory / "command.json").read_text())
        assert command["env"]["CLK_FREQ_MHZ"] == "250" and command["env"]["OPT_LEVEL"] == "3"
        util = paths["post_impl_util.rpt"].read_text()
        core = hierarchy(util, "VX_core_top")
        ntt = hierarchy(util, "g_blocks[0].nttmul_unit")
        assert core == tuple(int(summary[key]) for key in ("lut", "ff", "dsp"))
        timing = match(r"^Slack \((MET|VIOLATED)\) :\s+([-+]?\d+\.\d+)ns", paths["timing.rpt"].read_text())
        assert abs(float(timing[2]) - float(summary["wns_ns"])) < 0.001
        errors = int(match(r"# of nets with routing errors\.+ :\s+(\d+)", paths["route.rpt"].read_text())[1])
        assert errors == 0
        rtl = directory / "project_1/src/VX_pqc_nttmul.sv"
        assert rtl.is_file(), rtl
        generated.append(re.sub(r"localparam NUM_MULTIPLIERS = .*?;",
                                "localparam NUM_MULTIPLIERS = BANK;", rtl.read_text()))
        rows.append(dict(
            multipliers=bank, xlen=32, warps=8, threads=32, target_mhz=250, f_enabled=0,
            d_enabled=0, total_luts=core[0], ffs=core[1], bram_tiles=summary["bram"],
            dsps=core[2], ntt_luts=ntt[0], ntt_ffs=ntt[1], ntt_dsps=ntt[2],
            wns_ns=summary["wns_ns"], routing_errors=errors,
            status="met" if timing[1] == "MET" else "timing_unmet",
            generated_rtl_sha256=digest(rtl),
            **{name.replace(".rpt", "").replace(".csv", "") + "_sha256": digest(path)
               for name, path in paths.items()},
            report_dir=str(directory.relative_to(ROOT))))
        print(bank, "multipliers:", core, "NTT:", ntt, "WNS:", timing[2])
    assert all(source == generated[0] for source in generated), "PPA sources differ beyond bank size"
    write_csv("ntt_multiplier_bank_ppa.csv", rows)


def archive_sources():
    paths = [Path(__file__), OUT / "ntt_multiplier_bank_performance.csv",
             OUT / "ntt_multiplier_bank_nttbf.csv",
             OUT / "ntt_multiplier_bank_ppa.csv",
             ROOT / "docs/proposals/ntt_multiplier_bank_proposal.md"]
    paths.extend(ROOT / name for name in (
        "VX_config.toml", "hw/rtl/pqc/VX_pqc_nttmul.sv", "sim/simx/alu_unit.cpp",
        "sim/simx/alu_unit.h", "hw/unittest/pqc_unit/sim/VX_pqc_nttmul_tb.sv",
        "hw/unittest/pqc_unit/sim/VX_pqc_nttmul_d_tb.sv", "ci/testcases/pqc.yaml"))
    for build in (BUILD, build_dir(4), build_dir(2), build_dir(1)):
        paths.extend(build / name for name in (
            "build_banks.py", "build_apps.py", "run_performance.py",
            "run_manifest.json", "run_performance.log", "configure.log",
            "build_kernel.log", "sw_sim_boundary.log", "catalog_lint.log"))
        paths.append(ROOT / f"{build.name}_ci/parity.log")
    paths.extend(BUILD / name for name in ("run_ppa.py", "run_ppa.log"))
    paths.extend(BUILD / name for name in
                 ("catalog_lint_ppa.log", "sw_sim_boundary_ppa.log"))
    paths.extend(NTTBF_BUILDS[16] / name for name in
                 ("parity.log", "configure.log", ".config.stamp"))
    paths.extend(build_dir(4) / name for name in (
        "run_ppa.py", "run_ppa.log", "ppa_deferred.json",
        "run_ppa_sweep.py", "run_ppa_sweep.log"))
    paths.extend(build_dir(bank) / "configure_ppa.log" for bank in (4, 2, 1))
    for bank in (4, 2, 1):
        paths.extend(build_dir(bank) / name for name in ("run_units.py", "run_units.log"))
    for bank in (2, 1):
        paths.extend(build_dir(bank) / name for name in ("run_pipeline.py", "run_pipeline.log"))
    paths.extend(build_dir(1) / name for name in
                 ("run_mixed_regression.py", "run_mixed_regression.log"))
    for xlen in (32, 64):
        paths.append(build_dir(1, xlen) / "unit_kd_1_initial_coverage.log")
    for bank in BANKS:
        for xlen in (32, 64):
            paths.append(build_dir(bank, xlen) / f"unit_kd_{bank}.log")
            paths.append(build_dir(bank, xlen) / f"unit_d_{bank}.log")
        directory = build_dir(bank) / f"bank{bank}"
        paths.extend(directory.glob("*.log"))
        paths.extend(directory.glob("*.json"))
        paths.extend(directory / "runtime" / name for name in
                     ("simx_config.stamp", "xrtsim_config.stamp"))
        for scheme in ("mlkem", "mldsa"):
            app = build_dir(bank) / f"tests/pqc/{scheme}_bank{bank}"
            paths.extend(app / name for name in ("Makefile", "kernel.vxbin", f"{scheme}_profile"))
    report_dirs = [build_dir(bank) / f"hw/syn/xilinx/dut/v80_rv32im_ntt_bank{bank}_core"
                   for bank in PPA_BANKS]
    report_dirs.append(ROOT / "build32_im/hw/syn/xilinx/dut/v80_rv32im_ntt_shared_inputpipe_core")
    for directory in report_dirs:
        paths.extend(directory / name for name in (
            "build.log", "synth_summary.csv", "post_synth_util.rpt", "post_impl_util.rpt",
            "timing.rpt", "route.rpt", "drc.rpt", "project_1/sources.txt",
            "project_1/src/VX_pqc_nttmul.sv"))
        if (directory / "command.json").exists():
            paths.append(directory / "command.json")
    stopped = build_dir(4) / "hw/syn/xilinx/dut/v80_rv32im_ntt_bank4_stopped_core"
    paths.extend(stopped / name for name in
                 ("build.log", "command.json", "project_1/sources.txt"))
    destination = OUT / "ntt_multiplier_bank_sources.zip"
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(set(paths)):
            archive.write(path, path.relative_to(ROOT))
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
    print("Archived", len(set(paths)), "files in", destination.relative_to(ROOT))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("part", choices=("performance", "nttbf", "ppa", "all"))
    args = parser.parse_args()
    if args.part in ("performance", "all"):
        performance()
    if args.part in ("nttbf", "all"):
        nttbf()
    if args.part in ("ppa", "all"):
        ppa()
    if args.part == "all":
        archive_sources()
