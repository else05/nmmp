from pathlib import Path
import datetime
import json
import subprocess
import sys
import time

w = Path(__file__).resolve().parent
scripts = w.parents[2] / 'nmm-protect/scripts'
jobs = [
    ('largeShort-diagnostic', [w / 'run-s5-large-diagnostic.py']),
    ('post-review-ART', [w / 'test-wipe-device.py']),
    ('post-review-hotspots', [scripts / 'measure-hotspots.py', w / 'wipe-hotspot-plan.json', w / 'wipe-hotspots']),
    ('post-review-legacy-APK', [scripts / 'measure-startup.py', w / 'wipe-legacy-device-plan.json', w / 'wipe-legacy-device']),
    ('final-candidate-restore', [scripts / 'measure-startup.py', w / 'wipe-final-restoration-plan.json', w / 'wipe-final-device']),
    ('final-idle-observation', [w / 'observe-wipe-final-device.py'])]
for name, args in jobs:
    started = time.monotonic()
    row = dict(job=name, hostUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    print('START ' + name, flush=True)
    result = subprocess.run([sys.executable, '-B'] + [str(arg) for arg in args])
    row.update(exitCode=result.returncode, elapsedSeconds=time.monotonic() - started)
    with (w / 'wipe-device-jobs.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(row) + '\n')
    print(json.dumps(row), flush=True)
    if result.returncode:
        sys.exit(result.returncode)
