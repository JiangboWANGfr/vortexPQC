#!/usr/bin/env python3
"""Validate matched run logs and derive primitive, request, and profile tables."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import zipfile


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s,]+)', line))


def write(path, rows):
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def collect(source, destination):
    manifest = json.loads((source / 'manifest.json').read_text())
    runs, primitive, requests, profiles = [], [], [], []
    for name, app in manifest['apps'].items():
        kind, scheme, arm = name.split('_')
        binary_folder = source.parent / 'tests/pqc' / ('joint_' + name)
        assert hashlib.sha256((binary_folder / 'kernel.vxbin').read_bytes()).hexdigest() == app['kernel_sha256'], name
        for batch in ((1,) if kind == 'profile' else (1, 8)):
            for driver in ('simx', 'xrt'):
                stem = f'{name}_m{batch}_{driver}'
                status = json.loads((source / f'{stem}.json').read_text())
                log = source / f'{stem}.log'
                data = log.read_bytes()
                text = data.decode()
                assert status['exit_code'] == 0 and text.rstrip().endswith('PASSED!'), stem
                assert status['log_sha256'] == hashlib.sha256(data).hexdigest(), stem
                assert status['kernel.vxbin_sha256'] == app['kernel_sha256'], stem
                assert status[app['project'] + '_sha256'] == hashlib.sha256(
                    (binary_folder / app['project']).read_bytes()).hexdigest(), stem
                core = 'libsimx.so' if driver == 'simx' else 'libxrtsim.so'
                bundle = {n: manifest['runtime'][n] for n in ('libvortex.so', f'libvortex-{driver}.so', core)}
                assert status['runtime_sha256'] == hashlib.sha256(
                    json.dumps(bundle, sort_keys=True).encode()).hexdigest(), stem
                perf = re.findall(r'PERF: instrs=(\d+), cycles=(\d+)', text)
                assert len(perf) == 1, stem
                base = dict(kind=kind, scheme=scheme, arm=arm, batch=batch, driver=driver)
                run = dict(base, instructions=int(perf[0][0]), device_cycles=int(perf[0][1]),
                           interval_cycles='', calls_sha256='', log=f'{stem}.log')
                if kind == 'primitive':
                    samples = [fields(line) for line in text.splitlines() if line.startswith('NTT_BATCH ')]
                    assert len(samples) == 10, stem
                    assert {(r['direction'], int(r['sample'])) for r in samples} == {
                        (direction, sample) for direction in ('forward', 'inverse') for sample in range(5)}, stem
                    for sample in samples:
                        assert int(sample['mismatch']) == 0 and int(sample['checked']) == batch * 256, stem
                        primitive.append(dict(base, direction=sample['direction'], sample=int(sample['sample']),
                                              cycles=int(sample['coop']), scalar_reference_cycles=int(sample['ref'])))
                else:
                    kat = ('pk/sk/ct/ss_enc/ss_dec match byte-for-byte' if scheme == 'K'
                           else 'pk/sk/signature match portable C byte-for-byte')
                    assert kat in text, stem
                    if scheme == 'K':
                        calls = re.findall(r'^COUNTERS:.*$', text, re.M)
                        assert len(calls) == batch and 'basemul=w32 reduce=w32 mul=c' in text, stem
                    else:
                        calls = re.findall(r'^(?:keccak_f1600_x[14]|poly_ntt|poly_invntt|rej_uniform|poly_pointwise|pointwise_acc_l5)\s+\d+$', text, re.M)
                        assert len(calls) == batch * 7 and 'pointwise=c l5=w32' in text, stem
                    run['calls_sha256'] = hashlib.sha256('\n'.join(calls).encode()).hexdigest()
                    batch_lines = [fields(line) for line in text.splitlines() if line.startswith('BATCH:')]
                    assert len(batch_lines) == 1 and int(batch_lines[0]['requests']) == batch, stem
                    run['interval_cycles'] = int(batch_lines[0]['makespan'])
                    requests.append(dict(base, cycles=run['interval_cycles']))
                    if kind == 'profile':
                        if scheme == 'K':
                            for line in text.splitlines():
                                if line.startswith('PHASE_PROFILE:'):
                                    f = fields(line)
                                    profiles.append(dict(scheme=scheme, arm=arm, driver=driver,
                                                         phase=f['phase'], cycles=int(f['cycles'])))
                        else:
                            phase_totals = {p: 0 for p in ('permute', 'ntt', 'intt', 'pointwise', 'residual')}
                            details = [fields(line) for line in text.splitlines() if line.startswith('DETAIL:')]
                            assert len(details) == 3, stem
                            for f in details:
                                assert sum(int(f[p]) for p in phase_totals) == int(f['total']), stem
                                for p in phase_totals:
                                    phase_totals[p] += int(f[p])
                            for p, cycles in phase_totals.items():
                                profiles.append(dict(scheme=scheme, arm=arm, driver=driver, phase=p, cycles=cycles))
                runs.append(run)
    for scheme in ('K', 'D'):
        for batch in (1, 8):
            hashes = {r['calls_sha256'] for r in runs
                      if r['kind'] != 'primitive' and r['scheme'] == scheme and r['batch'] == batch}
            assert len(hashes) == 1, (scheme, batch, 'workload counters differ')
    destination.mkdir(exist_ok=True, parents=True)
    write(destination / 'ntt_joint_runs.csv', runs)
    write(destination / 'ntt_joint_primitive_samples.csv', primitive)
    medians = {}
    for row in primitive:
        key = tuple(row[k] for k in ('scheme', 'arm', 'batch', 'driver', 'direction'))
        medians.setdefault(key, []).append(row['cycles'])
    medians = {key: statistics.median(values) for key, values in medians.items()}
    primitive_ratios = []
    for (scheme, arm, batch, driver, direction), sw in medians.items():
        if arm != 'sw':
            continue
        hw = medians[scheme, 'hw', batch, driver, direction]
        primitive_ratios.append(dict(scheme=scheme, batch=batch, driver=driver, direction=direction,
                                    software_cycles=sw, hardware_cycles=hw, speedup=sw / hw,
                                    cycle_reduction_pct=100 * (1 - hw / sw)))
    write(destination / 'ntt_joint_primitive.csv', primitive_ratios)
    interval_parity = []
    for (scheme, arm, batch, driver, direction), rtl in medians.items():
        if driver != 'xrt':
            continue
        sim = medians[scheme, arm, batch, 'simx', direction]
        gap = 100 * abs(sim / rtl - 1)
        interval_parity.append(dict(scheme=scheme, arm=arm, batch=batch, direction=direction,
                                    simx_cycles=sim, xrt_cycles=rtl, gap_pct=gap, within_5pct=gap <= 5))
    write(destination / 'ntt_joint_primitive_parity.csv', interval_parity)
    print(f'Primitive interval parity: {sum(p["within_5pct"] for p in interval_parity)}/{len(interval_parity)}')
    for row in interval_parity:
        if not row['within_5pct']:
            print('PRIMITIVE INTERVAL OUTLIER:', row)
    lookup = {(r['kind'], r['scheme'], r['arm'], r['batch'], r['driver']): r['cycles'] for r in requests}
    request_ratios = []
    for scheme in ('K', 'D'):
        for batch in (1, 8):
            for driver in ('simx', 'xrt'):
                for before, after, change in [('A', 'B', 'keccak_first'), ('B', 'D', 'ntt_after_keccak'),
                                              ('A', 'C', 'ntt_first'), ('C', 'D', 'keccak_after_ntt'),
                                              ('A', 'D', 'joint')]:
                    a, b = [lookup['request', scheme, arm, batch, driver] for arm in (before, after)]
                    request_ratios.append(dict(scheme=scheme, batch=batch, driver=driver, change=change,
                                               before=before, after=after, before_cycles=a, after_cycles=b,
                                               speedup=a / b, cycle_reduction_pct=100 * (1 - b / a)))
    write(destination / 'ntt_joint_request.csv', request_ratios)
    for row in profiles:
        total = lookup['profile', row['scheme'], row['arm'], 1, row['driver']]
        clean = lookup['request', row['scheme'], row['arm'], 1, row['driver']]
        row.update(total_cycles=total, share_pct=100 * row['cycles'] / total,
                   headline_cycles=clean, probe_overhead_pct=100 * (total / clean - 1))
    write(destination / 'ntt_joint_profile.csv', profiles)
    pairs = {(r['kind'], r['scheme'], r['arm'], r['batch'], r['driver']): r for r in runs}
    parity = []
    for row in runs:
        if row['driver'] != 'xrt':
            continue
        sim = pairs[row['kind'], row['scheme'], row['arm'], row['batch'], 'simx']
        gap = 100 * abs(sim['device_cycles'] / row['device_cycles'] - 1)
        interval_gap = (100 * abs(sim['interval_cycles'] / row['interval_cycles'] - 1)
                        if row['interval_cycles'] else '')
        parity.append(dict(kind=row['kind'], scheme=row['scheme'], arm=row['arm'], batch=row['batch'],
                           instructions_match=sim['instructions'] == row['instructions'],
                           simx_cycles=sim['device_cycles'], xrt_cycles=row['device_cycles'],
                           device_gap_pct=gap, interval_gap_pct=interval_gap,
                           pass_5pct=gap <= 5 and sim['instructions'] == row['instructions']))
    write(destination / 'ntt_joint_parity.csv', parity)
    print(f'Collected {len(runs)} runs, {len(primitive)} primitive samples; '
          f'parity: {sum(p["pass_5pct"] for p in parity)}/{len(parity)}')
    repo = Path(__file__).resolve().parents[2]
    with zipfile.ZipFile(destination / 'ntt_joint_sources.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.glob('*')):
            if path.suffix in ('.json', '.log'):
                archive.write(path, 'raw/' + path.name)
        for relative, expected in manifest['sources'].items():
            path = repo / relative
            assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, relative
            archive.write(path, 'source/' + relative)
        for script in ('run_ntt_joint.py', 'collect_ntt_joint.py'):
            archive.write(Path(__file__).with_name(script), 'source/pqc/results/' + script)
        for name, app in manifest['apps'].items():
            folder = source.parent / 'tests/pqc' / ('joint_' + name)
            for filename in ('kernel.vxbin', 'kernel.dump', 'config.stamp', app['project']):
                archive.write(folder / filename, 'tests/pqc/joint_' + name + '/' + filename)
        for name in ('libvortex.so', 'libvortex-simx.so', 'libvortex-xrt.so'):
            archive.write(source / 'runtime' / name, 'raw/runtime/' + name)
        for path in sorted((source / 'runtime').glob('*config.stamp')):
            archive.write(path, 'raw/runtime/' + path.name)
        for path in sorted(destination.glob('ntt_joint_*.csv')):
            archive.write(path, 'tables/' + path.name)
    failures = [p for p in parity if not p['pass_5pct']]
    for failure in failures:
        print('PARITY FAILURE:', failure)
    return not failures and all(p['within_5pct'] for p in interval_parity)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    raise SystemExit(0 if collect(args.source, args.output) else 1)
