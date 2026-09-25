import hashlib
import json
import re
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[3]
results = root / 'pqc/results/v80_hw_validation'
images = root / 'build32_v80_pqc_hw/hw/syn/xilinx/aved/v80_pqc_w8t32_200_id16_wt_multiout_perf_aved_hw/bin'
manifest = json.loads((results / 'perf_image_manifest.json').read_text())
assert manifest['pdi_sha256'] == hashlib.sha256((images / 'vortex_afu.vbin.prj/images/top_i_slash_slash_vortex_afu_inst_0_partial.pdi').read_bytes()).hexdigest()
patterns = {
    1: {'ifetch_latency_cycles': r'ifetch_lat=([\d.]+)', 'load_latency_cycles': r'load_lat=([\d.]+)', 'scoreboard_stall_percent_reported': r'scrb=(\d+)%'},
    4: {'dcache_reqs': r'dcache: reqs=(\d+)', 'dcache_read_misses': r'miss_r=(\d+)', 'dcache_write_misses': r'miss_w=(\d+)', 'dcache_bank_stall_cycles': r'bank_st=(\d+)', 'dcache_mshr_stall_cycles': r'mshr_st=(\d+)'},
    7: {'offchip_reqs': r'memory: reqs=(\d+)', 'offchip_reads': r'\(r=(\d+)', 'offchip_writes': r'w=(\d+)\)', 'offchip_read_latency_cycles': r'lat=([\d.]+) cyc', 'coalescer_misses': r'coalescer: misses=(\d+)'},
}
out = {'schema_version': 1, 'scope': 'ML-KEM-768 D, PERF_ENABLE routed checkpoint, same PDI at 150 and 190 MHz, 5 runs per class and batch', 'images': manifest, 'by_batch': {}}
for freq in (150, 190):
    readback = results / f'board_clock_perf_{freq}mhz_readback.log'
    assert int(readback.read_text().strip()) == freq * 1000000
for batch in (1, 8):
    b = out['by_batch'][f'M{batch}'] = {}
    for freq in (150, 190):
        f = b[str(freq)] = {}
        for klass in (1, 4, 7):
            rows = []
            for rep in range(1, 6):
                path = results / f'mlkem768_D_m{batch}_perf{klass}_{freq}mhz_r{rep}.log'
                log = path.read_text()
                assert 'PASSED!' in log and len(re.findall(r'^KAT:', log, re.M)) == batch
                row = {'log': str(path.relative_to(root))}
                for key, pattern in {'makespan_cycles': r'BATCH:.*makespan=(\d+)', 'retired_instructions': r'PERF: instrs=(\d+)'}.items():
                    m = re.search(pattern, log)
                    assert m, (path, key)
                    row[key] = int(m.group(1))
                for key, pattern in patterns[klass].items():
                    m = re.search(pattern, log)
                    assert m, (path, key)
                    row[key] = float(m.group(1)) if '.' in m.group(1) else int(m.group(1))
                rows.append(row)
            assert len({r['retired_instructions'] for r in rows}) == 1
            medians = {k: statistics.median([r[k] for r in rows]) for k in rows[0] if k != 'log'}
            f[str(klass)] = {'runs': rows, 'medians': medians}
for batch in (1, 8):
    b = out['by_batch'][f'M{batch}']
    for freq in (150, 190):
        m = b[str(freq)]['7']['medians']
        m['offchip_read_latency_ns'] = m['offchip_read_latency_cycles'] * 1000 / freq
        m['elapsed_ms'] = m['makespan_cycles'] / (freq * 1000)
    b['elapsed_speedup_150_to_190'] = b['150']['7']['medians']['elapsed_ms'] / b['190']['7']['medians']['elapsed_ms']
path = results / 'mlkem768_perf_150_190mhz_5x.json'
path.write_text(json.dumps(out, indent=2) + '\n')
for batch in (1, 8):
    b = out['by_batch'][f'M{batch}']
    print(f'M{batch} elapsed speedup {b["elapsed_speedup_150_to_190"]:.4f}x')
    for freq in (150, 190):
        a=b[str(freq)]['1']['medians']; c=b[str(freq)]['4']['medians']; d=b[str(freq)]['7']['medians']
        print(freq, 'cycles',d['makespan_cycles'],'load_lat',a['load_latency_cycles'],'mshr_st',c['dcache_mshr_stall_cycles'],'bank_st',c['dcache_bank_stall_cycles'],'offchip_reads',d['offchip_reads'],'offchip_writes',d['offchip_writes'],'offchip_lat_cyc',d['offchip_read_latency_cycles'],'offchip_lat_ns',d['offchip_read_latency_ns'])
