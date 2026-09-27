#!/usr/bin/env python3
"""Summarize the complete-mapping board cohort and its simulation coverage."""
import csv
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BOARD = REPO / 'pqc/results/v80_hw_validation/parameter_full_board_200mhz'
SIM = REPO / 'pqc/results/parameter_scaling_full'
SETS = ('K512', 'K768', 'K1024', 'D44', 'D65', 'D87')


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def write(path, rows):
    with path.open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def main():
    metadata = json.loads((BOARD / 'summary.json').read_text())
    assert metadata['full_mapping'] and metadata['cells'] == 156
    assert metadata['board_clock_hz_before'] == metadata['board_clock_hz_after'] == 200000000
    for parameter in SETS:
        assert len({metadata['call_count_sha256'][f'{parameter}_m8_w{workers}']
                    for workers in (1, 2, 4, 8)}) == 1, parameter
    manifest_path = REPO / 'build32_pqc_parameters_full_wt/parameter_sweep/manifest.json'
    manifest = json.loads(manifest_path.read_text())
    assert '-DVX_CFG_DCACHE_WRITEBACK=0' in manifest['flags'].split()
    for name, app in metadata['apps'].items():
        for key in ('kernel_sha256', 'host_sha256'):
            assert app[key] == manifest['apps'][name][key], (name, key)
    (BOARD / 'simulator_manifest.json').write_text(manifest_path.read_text())
    rows = read(BOARD / 'pqc_parameters_runs.csv')
    lookup = {(r['parameter'], r['arm'], int(r['requests']), int(r['resident'])):
              int(r['cycles']) for r in rows}
    ratios, occupancy, headline = [], [], []
    for (parameter, arm, batch, workers), cycles in lookup.items():
        targets = 'BCDE' if arm == 'A' else 'D' if arm == 'B' else 'E' if arm == 'D' else ''
        for target in targets:
            key = (parameter, target, batch, workers)
            if key in lookup:
                ratios.append(dict(parameter=parameter, requests=batch, resident=workers,
                    input_start=1, driver='aved', comparison=arm + '/' + target,
                    baseline_cycles=cycles, optimized_cycles=lookup[key], speedup=cycles / lookup[key]))
        key = (parameter, arm, batch, 1)
        if key in lookup:
            occupancy.append(dict(parameter=parameter, arm=arm, requests=batch, resident=workers,
                input_start=1, driver='aved', cycles=cycles, speedup_vs_one_worker=lookup[key] / cycles))
    write(BOARD / 'pqc_parameters_speedups.csv', ratios)
    write(BOARD / 'pqc_parameters_occupancy.csv', occupancy)
    for parameter in SETS:
        row = dict(parameter=parameter)
        for batch in (1, 8):
            for before, after in ('AB', 'BD', 'DE', 'AE'):
                row[f'{before}_over_{after}_m{batch}'] = (lookup[parameter, before, batch, batch] /
                                                         lookup[parameter, after, batch, batch])
        row['E_m1_cycles'] = lookup[parameter, 'E', 1, 1]
        row['E_m1_ms'] = row['E_m1_cycles'] / 200000
        row['E_occupancy_w8_over_w1'] = lookup[parameter, 'E', 8, 1] / lookup[parameter, 'E', 8, 8]
        row['E_throughput_m64_over_m8'] = 8 * lookup[parameter, 'E', 8, 8] / lookup[parameter, 'E', 64, 8]
        row['E_m64_requests_per_second'] = 64 * 200000000 / lookup[parameter, 'E', 64, 8]
        headline.append(row)
    write(BOARD / 'headline.csv', headline)
    model = json.loads((SIM / 'pqc_parameters_status.json').read_text()) if (SIM / 'pqc_parameters_status.json').exists() else {}
    board_pairs = []
    if (SIM / 'pqc_parameters_runs.csv').exists():
        simrows = read(SIM / 'pqc_parameters_runs.csv')
        xrt = {(r['parameter'], r['arm'], int(r['requests']), int(r['resident'])): int(r['instructions'])
               for r in simrows if r['driver'] == 'xrt' and r['input_start'] == '1'}
        samples = read(BOARD / 'raw_runs.csv')
        for key in sorted(xrt):
            matches = [r for r in samples if
                       (r['parameter'], r['arm'], int(r['requests']), int(r['resident'])) == key]
            assert len(matches) == 5, key
            same = all(int(r['instructions']) == xrt[key] for r in matches)
            board_pairs.append(dict(parameter=key[0], arm=key[1], requests=key[2], resident=key[3],
                                    board_repetitions=5, instructions_match=same))
        if board_pairs:
            write(BOARD / 'board_xrt_instruction_parity.csv', board_pairs)
    result = dict(board_cells=156, repetitions=5, headline=headline, model_status=model,
                  board_simulator_identical_binaries=30,
                  board_xrt_paired_cells=len(board_pairs),
                  board_xrt_instruction_failures=[r for r in board_pairs if not r['instructions_match']])
    (BOARD / 'results_summary.json').write_text(json.dumps(result, indent=2) + '\n')
    lines = ['# Complete software mapping on V80 at 200 MHz', '',
        'All 156 cells use one warmup and five timed runs. Each request passes byte-exact KATs; '
        'DSA arena allocation checks pass. Algorithm call counts match across arms and repetitions. '
        'A uses software Keccak/NTT; B enables Stage, C enables NTT, D enables both, '
        'and E also enables arithmetic NTTMUL. Every arm uses the same full software mapping. '
        'KEM Montgomery conversion obeys the arithmetic switch; A contains no PQC/NTT custom opcodes.', '',
        '| Parameter | A/E, M1 | A/E, M8 | E M1 (ms) | E W8/W1, fixed M8 | E throughput M64/M8, fixed W8 |',
        '| --- | ---: | ---: | ---: | ---: | ---: |']
    for row in headline:
        lines.append('| {parameter} | {A_over_E_m1:.3f} | {A_over_E_m8:.3f} | {E_m1_ms:.3f} | '
                     '{E_occupancy_w8_over_w1:.3f} | {E_throughput_m64_over_m8:.3f} |'.format(**row))
    lines += ['', '## Incremental acceleration', '',
        '| Parameter | A/B M1 / M8 | B/D M1 / M8 | D/E M1 / M8 |',
        '| --- | ---: | ---: | ---: |']
    for row in headline:
        lines.append('| {parameter} | {A_over_B_m1:.3f} / {A_over_B_m8:.3f} | '
                     '{B_over_D_m1:.3f} / {B_over_D_m8:.3f} | '
                     '{D_over_E_m1:.3f} / {D_over_E_m8:.3f} |'.format(**row))
    lines += ['', 'All sweep inputs start at 1. Archived DSA reproduction uses input start 3 '
        'and is reported separately in `replay/summary.json`. Fixed-W8 batches use synchronized waves; '
        'DSA batch extensions add inputs and can change signing rejection work.', '',
        'The resident image was reused without programming. The user clock reads back at 200 MHz; '
        'VRT metadata does not independently attest the resident PDI identity.', '',
        f'Simulation coverage at report generation: {model.get("planned_completed", 0)}/'
        f'{model.get("planned", 312)} planned runs; {model.get("paired", 0)} SimX/XRT pairs; '
        f'{len(model.get("parity_failures", []))} model-parity failures. '
        f'Board/XRT instruction counts checked for {len(board_pairs)} cells (five repetitions each).', '',
        'The write-through simulator build produces byte-identical host/kernel binaries for all '
        '30 apps measured on the board; `simulator_manifest.json` records that comparison. '
        'The earlier write-back diagnostic is retained separately and does not pass RTL/model validation.', '',
        'Raw logs, build/source manifests, opcode/mapping audit, binaries and configurations are in '
        '`parameter_board_sources.zip`. Derived ratios are in `pqc_parameters_speedups.csv`; '
        'absolute throughput and latency are in `headline.csv`.', '']
    (BOARD / 'README.md').write_text('\n'.join(lines))
    assert not result['board_xrt_instruction_failures'], result['board_xrt_instruction_failures']
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
