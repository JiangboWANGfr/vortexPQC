#!/usr/bin/env python3

import hashlib
import json
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RESULT_DIR = ROOT / "pqc/results/v80_hw_validation"

ARMS = {
    "A": {
        "description": "software Keccak + software NTT",
        "kernel": "build32_v80_pqc_hw/tests/pqc/v80_variants/mlkem768_keccak_sw_ntt_sw/kernel.vxbin",
    },
    "B": {
        "description": "Stage Keccak + software NTT",
        "kernel": "build32_v80_pqc_hw/tests/pqc/v80_variants/mlkem768_keccak_hw_ntt_sw/kernel.vxbin",
    },
    "C": {
        "description": "software Keccak + hardware NTT",
        "kernel": "build32_v80_pqc_hw/tests/pqc/v80_variants/mlkem768_keccak_sw_ntt_hw/kernel.vxbin",
    },
    "D": {
        "description": "Stage Keccak + hardware NTT",
        "kernel": "build32_v80_pqc_hw/tests/pqc/v80_variants/mlkem768_full/kernel.vxbin",
    },
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_log(path):
    text = path.read_text()
    batch = re.search(
        r"BATCH: requests=(\d+) start=(\d+) end=(\d+) makespan=(\d+)", text
    )
    perf = re.search(r"PERF: instrs=(\d+), cycles=(\d+), IPC=([0-9.]+)", text)
    if batch is None or perf is None or "PASSED!" not in text:
        raise RuntimeError(f"incomplete result: {path}")
    return {
        "log": str(path.relative_to(ROOT)),
        "requests": int(batch.group(1)),
        "makespan_cycles": int(batch.group(4)),
        "retired_instructions": int(perf.group(1)),
        "perf_cycles": int(perf.group(2)),
        "ipc": float(perf.group(3)),
        "kat": "PASS",
    }


def summarize_board(batch, arm):
    runs = [
        parse_log(RESULT_DIR / f"mlkem768_{arm}_{batch.lower()}_multiout_v80_r{i}.log")
        for i in range(1, 6)
    ]
    requests = {run["requests"] for run in runs}
    instructions = {run["retired_instructions"] for run in runs}
    if len(requests) != 1 or len(instructions) != 1:
        raise RuntimeError(f"unstable board metadata for {batch}/{arm}")
    return {
        "runs": runs,
        "median_makespan_cycles": int(
            statistics.median(run["makespan_cycles"] for run in runs)
        ),
        "median_perf_cycles": int(
            statistics.median(run["perf_cycles"] for run in runs)
        ),
        "min_makespan_cycles": min(run["makespan_cycles"] for run in runs),
        "max_makespan_cycles": max(run["makespan_cycles"] for run in runs),
        "all_kat_pass": True,
    }


def speedups(results):
    cycles = {
        arm: results[arm]["median_makespan_cycles"] for arm in ARMS
    }
    return {
        "keccak_A_over_B": cycles["A"] / cycles["B"],
        "ntt_first_A_over_C": cycles["A"] / cycles["C"],
        "ntt_after_keccak_B_over_D": cycles["B"] / cycles["D"],
        "keccak_after_ntt_C_over_D": cycles["C"] / cycles["D"],
        "joint_A_over_D": cycles["A"] / cycles["D"],
    }


def parity(batch, arm):
    drivers = {}
    for driver in ("simx", "rtlsim"):
        path = RESULT_DIR / f"mlkem768_{arm}_{batch.lower()}_multiout_{driver}.log"
        drivers[driver] = parse_log(path)
    simx = drivers["simx"]
    rtlsim = drivers["rtlsim"]
    return {
        **drivers,
        "instructions_exact": (
            simx["retired_instructions"] == rtlsim["retired_instructions"]
        ),
        "perf_cycle_relative_difference_pct": (
            abs(simx["perf_cycles"] - rtlsim["perf_cycles"])
            / rtlsim["perf_cycles"]
            * 100
        ),
        "within_5pct": (
            abs(simx["perf_cycles"] - rtlsim["perf_cycles"])
            / rtlsim["perf_cycles"]
            <= 0.05
        ),
    }


def rtlsim_reference(batch, arm):
    current = RESULT_DIR / f"mlkem768_{arm}_{batch.lower()}_multiout_rtlsim.log"
    if current.exists():
        return parse_log(current)
    count = 1 if batch == "M1" else 8
    return parse_log(
        RESULT_DIR
        / f"mlkem768_ablation_{arm}_b{count}_w{count}_rtlsim_bresp.log"
    )


def main():
    clock_readback = RESULT_DIR / "board_clock_multiout_150mhz_readback.log"
    board_clock_hz = int(clock_readback.read_text().strip())
    if board_clock_hz != 150000000:
        raise RuntimeError(f"unexpected board clock: {board_clock_hz}")

    arms = {}
    for name, arm in ARMS.items():
        kernel = ROOT / arm["kernel"]
        arms[name] = {**arm, "kernel_sha256": sha256(kernel)}

    board = {
        batch: {arm: summarize_board(batch, arm) for arm in ARMS}
        for batch in ("M1", "M8")
    }
    parity_results = {
        "M1": {arm: parity("M1", arm) for arm in ARMS},
        "M8": {"D": parity("M8", "D")},
    }
    rtlsim = {
        batch: {arm: rtlsim_reference(batch, arm) for arm in ARMS}
        for batch in ("M1", "M8")
    }
    rtlsim_speedups = {
        batch: {
            name: value
            for name, value in speedups(
                {
                    arm: {"median_makespan_cycles": result["makespan_cycles"]}
                    for arm, result in rtlsim[batch].items()
                }
            ).items()
        }
        for batch in rtlsim
    }
    board_speedups = {batch: speedups(board[batch]) for batch in board}
    board_vs_rtlsim = {
        batch: {
            name: {
                "v80": board_speedups[batch][name],
                "rtlsim": rtlsim_speedups[batch][name],
                "relative_difference_pct": (
                    board_speedups[batch][name] / rtlsim_speedups[batch][name] - 1
                )
                * 100,
            }
            for name in board_speedups[batch]
        }
        for batch in board
    }

    output = {
        "schema_version": 2,
        "scope": "ML-KEM-768 matched A/B/C/D final V80 comparison",
        "git_head": "24a74cdb8912091ff154479173d99eaffc993a91",
        "note": (
            "Final 150 MHz measurements with up to 16 outstanding AXI writes "
            "per bank. Speedups use the median of five board makespans."
        ),
        "configuration": {
            "xlen": 32,
            "extensions": "IM+PQC+NTT; F/D disabled",
            "cores": 1,
            "warps": 8,
            "threads": 32,
            "ntt_mul_lanes": 2,
            "dcache_writeback": 0,
            "board_clock_mhz": 150,
            "board_clock_readback_hz": board_clock_hz,
            "board_clock_readback_log": str(clock_readback.relative_to(ROOT)),
            "max_outstanding_axi_writes_per_bank": 16,
        },
        "image": {
            "path": "build32_v80_pqc_hw/hw/syn/xilinx/aved/v80_pqc_w8t32_150_id16_wt_multiout_aved_hw/bin/vortex_afu.vbin",
            "sha256": "2293c75bf5944af24ee90bfbc58be66cac791e07c64f1c9d3f8e2c41db5f3c19",
            "timing": {
                "wns_ns": 0.087,
                "tns_ns": 0.0,
                "whs_ns": 0.011,
                "ths_ns": 0.0,
                "routing_errors": 0,
                "constraints_met": True,
            },
            "full_design_resources": {
                "clb_luts": 286250,
                "registers": 206916,
                "bram_tiles": 183,
                "dsp_slices": 106,
            },
            "reconfigurable_module_resources": {
                "clb_luts": 266489,
                "registers": 185053,
                "bram_tiles": 183,
                "dsp_slices": 106,
            },
        },
        "arms": arms,
        "board_measurements": board,
        "board_speedups": board_speedups,
        "rtlsim_reference": {
            "note": (
                "RTLsim bypasses the AFU AXI adapter. The M8 A/B/C logs from "
                "the prior image therefore remain applicable; the current M8 "
                "D rerun exactly reproduced its instruction and cycle counts."
            ),
            "measurements": rtlsim,
            "speedups": rtlsim_speedups,
        },
        "board_vs_rtlsim_speedups": board_vs_rtlsim,
        "model_parity": parity_results,
        "validation": {
            "board_kat_runs_passed": 40,
            "board_kat_runs_total": 40,
            "runtime_async_simx": "PASS",
            "runtime_async_rtlsim": "PASS",
            "runtime_async_v80_runs_passed": 2,
            "mem_to_axi_protocol_test": "PASS",
        },
    }

    destination = RESULT_DIR / "mlkem768_ablation_v80_multiout_5x.json"
    destination.write_text(json.dumps(output, indent=2) + "\n")


if __name__ == "__main__":
    main()
