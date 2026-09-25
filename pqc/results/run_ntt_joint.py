#!/usr/bin/env python3
"""Build and run matched M2 primitive and Stage/NTT experiments from a build tree."""
import argparse
import concurrent.futures
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]
BUILD = Path.cwd()
FLAGS = ('-DVX_CFG_EXT_F_DISABLE -DVX_CFG_EXT_D_DISABLE '
         '-DVX_CFG_EXT_PQC_ENABLE -DVX_CFG_EXT_NTT_ENABLE '
         '-DVX_CFG_EXT_KSG25_ENABLE -DVX_CFG_EXT_KROUND25_ENABLE '
         '-DVX_CFG_NUM_WARPS=8 -DVX_CFG_NUM_THREADS=32 -DVX_CFG_NTT_MUL_LANES=2')
OUT = BUILD / 'ntt_joint'
RUNTIME = OUT / 'runtime'
ENV = dict(os.environ, LIBC_PATH=str(REPO.parent / 'toolchains-im/libc32'),
           LIBCRT_PATH=str(REPO.parent / 'toolchains-im/libcrt32'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runtime_sha(driver):
    library = 'libsimx.so' if driver == 'simx' else 'libxrtsim.so'
    names = ('libvortex.so', f'libvortex-{driver}.so', library)
    hashes = {name: sha(RUNTIME / name) for name in names}
    return hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()


def execute(command, cwd, name, env=ENV):
    log = OUT / f'{name}.log'
    status = {'command': command, 'cwd': str(cwd), 'start': time.time(),
              'environment': {k: env[k] for k in ('LIBC_PATH', 'LIBCRT_PATH',
                  'LD_LIBRARY_PATH', 'VORTEX_DRIVER', 'XRT_DEVICE', 'VORTEX_PROFILING') if k in env}}
    if env.get('VORTEX_DRIVER') in ('simx', 'xrt'):
        status['runtime_sha256'] = runtime_sha(env['VORTEX_DRIVER'])
    for binary in ('kernel.vxbin', command[0][2:]):
        p = cwd / binary
        if p.is_file():
            status[binary + '_sha256'] = sha(p)
    state = OUT / f'{name}.json'
    state.write_text(json.dumps(status, indent=2) + '\n')
    with log.open('w') as stream:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    status.update(exit_code=result.returncode, elapsed=time.time() - status['start'], log_sha256=sha(log))
    state.write_text(json.dumps(status, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(f'{name} exited {result.returncode}: {log}')
    print(f'PASS {name} ({status["elapsed"]:.1f}s)', flush=True)


def apps():
    for scheme in ('K', 'D'):
        yield f'primitive_{scheme}_sw', 'shared_ntt_xn', [f'SCHEME={scheme}', 'NTT=sw']
        yield f'primitive_{scheme}_hw', 'shared_ntt_xn', [f'SCHEME={scheme}', 'NTT=ise']
        project = 'mlkem_profile' if scheme == 'K' else 'mldsa_profile'
        fixed = ['NTT=reg32', 'UNROLL=1']
        fixed += ['SERIAL=1', 'ARITH=all'] if scheme == 'K' else ['MLDSA_RAM=full', 'POINTWISE_L5=w32']
        for arm, keccak, hardware in [('A', 'sg25_sw', False), ('B', 'sg25', False),
                                      ('C', 'sg25_sw', True), ('D', 'sg25', True)]:
            options = fixed + [f'KECCAK={keccak}']
            if hardware:
                options += ['NTTMUL=ise', 'NTTBF=ise']
            yield f'request_{scheme}_{arm}', project, options
            if arm != 'C':
                yield f'profile_{scheme}_{arm}', project, options + ['PROFILE_PHASES=1']


def build():
    execute(['../configure', '--xlen=32', '--tooldir=/home/jiangbowang/tools-pqc-v3.0.1'],
            BUILD, 'configure')
    RUNTIME.mkdir(exist_ok=True)
    archive = REPO / 'build32_ntt_bank2/bank2/runtime'
    for name in ('libvortex.so', 'libvortex-simx.so', 'libvortex-xrt.so'):
        shutil.copy2(archive / name, RUNTIME / name)
    shutil.copy2(RUNTIME / 'libvortex.so', BUILD / 'sw/runtime/libvortex.so')
    execute(['make', '-C', 'sw/kernel', '-j4', f'CONFIGS={FLAGS}'], BUILD, 'build_kernel')
    for name in ('simx', 'xrtsim'):
        command = ['make', '-C', f'sim/{name}', '-j8', 'JOBS=8', f'CONFIGS={FLAGS}', f'DESTDIR={RUNTIME}']
        if name == 'simx':
            command.append(str(RUNTIME / 'libsimx.so'))
        execute(command, BUILD, f'build_{name}')
    build_apps()


def audit_opcodes():
    audit = {}
    for name, _, _ in apps():
        dump = (BUILD / 'tests/pqc' / ('joint_' + name) / 'kernel.dump').read_text()
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
        kind, _, arm = name.split('_')
        hardware = arm == 'hw' if kind == 'primitive' else arm in ('C', 'D')
        stage = kind != 'primitive' and arm in ('B', 'D')
        assert bool(counts['nttmul']) == hardware and bool(counts['nttbf']) == hardware, name
        assert bool(counts['stage']) == stage, name
        assert counts['pointer'] == 0 and counts['round'] == 0, name
        audit[name] = counts
    (OUT / 'opcode_audit.json').write_text(json.dumps(audit, indent=2) + '\n')


def build_apps():
    for name, project, options in apps():
        app = BUILD / 'tests/pqc' / ('joint_' + name)
        app.mkdir(exist_ok=True)
        shutil.copy2(BUILD / 'tests/pqc' / project / 'Makefile', app / 'Makefile')
        execute(['make', '-j4', f'CONFIGS={FLAGS}'] + options, app, 'build_' + name)
    audit_opcodes()
    sources = [REPO / 'VX_config.toml', REPO / 'hw/rtl/pqc/VX_pqc_nttmul.sv',
               REPO / 'sim/simx/alu_unit.cpp', REPO / 'sw/kernel/include/pqc/vx_ntt.h']
    sources += list((REPO / 'tests/pqc/shared_ntt_xn').glob('*'))
    for folder in ('mlkem_profile', 'mldsa_profile'):
        sources += [p for p in (REPO / 'tests/pqc' / folder).glob('*') if p.is_file()]
    sources += [REPO / 'tests/pqc/mlkem_coop_ntt.h', REPO / 'tests/pqc/mldsa_ntt_reg32.h']
    manifest = {'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
                'flags': FLAGS, 'sources': {str(p.relative_to(REPO)): sha(p) for p in sources},
                'runtime': {p.name: sha(p) for p in RUNTIME.glob('*.so')},
                'apps': {name: {'project': project, 'options': options,
                    'kernel_sha256': sha(BUILD / 'tests/pqc' / ('joint_' + name) / 'kernel.vxbin')}
                    for name, project, options in apps()}}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def run(selection, workers):
    jobs = []
    for name, project, _ in apps():
        if selection != 'all' and not name.startswith(selection):
            continue
        for batch in ((1,) if name.startswith('profile') else (1, 8)):
            for driver in ('simx', 'xrt'):
                jobs.append((name, project, batch, driver))
    def work(job):
        name, project, batch, driver = job
        stem = f'{name}_m{batch}_{driver}'
        state = OUT / f'{stem}.json'
        app = BUILD / 'tests/pqc' / ('joint_' + name)
        argv = [f'./{project}', f'-b{batch}']
        argv += [f'-s{0 if batch == 1 else 1}'] if project == 'mldsa_profile' else ['-t32']
        if state.exists():
            old = json.loads(state.read_text())
            if (old.get('exit_code') == 0
                    and old.get('command') == argv
                    and old.get('kernel.vxbin_sha256') == sha(app / 'kernel.vxbin')
                    and old.get(project + '_sha256') == sha(app / project)
                    and old.get('runtime_sha256') == runtime_sha(driver)):
                print('SKIP ' + stem, flush=True)
                return
        env = dict(ENV, LD_LIBRARY_PATH=str(RUNTIME), VORTEX_DRIVER=driver,
                   XRT_DEVICE='xrtsim', VORTEX_PROFILING='0')
        execute(argv, app, stem, env)
        if not (OUT / f'{stem}.log').read_text().rstrip().endswith('PASSED!'):
            raise RuntimeError('Missing PASS: ' + stem)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        list(pool.map(work, jobs))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('build', 'apps', 'run'))
    parser.add_argument('--selection', choices=('all', 'primitive', 'request', 'profile'), default='all')
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    assert (BUILD / 'config.mk').exists() and BUILD != REPO, 'Run from a configured build tree'
    OUT.mkdir(exist_ok=True)
    if args.action == 'build':
        build()
    elif args.action == 'apps':
        build_apps()
    else:
        run(args.selection, args.workers)
