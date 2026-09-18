#!/usr/bin/env python3
"""Collect matched M1 phase intervals for the final shared-NTT build."""

import csv
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUN = ROOT / "build32_im/tests/pqc"
RUNTIME = ROOT / "build32_im/mldsa_ntt_shared/runtime"
FIELDS = ["scheme", "driver", "phase", "permute", "absorb", "squeeze",
          "ntt", "intt", "pointwise", "mulcache", "basemul", "reduce",
          "residual", "phase_total", "request_total", "instrs",
          "binary_sha256", "runtime_sha256", "log_sha256", "raw_log",
          "headline_total", "instrumentation_overhead_pct",
          "headline_log_sha256", "headline_raw_log"]


def match(pattern, content):
    found = re.search(pattern, content, re.MULTILINE)
    assert found, pattern
    return found


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


rows = []
for scheme, directory in (("mlkem", "mlkem_shared_phase_profile"),
                          ("mldsa", "mldsa_phase_profile")):
    binary = RUN / directory / "kernel.vxbin"
    for driver in ("simx", "xrt"):
        log = RUN / directory / f"shared_phase_{driver}.log"
        content = log.read_text()
        assert content.endswith("PASSED!\n")
        if scheme == "mldsa":
            assert "REFERENCE: pk/sk/signature match portable C byte-for-byte" in content
        else:
            assert "KAT: id=0 pk/sk/ct/ss_enc/ss_dec match byte-for-byte" in content
        instrs = int(match(r"^PERF: instrs=(\d+), cycles=", content).group(1))
        request_total = int(match(r"^CYCLES: .* total=(\d+)$", content).group(1))
        headline = (RUN / "mlkem_shared_headline" / f"headline_{driver}.log"
                    if scheme == "mlkem" else
                    ROOT / "build32_im/mldsa_ntt_shared" / f"pointwise_both_{driver}_m1.log")
        headline_content = headline.read_text()
        assert headline_content.endswith("PASSED!\n")
        if scheme == "mlkem":
            assert "keccak=pe fips202=serial" in headline_content
            assert "mulcache=w32 basemul=w32 reduce=w32 mul=ise profile=0" in headline_content
        else:
            assert "ntt=reg32 nttmul=d nttbf=d pointwise=ise l5=w32" in headline_content
        headline_total = int(match(r"^CYCLES: .* total=(\d+)$",
                                   headline_content).group(1))
        overhead = round((request_total / headline_total - 1) * 100, 3)
        common = [scheme, driver]
        runtime = RUNTIME / ("libsimx.so" if driver == "simx" else "libxrtsim.so")
        tail = [request_total, instrs, digest(binary), digest(runtime), digest(log),
                str(log.relative_to(ROOT)), headline_total, overhead,
                digest(headline), str(headline.relative_to(ROOT))]
        if scheme == "mlkem":
            assert "keccak=pe fips202=serial" in content
            phases = dict((name, int(value)) for name, value in re.findall(
                r"^PHASE_PROFILE: id=0 phase=(\w+) cycles=(\d+)",
                content, re.MULTILINE))
            names = ("keccak_permute", "keccak_absorb", "keccak_squeeze",
                     "ntt", "intt", "mulcache", "basemul", "reduce", "other")
            assert set(phases) == set(names)
            assert sum(phases.values()) == request_total
            rows.append(common + ["roundtrip", phases["keccak_permute"],
                         phases["keccak_absorb"], phases["keccak_squeeze"],
                         phases["ntt"], phases["intt"], 0,
                         phases["mulcache"], phases["basemul"],
                         phases["reduce"], phases["other"], request_total] + tail)
        else:
            assert "ntt=reg32 nttmul=d nttbf=d pointwise=ise l5=w32" in content
            details = re.findall(
                r"^DETAIL: phase=(\w+) permute=(\d+) ntt=(\d+) intt=(\d+) "
                r"pointwise=(\d+) residual=(\d+) total=(\d+)$",
                content, re.MULTILINE)
            assert [detail[0] for detail in details] == ["keypair", "sign", "verify"]
            assert sum(int(detail[-1]) for detail in details) == request_total
            for name, permute, ntt, intt, pointwise, residual, total in details:
                values = list(map(int, (permute, ntt, intt, pointwise, residual, total)))
                assert sum(values[:5]) == values[5]
                rows.append(common + [name, values[0], 0, 0, values[1], values[2],
                             values[3], 0, 0, 0, values[4], values[5]] + tail)

for scheme in ("mlkem", "mldsa"):
    pair = [row for row in rows if row[0] == scheme and row[2] ==
            ("roundtrip" if scheme == "mlkem" else "keypair")]
    assert len(pair) == 2
    assert pair[0][16] == pair[1][16]
    assert pair[0][15] == pair[1][15]
    assert abs(pair[0][14] - pair[1][14]) / pair[1][14] < 0.05

out = Path(__file__).with_name("final_phase_profile.csv")
with out.open("w", newline="") as stream:
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(FIELDS)
    writer.writerows(rows)
