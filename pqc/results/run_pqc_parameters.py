#!/usr/bin/env python3
"""Run all parameter sets on the archived, identical RV32IM W8T32 M2 core."""
import argparse
import concurrent.futures
import fcntl
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from run_ntt_joint import FLAGS, ENV, REPO, sha

BUILD = Path.cwd()
OUT = BUILD / 'parameter_sweep'
RUNTIME = OUT / 'runtime'
SETS = ('K512', 'K768', 'K1024', 'D44', 'D65', 'D87')
ARMS = {'A': ('sg25_sw', False), 'B': ('sg25', False),
        'C': ('sg25_sw', True), 'D': ('sg25', True), 'E': ('sg25', True)}


def apps():
    for parameter in SETS:
        project = 'mlkem_profile' if parameter[0] == 'K' else 'mldsa_profile'
        fixed = [f'PARAM={parameter[1:]}', 'NTT=reg32', 'UNROLL=1']
        fixed += (['SERIAL=1', 'ARITH=all'] if parameter[0] == 'K'
                  else ['MLDSA_RAM=full', 'POINTWISE_L5=w32'])
        for arm, (keccak, hardware) in ARMS.items():
            options = fixed + [f'KECCAK={keccak}']
            if hardware:
                options += ['NTTMUL=ise', 'NTTBF=ise']
            if arm == 'E':
                options += ['ARITH_MUL=ise' if parameter[0] == 'K' else 'POINTWISE=ise']
            yield f'{parameter}_{arm}', project, options


def audit_opcodes(manifest):
    result = {}
    for name in manifest['apps']:
        dump = (BUILD / 'tests/pqc' / ('parameters_' + name) / 'kernel.dump').read_text()
        counts = dict(nttmul=0, nttbf=0, stage=0, pointer=0, round=0)
        for word in re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s', dump, re.M):
            word = int(word, 16)
            f3, f7 = (word >> 12) & 7, word >> 25
            if word & 127 != 11:
                continue
            if f7 == 5 and f3 in (2, 3):
                counts['nttmul'] += 1
            elif f7 in (8, 9, 10, 11):
                counts['nttbf'] += 1
            elif f7 == 6:
                counts['stage'] += 1
            elif f7 == 5 and f3 == 0:
                counts['pointer'] += 1
            elif f7 == 7:
                counts['round'] += 1
        arm = name.split('_')[1]
        assert bool(counts['nttmul']) == (arm in 'CDE'), name
        assert bool(counts['nttbf']) == (arm in 'CDE'), name
        assert bool(counts['stage']) == (arm in 'BDE'), name
        assert counts['pointer'] == 0 and counts['round'] == 0, name
        result[name] = counts
    (OUT / 'opcode_audit.json').write_text(json.dumps(result, indent=2) + '\n')


def build():
    with (OUT / 'configure.log').open('w') as log:
        subprocess.run(['../configure', '--xlen=32', '--tooldir=/home/jiangbowang/tools-pqc-v3.0.1'],
                       cwd=BUILD, env=ENV, stdout=log, stderr=subprocess.STDOUT, check=True)
    previous = REPO / 'build32_ntt_joint/ntt_joint/manifest.json'
    core = json.loads(previous.read_text())
    assert core['flags'] == FLAGS
    for name, expected in core['runtime'].items():
        assert sha(RUNTIME / name) == expected, name
    manifest = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                'flags': FLAGS, 'runtime': core['runtime'], 'core_manifest': str(previous), 'apps': {}}
    for name, project, options in apps():
        folder = BUILD / 'tests/pqc' / ('parameters_' + name)
        folder.mkdir(exist_ok=True)
        shutil.copy2(BUILD / 'tests/pqc' / project / 'Makefile', folder / 'Makefile')
        command = ['make', '-j4', f'CONFIGS={FLAGS}'] + options
        with (OUT / f'build_{name}.log').open('w') as log:
            subprocess.run(command, cwd=folder, env=ENV, stdout=log, stderr=subprocess.STDOUT, check=True)
        manifest['apps'][name] = dict(project=project, options=options, command=command,
                                     kernel_sha256=sha(folder / 'kernel.vxbin'), host_sha256=sha(folder / project))
        print('BUILT ' + name, flush=True)
    audit_opcodes(manifest)
    sources = [REPO / 'VX_config.toml']
    for directory in ('tests/pqc/mlkem_profile', 'tests/pqc/mldsa_profile', 'tests/pqc/mlkem', 'tests/pqc/mldsa'):
        sources += [p for p in (REPO / directory).iterdir() if p.is_file()]
    sources += list((REPO / 'tests/pqc').glob('*.h'))
    sources += [Path(__file__).resolve(), Path(__file__).with_name('run_ntt_joint.py').resolve(),
                Path(__file__).with_name('collect_pqc_parameters.py').resolve(),
                REPO / 'docs/proposals/pqc_parameter_scaling_proposal.md']
    manifest['sources'] = {}
    for path in sources:
        relative = str(path.relative_to(REPO))
        target = OUT / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        manifest['sources'][relative] = sha(path)
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def run(args):
    manifest = json.loads((OUT / 'manifest.json').read_text())
    runtime = {name: sha(RUNTIME / name) for name in manifest['runtime']}
    assert runtime == manifest['runtime'], 'Runtime changed'
    cells = [(name, batch, args.resident or min(batch, 8)) for name in manifest['apps']
             if name.split('_')[0] in args.parameters and name.split('_')[1] in args.arms
             for batch in args.batches]
    if args.suite:
        cells = [(name, batch, batch) for name in manifest['apps'] for batch in (1, 8)]
        for parameter in SETS:
            for arm in ('A', 'E'):
                name = f'{parameter}_{arm}'
                cells += [(name, b, b) for b in (2, 4)]
                cells += [(name, 8, w) for w in (1, 2, 4)]
                cells += [(name, b, 8) for b in (16, 32, 64)]
    jobs = [(name, batch, resident, driver) for name, batch, resident in cells for driver in args.drivers]
    stems = [f'{name}_m{batch}_w{resident}_s{args.input_start}_{driver}'
             for name, batch, resident, driver in jobs]
    if args.suite:
        (OUT / ('plan_' + '_'.join(args.drivers) + '.json')).write_text(
            json.dumps({'jobs': stems}, indent=2) + '\n')

    def work(job):
        name, batch, resident, driver = job
        stem = f'{name}_m{batch}_w{resident}_s{args.input_start}_{driver}'
        with (OUT / (stem + '.lock')).open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            work_locked(job)

    def work_locked(job):
        name, batch, resident, driver = job
        app = manifest['apps'][name]
        project = app['project']
        folder = BUILD / 'tests/pqc' / ('parameters_' + name)
        assert sha(folder / 'kernel.vxbin') == app['kernel_sha256'], name
        assert sha(folder / project) == app['host_sha256'], name
        command = [f'./{project}', f'-b{batch}', f'-w{resident}']
        command += [f'-s{args.input_start}'] if name.startswith('D') else ['-t32']
        stem = f'{name}_m{batch}_w{resident}_s{args.input_start}_{driver}'
        state = OUT / f'{stem}.json'
        logpath = OUT / f'{stem}.log'
        identity = dict(command=command, kernel_sha256=app['kernel_sha256'],
                        host_sha256=app['host_sha256'], runtime=runtime,
                        parameter=name.split('_')[0], arm=name.split('_')[1],
                        batch=batch, resident=resident, input_start=args.input_start, driver=driver)
        if state.exists():
            old = json.loads(state.read_text())
            if (old.get('exit_code') == 0 and all(old.get(k) == v for k, v in identity.items())
                    and logpath.exists() and sha(logpath) == old.get('log_sha256')):
                print('SKIP ' + stem, flush=True)
                return
        status = dict(identity, cwd=str(folder), start=time.time())
        state.write_text(json.dumps(status, indent=2) + '\n')
        env = dict(ENV, LD_LIBRARY_PATH=str(RUNTIME), VORTEX_DRIVER=driver,
                   XRT_DEVICE='xrtsim', VORTEX_PROFILING='0')
        with logpath.open('w') as log:
            result = subprocess.run(command, cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT)
        status.update(exit_code=result.returncode, elapsed=time.time() - status['start'], log_sha256=sha(logpath))
        state.write_text(json.dumps(status, indent=2) + '\n')
        if result.returncode or not logpath.read_text().rstrip().endswith('PASSED!'):
            raise RuntimeError(f'{stem} failed: {logpath}')
        print(f'PASS {stem} ({status["elapsed"]:.1f}s)', flush=True)

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for job, future in zip(jobs, [pool.submit(work, job) for job in jobs]):
            try:
                future.result()
            except Exception as exc:
                failures.append(str(exc))
                print('FAIL ' + str(exc), flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('build', 'run'))
    parser.add_argument('--suite', action='store_true', help='Complete paper coverage, occupancy, and batch matrix')
    parser.add_argument('--parameters', nargs='+', choices=SETS, default=SETS)
    parser.add_argument('--arms', nargs='+', choices=ARMS, default=list(ARMS))
    parser.add_argument('--batches', nargs='+', type=int, choices=(1, 2, 4, 8, 16, 32, 64), default=(1, 2, 4, 8))
    parser.add_argument('--drivers', nargs='+', choices=('simx', 'xrt'), default=('simx', 'xrt'))
    parser.add_argument('--resident', type=int, choices=(1, 2, 4, 8))
    parser.add_argument('--input-start', type=int, default=1)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    assert BUILD != REPO and (BUILD / 'config.mk').exists(), 'Run from a configured build tree'
    OUT.mkdir(exist_ok=True)
    assert args.workers > 0 and 0 <= args.input_start <= 0xffffffff - 63
    assert all(b % (args.resident or min(b, 8)) == 0 for b in args.batches)
    if args.action == 'build':
        build()
    else:
        run(args)
