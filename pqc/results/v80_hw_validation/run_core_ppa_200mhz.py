#!/usr/bin/env python3
"""Route the remaining matched RV32IM core variants at 200 MHz."""

import csv
import os
from pathlib import Path
import shutil
import subprocess


REPO = Path(__file__).resolve().parents[3]
BUILDS = REPO / "build32_pqc_200_ppa/hw/syn/xilinx/dut"
RUNS = ((2, "stage"), (2, "base"), (8, "base"), (4, "base"),
        (1, "base"), (16, "stage"), (16, "round"), (16, "pointer"))
COMMON = ("-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE "
          "-DVX_CFG_EXT_NTT_ENABLE -DVX_CFG_NUM_WARPS=8 "
          "-DVX_CFG_NUM_THREADS=32 -DVX_CFG_DCACHE_WRITEBACK=0")


def main():
    assert Path.cwd() == BUILDS
    assert (BUILDS / "v80_rv32im_m16_base_wt_200_core/synth_summary.csv").exists()
    env = dict(os.environ, XILINX_VIVADO="/data/Xilinx/2025.1/Vivado",
               CLK_FREQ_MHZ="200", OPT_LEVEL="3")
    for bank, keccak in RUNS:
        directory = BUILDS / f"v80_rv32im_m{bank}_{keccak}_wt_200_core"
        summary_path = directory / "synth_summary.csv"
        if summary_path.exists():
            with summary_path.open() as stream:
                summary, = list(csv.DictReader(stream))
            assert summary["period_ns"] == "5.000" and float(summary["wns_ns"]) >= 0
            assert (directory / "route.rpt").exists()
            print("SKIP", directory.name, flush=True)
            continue
        directory.mkdir(exist_ok=True)
        shutil.copy2(BUILDS / "build.mk", directory / "Makefile")
        configs = COMMON + f" -DVX_CFG_NTT_MUL_LANES={bank}"
        if keccak == "stage":
            configs += " -DVX_CFG_EXT_KSG25_ENABLE"
        elif keccak == "round":
            configs += " -DVX_CFG_EXT_KROUND25_ENABLE"
        elif keccak == "pointer":
            configs += " -DVX_CFG_EXT_PQC_ENABLE"
        command = ["make", "DUT=core", "DEVICE=xcv80-lsva4737-2MHP-e-S",
                   "MAX_JOBS=2", f"CONFIGS={configs}", "build"]
        print("START", directory.name, flush=True)
        with (directory / "run.log").open("w") as log:
            subprocess.run(command, cwd=directory, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        with summary_path.open() as stream:
            summary, = list(csv.DictReader(stream))
        assert summary["period_ns"] == "5.000" and float(summary["wns_ns"]) >= 0
        print("PASS", directory.name, summary["wns_ns"], flush=True)


if __name__ == "__main__":
    main()
