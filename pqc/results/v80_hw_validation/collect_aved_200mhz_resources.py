#!/usr/bin/env python3
"""Extract post-route resources for the measured 200 MHz AVED image."""

import hashlib
import json
from pathlib import Path
import re


REPO = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
IMAGE = REPO / "build32_v80_pqc_hw/hw/syn/xilinx/aved/v80_pqc_w8t32_200_id16_wt_multiout_aved_hw/bin/vortex_afu.vbin"
UTIL = IMAGE.parent / "vortex_afu.vbin.prj/report_utilization_vortex_afu.txt"
TIMING = HERE / "timing_multiout_200mhz.txt"
ROUTE = HERE / "route_status_multiout_200mhz.rpt"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    util = UTIL.read_text()
    timing = TIMING.read_text()
    route = ROUTE.read_text()
    assert "Design State : Physopt postRoute" in util
    assert "200.000" in timing and "All user specified timing constraints are met." in timing
    wns = float(re.search(r"^\s*([+-]?\d+\.\d+)\s+0\.000\s+0\s+509918", timing, re.M).group(1))
    assert wns >= 0
    assert re.search(r"# of nets with routing errors\.*\s*:\s*0\s*:", route)

    names = {
        "top_wrapper": "full_device",
        "vortex_afu_0": "reconfigurable_afu",
        "g_cores[0].core": "vortex_core",
        "g_blocks[0].ksg25_unit": "keccak_stage_unit",
        "g_blocks[0].kround25_unit": "keccak_round_unit",
        "g_blocks[0].nttmul_unit": "shared_ntt_unit",
        "pqc_agu": "pointer_keccak_agu",
    }
    found = {}
    for line in util.splitlines():
        if not line.startswith("|"):
            continue
        fields = [part.strip() for part in line.strip("|").split("|")]
        if len(fields) != 13 or fields[0] not in names:
            continue
        name = names[fields[0]]
        assert name not in found, name
        number = lambda index: int(re.match(r"\d+", fields[index]).group())
        found[name] = {
            "luts": number(4), "ffs": number(8),
            "ramb36": number(9), "ramb18": number(10), "dsps": number(12),
        }
    assert set(found) == set(names.values()), set(names.values()) - set(found)
    data = {
        "clock_mhz": 200, "wns_ns": wns, "routing_errors": 0,
        "vbin_sha256": sha(IMAGE), "utilization_sha256": sha(UTIL),
        "timing_sha256": sha(TIMING), "route_sha256": sha(ROUTE),
        "note": "Hierarchy rows overlap; unit rows must not be summed with their parent core.",
        "resources": found,
    }
    (HERE / "aved_200mhz_resources.json").write_text(json.dumps(data, indent=2) + "\n")
    print(json.dumps(data, indent=2))


if __name__ == "__main__":
    main()
