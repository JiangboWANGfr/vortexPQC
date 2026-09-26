#!/usr/bin/env python3
"""Plot complete parameter-scaling series for one execution driver."""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

SETS = ('K512', 'K768', 'K1024', 'D44', 'D65', 'D87')


def plot(folder, driver):
    with (folder / 'pqc_parameters_runs.csv').open() as stream:
        rows = [r for r in csv.DictReader(stream) if r['driver'] == driver]
    for name, axis, points in [('occupancy', 'resident', (1, 2, 4, 8)),
                               ('batch', 'requests', (8, 16, 32, 64))]:
        fig, axes = plt.subplots(2, 3, figsize=(9, 5.2), layout='constrained')
        fig.supylabel('Requests / million cycles')
        complete = True
        for parameter, ax in zip(SETS, axes.flat):
            for arm, label in [('A', 'W32 software'), ('E', 'All implemented ISE')]:
                series = [r for r in rows if r['parameter'] == parameter and r['arm'] == arm
                          and int(r['input_start']) == 1
                          and (int(r['requests']) == 8 if name == 'occupancy' else int(r['resident']) == 8)
                          and int(r[axis]) in points]
                values = {int(r[axis]): float(r['requests_per_mcycle']) for r in series}
                if set(values) != set(points):
                    complete = False
                    continue
                ax.plot(points, [values[p] for p in points], marker='o', markersize=4, label=label)
            ax.set_title(('ML-KEM-' if parameter[0] == 'K' else 'ML-DSA-') + parameter[1:])
            ax.set_xticks(points)
            ax.set_xlabel('Resident warps (8 fixed inputs)' if name == 'occupancy' else 'Requests (8 resident warps)')
            ax.grid(alpha=0.2)
        if complete:
            axes[0, 0].legend(fontsize=8)
            fig.savefig(folder / f'pqc_parameters_{name}.pdf')
            fig.savefig(folder / f'pqc_parameters_{name}.png', dpi=180)
            print('Wrote ' + name)
        else:
            print('Pending complete ' + driver + ' series: ' + name)
        plt.close(fig)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', type=Path)
    parser.add_argument('--driver', default='xrt', choices=('xrt', 'aved'))
    args = parser.parse_args()
    plot(args.folder, args.driver)
