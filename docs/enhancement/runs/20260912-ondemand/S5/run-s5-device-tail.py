"""Serialize the remaining authorized device checks; stop and preserve evidence on failure."""
from pathlib import Path
import datetime
import json
import subprocess
import sys

w = Path(__file__).resolve().parent
scripts = w.parents[2] / 'nmm-protect/scripts'
jobs = [('invariant-diagnosis', [scripts / 'measure-hotspots.py', w / 's5-harness/diagnosis-invariant-plan.json', w / 's5-harness/diagnosis-invariant'])]
for index in range(1, 4):
    base = w / 's5' / ('seed' + str(index))
    jobs.append(('seed' + str(index) + '-hotspots', [scripts / 'measure-hotspots.py', base / 'hotspot-plan.json', base / 'hotspots']))
jobs += [('restore-candidate', [scripts / 'measure-startup.py', w / 'final-restoration-plan.json', w / 'final-device']),
         ('idle-stability', [w / 'observe-final-device.py'])]
for name, args in jobs:
    print('START ' + name, flush=True)
    before = datetime.datetime.now(datetime.timezone.utc).isoformat()
    result = subprocess.run([sys.executable] + [str(arg) for arg in args])
    record = dict(job=name, startedUtc=before, completedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  exitCode=result.returncode)
    with (w / 's5-device-tail.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(record) + '\n')
    if result.returncode:
        raise SystemExit(result.returncode)
    print('COMPLETE ' + name, flush=True)
