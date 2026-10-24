#!/usr/bin/env python3
"""Reproduce the seven bakefix zones, moving props and before/after budget report.

Run with Python -I -B. No runtime files are generated or modified.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
ZONES = (1000,2000,3000,4000,5000,9000,12000)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', required=True)
    parser.add_argument('--out', default=str(ROOT/'sdmc-out/3ds/toontown3ds/data/zones'))
    parser.add_argument('--baseline', help='read-only original zone directory for comparison')
    args = parser.parse_args()
    output = Path(args.out).resolve()
    output.mkdir(parents=True,exist_ok=True)
    scratch = output/'tmp'
    scratch.mkdir(exist_ok=True)
    env = dict(os.environ, TMPDIR=str(scratch), TT_ETC1_THREADS='3',
               OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1',
               MKL_NUM_THREADS='1', VECLIB_MAXIMUM_THREADS='1')
    reports = {}
    for zone in ZONES:
        script = 'bake_coghq.py' if zone==12000 else 'bake_street.py'
        cmd = [sys.executable,'-I','-B',str(ROOT/'tools/pipeline'/script),
               '--zone',str(zone),'--resources',str(Path(args.resources).resolve()),'--out',str(output)]
        if zone==1000:
            cmd.append('--separate-boat')
        subprocess.run(cmd,check=True,env=env,cwd=ROOT)
        after = json.loads((output/f'{zone}.json').read_text())
        before = json.loads((Path(args.baseline)/f'{zone}.json').read_text()) if args.baseline else None
        reports[str(zone)] = dict(before=before['planned_worst'] if before else None,
                                 after=after['planned_worst'],file_bytes=after['file_bytes'],
                                 moving_props=after.get('moving_props',[]))
    (output/'bakefix_budgets.json').write_text(json.dumps(reports,indent=2)+'\n')


if __name__=='__main__':
    main()
