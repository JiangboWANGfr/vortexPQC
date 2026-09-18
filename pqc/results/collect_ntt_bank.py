#!/usr/bin/env python3
"""Validate and archive the matched 16/eight-multiplier experiments."""

import argparse
import csv
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build32_ntt_bank"
OUT = Path(__file__).resolve().parent


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
    manifest = json.loads((BUILD / "run_manifest.json").read_text())
    for name, expected in manifest["files"].items():
        assert digest(ROOT / name) == expected, name
    for xlen in (32, 64):
        for bank in (8, 16):
            for kind in ("kd", "d"):
                path = ROOT / f"build{xlen}_ntt_bank/unit_{kind}_{bank}.log"
                content = path.read_text()
                assert "PASSED" in content and "%Error" not in content, path
    for bank in (8, 16):
        for name in ("simx_config.stamp", "xrtsim_config.stamp"):
            content = (BUILD / f"bank{bank}/runtime" / name).read_text()
            for flag in ("EXT_F_DISABLE", "EXT_D_DISABLE", "EXT_PQC_ENABLE", "EXT_NTT_ENABLE",
                         "EXT_KSG25_ENABLE", "EXT_KROUND25_ENABLE", "NUM_WARPS=8",
                         "NUM_THREADS=32", f"NTT_MUL_LANES={bank}"):
                assert f"-DVX_CFG_{flag} " in content, (bank, name, flag)
    rows = []
    for scheme in ("mlkem", "mldsa"):
        for bank in (16, 8):
            directory = BUILD / f"bank{bank}"
            app = BUILD / f"tests/pqc/{scheme}_bank{bank}"
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
                        model_gap_pct="", bank8_change_pct="", result="PASS",
                        calls_sha256=hashlib.sha256(repr(calls).encode()).hexdigest(),
                        kernel_sha256=digest(app / "kernel.vxbin"),
                        host_sha256=digest(app / f"{scheme}_profile"),
                        runtime_sha256=digest(runtime), log_sha256=digest(log),
                        rtl_sha256=manifest["files"]["hw/rtl/pqc/VX_pqc_nttmul.sv"],
                        raw_log=str(log.relative_to(ROOT))))
    index = {(r["scheme"], r["multipliers"], r["requests"], r["driver"]): r for r in rows}
    for row in rows:
        rtl = index[row["scheme"], row["multipliers"], row["requests"], "xrt"]
        baseline = index[row["scheme"], 16, row["requests"], row["driver"]]
        for field in ("instructions", "calls_sha256", "kernel_sha256", "host_sha256"):
            assert row[field] == rtl[field] == baseline[field], (row, field)
        for field in ("makespan_cycles", "device_cycles"):
            gap = abs(row[field] / rtl[field] - 1) * 100
            assert gap <= 5, (row, field, gap)
        row["model_gap_pct"] = f'{abs(row["device_cycles"] / rtl["device_cycles"] - 1) * 100:.6f}'
        row["bank8_change_pct"] = f'{(row["makespan_cycles"] / baseline["makespan_cycles"] - 1) * 100:.6f}'
        print(row["scheme"], row["multipliers"], row["requests"], row["driver"],
              row["makespan_cycles"], "change", row["bank8_change_pct"], "%")
    write_csv("ntt_multiplier_bank_performance.csv", rows)


def hierarchy(report, instance):
    for line in report.splitlines():
        fields = [part.strip() for part in line.split("|")[1:-1]]
        if fields and fields[0] == instance:
            return tuple(int(re.match(r"\d+", fields[index])[0]) for index in (2, 6, 10))
    raise AssertionError(instance)


def ppa():
    rows = []
    generated = []
    for bank in (16, 8):
        directory = BUILD / f"hw/syn/xilinx/dut/v80_rv32im_ntt_bank{bank}_core"
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
    assert generated[0] == generated[1], "PPA sources differ beyond bank size"
    write_csv("ntt_multiplier_bank_ppa.csv", rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("part", choices=("performance", "ppa", "all"))
    args = parser.parse_args()
    if args.part in ("performance", "all"):
        performance()
    if args.part in ("ppa", "all"):
        ppa()
