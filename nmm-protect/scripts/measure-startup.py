"""Interleaved, data-preserving APK process-cold startup measurements on an authorized device."""
import datetime
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time

plan = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=False)
package, activity = plan['package'], plan['activity']
adb = ['adb', '-s', plan['serial']]
variants = plan['variants']
for variant in variants:
    variant['sha256'] = hashlib.sha256(Path(variant['apk']).read_bytes()).hexdigest()
(out / 'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')

def command(args, required=True):
    result = subprocess.run(adb + args, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    if required and result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout + result.stderr

def shell(*args, required=True):
    return command(['shell'] + list(args), required)

def drawn(window, pid):
    return any(package + '/' in block and 'MainActivity' in block and 'HAS_DRAWN' in block
               and 'isOnScreen=true' in block and (' ' + pid + ':') in block
               for block in re.split(r'(?=  Window #[0-9]+)', window))

identity = {name: shell('getprop', prop).strip() for name, prop in
            [('api', 'ro.build.version.sdk'), ('device', 'ro.product.device'), ('abi', 'ro.product.cpu.abi')]}
identity['batteryBefore'] = shell('dumpsys', 'battery')
identity['loadBefore'] = shell('cat', '/proc/loadavg')
(out / 'identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
samples = []
for round_index in range(plan['warmups'] + plan['rounds']):
    order = variants if round_index % 2 == 0 else list(reversed(variants))
    for variant in order:
        sample = {'variant': variant['name'], 'round': round_index, 'warmup': round_index < plan['warmups'],
                  'apkSha256': variant['sha256'], 'installedOverExisting': True,
                  'hostUtc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        try:
            install = command(['install', '-r', variant['apk']])
            if 'Success' not in install: raise RuntimeError(install)
            old_pid = shell('pidof', package, required=False).strip()
            shell('am', 'force-stop', package)
            if shell('pidof', package, required=False).strip(): raise RuntimeError('old process survived force-stop')
            started = time.monotonic()
            start = shell('am', 'start', '-W', '-n', activity)
            sample['amStart'] = start
            match = re.search(r'^ThisTime:\s*(\d+)', start, re.M)
            if not match or int(match[1]) <= 0 or 'Status: ok' not in start: raise RuntimeError('invalid start result')
            sample['thisTimeMs'] = int(match[1])
            pid = shell('pidof', package).strip()
            if not pid or ' ' in pid: raise RuntimeError('missing or ambiguous main PID')
            window = shell('dumpsys', 'window', 'windows')
            while not drawn(window, pid) and time.monotonic() - started < 15:
                time.sleep(.1); window = shell('dumpsys', 'window', 'windows')
            if not drawn(window, pid): raise RuntimeError('current main window not drawn/on screen')
            sample.update(pid=pid, oldPid=old_pid, oldPidExited=True, mainWindowDrawnAndOnScreen=True,
                          drawnObservationUpperBoundMs=round((time.monotonic() - started) * 1000, 2))
            activities = shell('dumpsys', 'activity', 'activities')
            sample['resumed'] = any('ResumedActivity' in line and package in line and 'MainActivity' in line
                                    for line in activities.splitlines())
            if not sample['resumed']: raise RuntimeError('main activity not resumed')
            logs = command(['logcat', '-d', '--pid=' + pid, '-v', 'threadtime'])
            sample['crashMarkers'] = [x for x in ['FATAL EXCEPTION', 'InternalError', 'UnsatisfiedLinkError',
                                                  'SIGSEGV', 'JNI DETECTED ERROR'] if x in logs]
            if sample['crashMarkers']: raise RuntimeError(str(sample['crashMarkers']))
            loader = [line for line in logs.splitlines() if 'NMMP-Loader' in line]
            sample['loader'] = loader
            if variant.get('loader') and not any('ready id=' in line for line in loader):
                raise RuntimeError('loader did not publish READY')
            if round_index == 0:
                remote = shell('pm', 'path', package).strip().removeprefix('package:')
                if shell('sha256sum', remote).split()[0] != variant['sha256']: raise RuntimeError('installed APK hash mismatch')
                (out / (variant['name'] + '-first-window.log')).write_text(window, encoding='utf-8')
                (out / (variant['name'] + '-first-activities.log')).write_text(activities, encoding='utf-8')
            if round_index % 5 == 0 or round_index == plan['warmups'] + plan['rounds'] - 1:
                sample['battery'] = shell('dumpsys', 'battery')
                sample['load'] = shell('cat', '/proc/loadavg')
                memory = shell('dumpsys', 'meminfo', package)
                (out / (variant['name'] + '-memory-' + str(round_index) + '.log')).write_text(memory, encoding='utf-8')
                pss = re.search(r'TOTAL PSS:\s*(\d+)', memory)
                if pss: sample['totalPssKb'] = int(pss[1])
            sample['valid'] = True
        except Exception as error:
            sample.update(valid=False, error=str(error))
            with (out / 'attempts.jsonl').open('a', encoding='utf-8') as f: f.write(json.dumps(sample) + '\n')
            raise
        samples.append(sample)
        with (out / 'attempts.jsonl').open('a', encoding='utf-8') as f: f.write(json.dumps(sample) + '\n')
        print('%s round=%d warmup=%s ThisTime=%dms pid=%s' %
              (variant['name'], round_index, sample['warmup'], sample['thisTimeMs'], pid), flush=True)

summary = {}
for variant in variants:
    values = [s['thisTimeMs'] for s in samples if s['variant'] == variant['name'] and not s['warmup']]
    ordered = sorted(values)
    summary[variant['name']] = dict(count=len(values), medianMs=statistics.median(values),
                                    p95Ms=ordered[(95 * len(ordered) + 99) // 100 - 1], minMs=min(values), maxMs=max(values))
reference = variants[0]['name']
for row in summary.values(): row['regressionPercentVsFirst'] = (row['medianMs'] / summary[reference]['medianMs'] - 1) * 100
(out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary), flush=True)
