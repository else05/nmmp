"""Observe the restored candidate for five minutes without clearing data or restarting it."""
from pathlib import Path
import datetime
import json
import re
import subprocess
import time

w = Path(__file__).resolve().parent
out = w / 'final-stability'
out.mkdir(exist_ok=False)
adb = [r'E:\Scoop\apps\adb\current\platform-tools\adb.exe', '-s', '192.168.6.118:5555']
package = 'org.savior.sync'


def command(args):
    return subprocess.check_output(adb + args, text=True, encoding='utf-8', errors='replace', timeout=60)


restoration = json.loads((w / 'final-device/attempts.jsonl').read_text(encoding='utf-8').splitlines()[-1])
if not restoration['valid']:
    raise RuntimeError('Candidate restoration was not validated')
pid = command(['shell', 'pidof', package]).strip()
if pid != restoration['pid']:
    raise RuntimeError('Restored process changed before observation')
started = time.monotonic()
samples = []
for index in range(11):
    if index:
        time.sleep(max(0, index * 30 - (time.monotonic() - started)))
    current = command(['shell', 'pidof', package]).strip()
    row = dict(elapsedSeconds=time.monotonic() - started, pid=current,
               hostUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    if current != pid:
        row['failed'] = 'main process changed or exited'
    if index in [0, 5, 10]:
        memory = command(['shell', 'dumpsys', 'meminfo', package])
        (out / ('memory-' + str(index) + '.log')).write_text(memory, encoding='utf-8')
        match = re.search(r'(?:TOTAL PSS|TOTAL):\s*(\d+)', memory)
        if match:
            row['totalPssKb'] = int(match[1])
    samples.append(row)
    with (out / 'samples.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(row) + '\n')
    print(json.dumps(row), flush=True)
    if 'failed' in row:
        raise RuntimeError(row['failed'])
raw = command(['logcat', '-d', '--pid=' + pid, '-v', 'epoch'])
logs = '\n'.join(line for line in raw.splitlines()
                 if re.match(r'^\s*\d+\.\d+\s', line) and float(line.split()[0]) >= restoration['deviceStartEpoch'])
(out / 'process.log').write_text(logs, encoding='utf-8')
errors = [marker for marker in ['FATAL EXCEPTION', 'InternalError', 'UnsatisfiedLinkError', 'SIGSEGV', 'JNI DETECTED ERROR'] if marker in logs]
window = command(['shell', 'dumpsys', 'window', 'windows'])
activities = command(['shell', 'dumpsys', 'activity', 'activities'])
(out / 'window.log').write_text(window, encoding='utf-8')
(out / 'activities.log').write_text(activities, encoding='utf-8')
visible = any(package + '/' in block and 'MainActivity' in block and 'HAS_DRAWN' in block
              and 'isOnScreen=true' in block and (' ' + pid + ':') in block
              for block in re.split(r'(?=  Window #[0-9]+)', window))
resumed = any('ResumedActivity' in line and package in line and 'MainActivity' in line for line in activities.splitlines())
result = dict(apkSha256=restoration['apkSha256'], elapsedSeconds=time.monotonic() - started,
              pid=pid, samePidAllSamples=True, crashMarkers=errors, mainWindowVisible=visible, resumed=resumed,
              note='Idle unactivated application observation, not a full business or leak test')
(out / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
if errors or not visible or not resumed:
    raise RuntimeError('Final observation validation failed')
