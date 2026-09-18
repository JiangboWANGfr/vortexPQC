#!/usr/bin/env python3
"""Archive routed RV32IM K/D NTT core results from Vivado reports."""

import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build32_im/hw/syn/xilinx/dut"
CASES = [
    ("shared_ntt", "v80_rv32im_ntt_shared_inputpipe_core", False),
    ("shared_ntt_stage", "v80_rv32im_ntt_shared_stage_core", True),
]
FIELDS = [
    "variant", "xlen", "warps", "threads", "target_mhz", "f_enabled",
    "d_enabled", "stage_enabled", "total_luts", "ffs", "bram_tiles",
    "dsps", "wns_ns", "fmax_mhz", "ntt_hier_luts", "ntt_hier_ffs",
    "ntt_hier_dsps", "routing_errors", "status", "rtl_sha256",
    "summary_sha256", "util_sha256", "timing_sha256", "route_sha256",
    "report_dir",
]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hierarchy(report, instance):
    for line in report.splitlines():
        if not line.startswith("|"):
            continue
        fields = [part.strip() for part in line.split("|")[1:-1]]
        if fields and fields[0] == instance:
            return tuple(int(re.match(r"\d+", fields[index]).group())
                         for index in (2, 6, 10))
    raise AssertionError(instance)


rows = []
rtl_hash = digest(ROOT / "hw/rtl/pqc/VX_pqc_nttmul.sv")
for variant, dirname, stage in CASES:
    directory = BUILD / dirname
    paths = {name: directory / name for name in
             ("synth_summary.csv", "post_impl_util.rpt", "timing.rpt", "route.rpt")}
    with paths["synth_summary.csv"].open() as stream:
        summary, = list(csv.DictReader(stream))
    assert summary["design"] == "impl_1" and summary["period_ns"] == "4.000"
    sources = (directory / "project_1/sources.txt").read_text()
    for define in ("VX_CFG_EXT_F_DISABLE", "VX_CFG_EXT_D_DISABLE",
                   "VX_CFG_EXT_NTT_ENABLE", "VX_CFG_NUM_WARPS=8",
                   "VX_CFG_NUM_THREADS=32"):
        assert f"+define+{define}" in sources, (variant, define)
    assert ("+define+VX_CFG_EXT_KSG25_ENABLE" in sources) == stage
    util = paths["post_impl_util.rpt"].read_text()
    core_luts, core_ffs, core_dsps = hierarchy(util, "VX_core_top")
    ntt_luts, ntt_ffs, ntt_dsps = hierarchy(util, "g_blocks[0].nttmul_unit")
    assert (core_luts, core_ffs, core_dsps) == tuple(
        int(summary[key]) for key in ("lut", "ff", "dsp"))
    timing = paths["timing.rpt"].read_text()
    match = re.search(r"^Slack \((MET|VIOLATED)\) :\s+([-+]?\d+\.\d+)ns", timing, re.M)
    assert match and abs(float(match[2]) - float(summary["wns_ns"])) < 0.001
    route = paths["route.rpt"].read_text()
    errors = int(re.search(r"# of nets with routing errors\.+ :\s+(\d+)", route).group(1))
    assert errors == 0
    rows.append(dict(zip(FIELDS, [
        variant, 32, 8, 32, 250, 0, 0, int(stage), core_luts, core_ffs,
        summary["bram"], core_dsps, summary["wns_ns"], summary["fmax_mhz"],
        ntt_luts, ntt_ffs, ntt_dsps, errors,
        "met" if match[1] == "MET" else "timing_unmet", rtl_hash,
        *(digest(paths[name]) for name in paths), str(directory.relative_to(ROOT)),
    ])))

with Path(__file__).with_name("shared_ntt_ppa.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
