#!/usr/bin/env python3
"""Validate independent, write-through RV32IM core routes at 200 MHz."""

import csv
import hashlib
from pathlib import Path
import re
import zipfile


REPO = Path(__file__).resolve().parents[3]
BUILDS = REPO / "build32_pqc_200_ppa/hw/syn/xilinx/dut"
OUT = Path(__file__).resolve().parent / "core_ppa_200mhz.csv"
VARIANTS = (
    (16, "base"), (16, "stage"), (16, "round"), (16, "pointer"),
    (8, "base"), (4, "base"), (2, "base"), (1, "base"), (2, "stage"),
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hierarchy(report, instance):
    for line in report.splitlines():
        fields = [part.strip() for part in line.split("|")[1:-1]]
        if fields and fields[0] == instance:
            return tuple(int(re.match(r"\d+", fields[index]).group())
                         for index in (2, 6, 10))
    raise AssertionError(instance)


def main():
    rows = []
    normalized_rtl = []
    for bank, keccak in VARIANTS:
        directory = BUILDS / f"v80_rv32im_m{bank}_{keccak}_wt_200_core"
        summary_path = directory / "synth_summary.csv"
        util_path = directory / "post_impl_util.rpt"
        timing_path = directory / "timing.rpt"
        route_path = directory / "route.rpt"
        drc_path = (directory / "project_1/project_1.runs/impl_1/"
                    "route_report_drc_0.rpt")
        summary_timing_path = (directory / "project_1/project_1.runs/impl_1/"
                               "post_route_phys_opt_report_timing_summary_0.rpt")
        rtl_path = directory / "project_1/src/VX_pqc_nttmul.sv"
        sources = (directory / "project_1/sources.txt").read_text()
        util = util_path.read_text()
        assert "Vivado v.2025.1" in util and "Design State : Physopt postRoute" in util
        assert "xcv80-lsva4737-2MHP-e-S" in util
        with summary_path.open() as stream:
            summary, = list(csv.DictReader(stream))
        assert summary["design"] == "impl_1" and summary["period_ns"] == "5.000"
        for define in ("VX_CFG_XLEN=32", "VX_CFG_EXT_F_DISABLE", "VX_CFG_EXT_D_DISABLE",
                       "VX_CFG_EXT_NTT_ENABLE", "VX_CFG_DCACHE_WRITEBACK=0",
                       "VX_CFG_NUM_WARPS=8", "VX_CFG_NUM_THREADS=32",
                       f"VX_CFG_NTT_MUL_LANES={bank}"):
            assert f"+define+{define}" in sources, (directory, define)
        assert ("+define+VX_CFG_EXT_PQC_ENABLE" in sources) == (keccak == "pointer")
        assert ("+define+VX_CFG_EXT_KSG25_ENABLE" in sources) == (keccak == "stage")
        assert ("+define+VX_CFG_EXT_KROUND25_ENABLE" in sources) == (keccak == "round")
        core = hierarchy(util, "VX_core_top")
        ntt = hierarchy(util, "g_blocks[0].nttmul_unit")
        assert core == tuple(int(summary[key]) for key in ("lut", "ff", "dsp"))
        timing = re.search(r"^Slack \((MET|VIOLATED)\) :\s+([-+]?\d+\.\d+)ns",
                           timing_path.read_text(), re.M)
        assert timing and timing.group(1) == "MET"
        assert abs(float(timing.group(2)) - float(summary["wns_ns"])) < 0.001
        timing_summary = summary_timing_path.read_text()
        closure = re.search(r"^\s*([-+]?\d+\.\d+)\s+0\.000\s+0\s+\d+\s+"
                            r"([-+]?\d+\.\d+)\s+0\.000\s+0\s+\d+", timing_summary, re.M)
        assert closure and "All user specified timing constraints are met." in timing_summary
        assert abs(float(closure.group(1)) - float(summary["wns_ns"])) < 0.001
        assert float(closure.group(2)) >= 0
        route = re.search(r"# of nets with routing errors\.+ :\s+(\d+)",
                          route_path.read_text())
        assert route and int(route.group(1)) == 0
        drc = drc_path.read_text()
        assert "Design State : Fully Routed" in drc
        assert not re.search(r"^\|[^|]+\|\s*Error\s*\|", drc, re.M)
        normalized_rtl.append(re.sub(r"localparam NUM_MULTIPLIERS = .*?;",
                                     "localparam NUM_MULTIPLIERS = BANK;", rtl_path.read_text()))
        rows.append({
            "variant": f"m{bank}_{keccak}", "multipliers": bank, "keccak": keccak,
            "target_mhz": 200, "dcache_writeback": 0,
            "total_luts": core[0], "ffs": core[1], "dsps": core[2],
            "ntt_luts": ntt[0], "ntt_ffs": ntt[1], "ntt_dsps": ntt[2],
            "bram_tiles": summary["bram"], "wns_ns": summary["wns_ns"],
            "whs_ns": closure.group(2),
            "routing_errors": 0, "drc_errors": 0, "status": "met",
            "util_sha256": digest(util_path), "timing_sha256": digest(timing_path),
            "timing_summary_sha256": digest(summary_timing_path),
            "route_sha256": digest(route_path), "rtl_sha256": digest(rtl_path),
            "drc_sha256": digest(drc_path),
            "report_dir": str(directory.relative_to(REPO)),
        })
        print(rows[-1]["variant"], core, ntt, summary["wns_ns"])
    assert all(source == normalized_rtl[0] for source in normalized_rtl)
    with OUT.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    archive = OUT.with_name("core_ppa_200mhz_reports.zip")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        zipped.write(OUT, OUT.name)
        for row in rows:
            directory = REPO / row["report_dir"]
            for name in ("synth_summary.csv", "post_impl_util.rpt", "timing.rpt",
                         "route.rpt", "run.log", "Makefile"):
                zipped.write(directory / name, row["variant"] + "/" + name)
            zipped.write(directory / "project_1/project_1.runs/impl_1/"
                         "post_route_phys_opt_report_timing_summary_0.rpt",
                         row["variant"] + "/post_route_timing_summary.rpt")
            zipped.write(directory / "project_1/project_1.runs/impl_1/route_report_drc_0.rpt",
                         row["variant"] + "/post_route_drc.rpt")
            for name in ("sources.txt", "src/VX_pqc_nttmul.sv"):
                zipped.write(directory / "project_1" / name,
                             row["variant"] + "/project_1/" + name)
    with zipfile.ZipFile(archive) as zipped:
        assert zipped.testzip() is None


if __name__ == "__main__":
    main()
