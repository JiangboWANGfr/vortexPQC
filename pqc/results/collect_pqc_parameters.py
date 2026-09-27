#!/usr/bin/env python3
"""Validate completed parameter/scaling jobs without treating pending jobs as passes."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import zipfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, rows):
    if not rows:
        return
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def collect(source, output, archive):
    manifest = json.loads((source / 'manifest.json').read_text())
    rows, pending, failures = [], [], []
    planned = set()
    for plan in source.glob('plan_*.json'):
        planned.update(json.loads(plan.read_text())['jobs'])
    completed = set()
    for state in sorted(source.glob('*_m*_w*_s*_*.json')):
        status = json.loads(state.read_text())
        if 'exit_code' not in status:
            pending.append(state.stem)
            continue
        if status['exit_code']:
            failures.append(state.stem)
            continue
        log = state.with_suffix('.log')
        text = log.read_text()
        name = status['parameter'] + '_' + status['arm']
        app = manifest['apps'][name]
        folder = source.parent / 'tests/pqc' / ('parameters_' + name)
        assert status['runtime'] == manifest['runtime'], state
        assert sha(log) == status['log_sha256'], state
        assert status['kernel_sha256'] == app['kernel_sha256'] == sha(folder / 'kernel.vxbin'), state
        assert status['host_sha256'] == app['host_sha256'] == sha(folder / app['project']), state
        assert text.rstrip().endswith('PASSED!'), state
        batch, resident = status['batch'], status['resident']
        parameter = status['parameter']
        if parameter.startswith('K'):
            assert f'PARAMETER_SET: ML-KEM-{parameter[1:]}' in text, state
            assert text.count('pk/sk/ct/ss_enc/ss_dec match byte-for-byte') == batch, state
            counts = re.findall(r'^COUNTERS:.*$', text, re.M)
            assert len(counts) == batch, state
        else:
            assert text.startswith(f'ML-DSA-{parameter[1:]} '), state
            assert text.count('pk/sk/signature match portable C byte-for-byte') == batch, state
            counts = re.findall(r'^(?:keccak_f1600_x[14]|poly_ntt|poly_invntt|rej_uniform|poly_pointwise|pointwise_acc_l5)\s+\d+$', text, re.M)
            assert len(counts) == 7 * batch, state
            assert len(re.findall(r'ARENA: peak=\d+ of \d+ fail=0', text)) == batch, state
        assert f'SCHEDULING: requests={batch} workers={resident} waves={batch // resident}' in text, state
        instructions, device_cycles = re.findall(r'PERF: instrs=(\d+), cycles=(\d+)', text)[0]
        cycles = int(re.findall(r'^BATCH:.*makespan=(\d+)', text, re.M)[0])
        completed.add(state.stem)
        rows.append(dict(parameter=parameter, arm=status['arm'], requests=batch,
                         resident=resident, input_start=status['input_start'], driver=status['driver'],
                         cycles=cycles, cycles_per_request=cycles / batch,
                         requests_per_mcycle=batch * 1e6 / cycles,
                         instructions=int(instructions), device_cycles=int(device_cycles),
                         calls_sha256=hashlib.sha256('\n'.join(counts).encode()).hexdigest(), log=log.name))
    groups = {}
    for row in rows:
        key = (row['parameter'], row['requests'], row['input_start'])
        groups.setdefault(key, set()).add(row['calls_sha256'])
    assert all(len(hashes) == 1 for hashes in groups.values()), 'Workload counts differ across configurations'
    lookup = {(r['parameter'], r['arm'], r['requests'], r['resident'], r['input_start'], r['driver']): r for r in rows}
    ratios, parity, occupancy = [], [], []
    for row in rows:
        param, arm, batch, resident, seed, driver = (row[k] for k in
            ('parameter', 'arm', 'requests', 'resident', 'input_start', 'driver'))
        if arm == 'A':
            for target in 'BCDE':
                key = (param, target, batch, resident, seed, driver)
                if key not in lookup:
                    continue
                other = lookup[key]
                ratios.append(dict(parameter=param, requests=batch, resident=resident, input_start=seed,
                                   driver=driver, comparison='A/' + target,
                                   baseline_cycles=row['cycles'], optimized_cycles=other['cycles'],
                                   speedup=row['cycles'] / other['cycles']))
        if arm in ('B', 'D'):
            target = 'D' if arm == 'B' else 'E'
            key = (param, target, batch, resident, seed, driver)
            if key in lookup:
                other = lookup[key]
                ratios.append(dict(parameter=param, requests=batch, resident=resident, input_start=seed,
                                   driver=driver, comparison=arm + '/' + target,
                                   baseline_cycles=row['cycles'], optimized_cycles=other['cycles'],
                                   speedup=row['cycles'] / other['cycles']))
        key = (param, arm, batch, 1, seed, driver)
        if key in lookup:
            occupancy.append(dict(parameter=param, arm=arm, requests=batch, resident=resident,
                                  input_start=seed, driver=driver, cycles=row['cycles'],
                                  speedup_vs_one_worker=lookup[key]['cycles'] / row['cycles']))
        key = (param, arm, batch, resident, seed, 'simx')
        if driver == 'xrt' and key in lookup:
            sim = lookup[key]
            gap = 100 * abs(sim['device_cycles'] / row['device_cycles'] - 1)
            same = sim['instructions'] == row['instructions']
            parity.append(dict(parameter=param, arm=arm, requests=batch, resident=resident,
                               input_start=seed, instructions_match=same, device_gap_pct=gap,
                               interval_gap_pct=100 * abs(sim['cycles'] / row['cycles'] - 1),
                               pass_5pct=same and gap <= 5))
    output.mkdir(parents=True, exist_ok=True)
    for name, data in [('runs', rows), ('speedups', ratios), ('occupancy', occupancy), ('parity', parity)]:
        write(output / f'pqc_parameters_{name}.csv', data)
    summary = dict(completed=len(rows), planned=len(planned), planned_completed=len(planned & completed),
                   not_completed=sorted(planned - completed), pending=pending, failed=failures,
                   paired=len(parity), parity_failures=[p for p in parity if not p['pass_5pct']])
    (output / 'pqc_parameters_status.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'not_completed'}, indent=2))
    if archive:
        assert not pending and not failures and not (planned - completed), 'Do not archive incomplete executions'
        assert all(p['pass_5pct'] for p in parity), 'Do not archive failed timing parity'
        with zipfile.ZipFile(output / 'pqc_parameters_sources.zip', 'w', zipfile.ZIP_DEFLATED) as zipped:
            for path in source.glob('*'):
                if path.suffix in ('.json', '.log'):
                    zipped.write(path, 'raw/' + path.name)
            for relative, expected in manifest['sources'].items():
                path = source / 'source' / relative
                assert sha(path) == expected, path
                zipped.write(path, 'source/' + relative)
            for name, app in manifest['apps'].items():
                folder = source.parent / 'tests/pqc' / ('parameters_' + name)
                for file in ('kernel.vxbin', 'kernel.dump', 'config.stamp', app['project']):
                    zipped.write(folder / file, 'tests/pqc/parameters_' + name + '/' + file)
            for path in output.glob('pqc_parameters_*.csv'):
                zipped.write(path, 'tables/' + path.name)
            for name, expected in manifest['runtime'].items():
                path = source / 'runtime' / name
                assert sha(path) == expected, path
                zipped.write(path, 'runtime/' + name)
    return not failures and all(p['pass_5pct'] for p in parity)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--archive', action='store_true')
    args = parser.parse_args()
    raise SystemExit(0 if collect(args.source, args.output, args.archive) else 1)
