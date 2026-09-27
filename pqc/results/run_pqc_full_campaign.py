#!/usr/bin/env python3
"""Finish the complete-mapping simulation matrix and archive validated results."""
import json
from pathlib import Path
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / 'build32_pqc_parameters_full_wt'
RAW = BUILD / 'parameter_sweep'
OUT = REPO / 'pqc/results/parameter_scaling_full'
BOARD = REPO / 'pqc/results/v80_hw_validation/parameter_full_board_200mhz'


def main():
    assert Path.cwd() == BUILD
    manifest = json.loads((RAW / 'manifest.json').read_text())
    assert manifest['full_mapping']
    assert '-DVX_CFG_DCACHE_WRITEBACK=0' in manifest['flags'].split()
    for parameter in ('K512', 'K768', 'K1024', 'D44', 'D65', 'D87'):
        state = json.loads((RAW / f'{parameter}_E_m1_w1_s1_simx.json').read_text())
        assert state['exit_code'] == 0, parameter
    OUT.mkdir(exist_ok=True)
    active, finished = {}, {}
    for driver, workers in (('simx', 4), ('xrt', 10)):
        command = [sys.executable, str(REPO / 'pqc/results/run_pqc_parameters.py'),
                   'run', '--full-mapping', '--suite', '--drivers', driver,
                   '--workers', str(workers)]
        with (RAW / f'campaign_{driver}.log').open('w') as stream:
            process = subprocess.Popen(command, cwd=BUILD, stdout=stream, stderr=subprocess.STDOUT)
        active[driver] = process
        (RAW / f'campaign_{driver}.json').write_text(json.dumps(dict(
            pid=process.pid, command=command, start=time.time()), indent=2) + '\n')
        print('START', driver, process.pid, flush=True)
    collector = [sys.executable, str(REPO / 'pqc/results/collect_pqc_parameters.py'),
                 str(RAW), '--output', str(OUT)]
    last_collect = 0
    while active:
        for driver, process in list(active.items()):
            code = process.poll()
            if code is not None:
                finished[driver] = code
                del active[driver]
                print('FINISH', driver, code, flush=True)
        plans_ready = all((RAW / f'plan_{driver}.json').exists() for driver in ('simx', 'xrt'))
        if plans_ready and time.time() - last_collect >= 60:
            with (RAW / 'collection.log').open('w') as stream:
                result = subprocess.run(collector, cwd=BUILD, stdout=stream, stderr=subprocess.STDOUT)
            status = dict(running={key: p.pid for key, p in active.items()},
                          exited=finished, collector_exit=result.returncode, updated_unix=time.time())
            (RAW / 'campaign_status.json').write_text(json.dumps(status, indent=2) + '\n')
            if (OUT / 'pqc_parameters_status.json').exists():
                data = json.loads((OUT / 'pqc_parameters_status.json').read_text())
                print('STATUS', data['planned_completed'], '/', data['planned'],
                      'failed', len(data['failed']), 'parity_failed', len(data['parity_failures']), flush=True)
            last_collect = time.time()
        if active:
            time.sleep(10)
    assert all(code == 0 for code in finished.values()), finished
    subprocess.run(collector + ['--archive'], cwd=BUILD, check=True)
    subprocess.run([sys.executable, str(REPO / 'pqc/results/plot_pqc_parameters.py'),
                    str(OUT)], cwd=BUILD, check=True)
    assert (BOARD / 'summary.json').exists(), 'Board sweep has not completed'
    subprocess.run([sys.executable, str(REPO / 'pqc/results/summarize_pqc_full.py')],
                   cwd=BUILD, check=True)
    (RAW / 'campaign_done.json').write_text(json.dumps(dict(
        drivers=finished, complete=True, finished_unix=time.time()), indent=2) + '\n')
    print('COMPLETE', flush=True)


if __name__ == '__main__':
    main()
