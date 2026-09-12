"""Run a production-generated ART harness. Preserve all rounds and fail on mismatches."""
from pathlib import Path
import hashlib
import json
import math
import statistics
import subprocess
import sys
import threading
import datetime

plan = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
deferred_cases = set(plan.get('deferredCases', []))
if not deferred_cases.issubset(plan['cases']):
    raise ValueError('Unknown deferred scenario')
out = Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=False)
adb = [plan.get('adb', 'adb'), '-s', plan['serial']]
remote = '/data/local/tmp/nmmp-on-demand/bench'


def command(args):
    return subprocess.check_output(adb + args, text=True, encoding='utf-8', errors='replace', timeout=120)


command(['shell', 'mkdir', '-p', remote])
command(['push', plan['runner'], remote + '/runner.dex'])
for variant in plan['variants']:
    path = Path(variant['library'])
    variant['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    command(['push', str(path), remote + '/' + variant['name'] + '.so'])
(out / 'identity.json').write_text(json.dumps({
    'api': command(['shell', 'getprop', 'ro.build.version.sdk']).strip(),
    'abi': command(['shell', 'getprop', 'ro.product.cpu.abi']).strip(),
    'battery': command(['shell', 'dumpsys', 'battery']),
    'load': command(['shell', 'cat', '/proc/loadavg']),
    'runnerSha256': hashlib.sha256(Path(plan['runner']).read_bytes()).hexdigest(),
    'variants': plan['variants']}, indent=2), encoding='utf-8')


def run(variant, case, iterations, warmups, rounds, label, timeout_seconds=600):
    args = ['shell', 'CLASSPATH=' + remote + '/runner.dex', 'app_process64', '/system/bin',
            'bench.BenchMain', remote + '/' + variant['name'] + '.so', case,
            str(iterations), str(warmups), str(rounds)]
    samples = []
    with (out / (variant['name'] + '-' + case + '-' + label + '.log')).open('w', encoding='utf-8') as log:
        proc = subprocess.Popen(adb + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding='utf-8', errors='replace')
        watchdog = threading.Timer(timeout_seconds, proc.kill)
        watchdog.start()
        try:
            for line in proc.stdout:
                watchdog.cancel()
                watchdog = threading.Timer(timeout_seconds, proc.kill)
                watchdog.start()
                log.write(line)
                log.flush()
                if line.startswith('{'):
                    row = json.loads(line)
                    row['variant'] = variant['name']
                    samples.append(row)
                    if row['round'] % 5 == 0:
                        print('%s %s %s round=%d %.1fms' %
                              (label, variant['name'], case, row['round'], row['elapsedNs'] / 1e6), flush=True)
            if proc.wait() != 0:
                raise RuntimeError('ART harness failed or exceeded its %ds no-output budget: %s/%s' %
                                   (timeout_seconds, variant['name'], case))
        finally:
            watchdog.cancel()
            if proc.poll() is None:
                proc.kill()
                proc.wait()
    if len(samples) != warmups + rounds or [r['round'] for r in samples] != list(range(-warmups, rounds)):
        raise RuntimeError('Missing rounds')
    if any(r['iterations'] != iterations or r['case'] != case or r['elapsedNs'] <= 0 for r in samples):
        raise RuntimeError('Invalid round')
    if len({r['checksum'] for r in samples}) != 1:
        raise RuntimeError('Unstable result')
    return samples


all_samples = []
summary = {}
for case_index, (case, initial) in enumerate(plan['cases'].items()):
    if case in deferred_cases:
        print('DEFERRED ' + case, flush=True)
        continue
    variants = plan['variants'][case_index % len(plan['variants']):] + plan['variants'][:case_index % len(plan['variants'])]
    calibration = []
    for variant in variants:
        rows = run(variant, case, initial, 1, 2, 'calibration')
        calibration.extend(rows)
    if len({r['checksum'] for r in calibration}) != 1:
        raise RuntimeError('Variant result mismatch during calibration')
    fastest = min(statistics.median(r['elapsedNs'] for r in calibration
                                   if r['variant'] == v['name'] and r['round'] >= 0) for v in variants)
    iterations = max(initial, math.ceil(initial * 1_500_000_000 / fastest))
    if case == 'largeShort' and 'largeShortIterations' in plan:
        iterations = plan['largeShortIterations']
        if type(iterations) is not int or iterations <= 0 or iterations > 2_147_483_647:
            raise ValueError('Invalid largeShort iteration count')
    slowest = max(statistics.median(r['elapsedNs'] for r in calibration
                                   if r['variant'] == v['name'] and r['round'] >= 0) for v in variants)
    timeout_seconds = max(600, math.ceil(3 * slowest * iterations / initial / 1_000_000_000))
    plan.setdefault('selectedIterations', {})[case] = iterations
    plan.setdefault('selectedTimeoutSeconds', {})[case] = timeout_seconds
    (out / 'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    (out / (case + '-calibration.json')).write_text(json.dumps(calibration, indent=2), encoding='utf-8')
    measured = []
    if plan['rounds'] % 2:
        raise ValueError('Balanced batches require an even number of rounds')
    for batch in range(2):
        order = variants if batch == 0 else list(reversed(variants))
        for variant in order:
            rows = run(variant, case, iterations, plan['warmups'], plan['rounds'] // 2,
                       'measured-' + str(batch), timeout_seconds)
            for row in rows:
                row['batch'] = batch
                if row['round'] >= 0:
                    row['round'] += batch * (plan['rounds'] // 2)
            measured.extend(rows)
    if len({r['checksum'] for r in measured}) != 1:
        raise RuntimeError('Variant result mismatch')
    all_samples.extend(measured)
    with (out / 'rounds.jsonl').open('w', encoding='utf-8') as stream:
        for row in all_samples:
            stream.write(json.dumps(row) + '\n')
    result = {}
    for variant in plan['variants']:
        values = sorted(r['elapsedNs'] for r in measured if r['variant'] == variant['name'] and r['round'] >= 0)
        result[variant['name']] = dict(count=len(values), medianNs=statistics.median(values),
                                      p95Ns=values[math.ceil(.95 * len(values)) - 1], minNs=min(values),
                                      roundsUnderOneSecond=sum(v < 1_000_000_000 for v in values))
    for row in result.values():
        row['regressionPercentVsFirst'] = (row['medianNs'] / result[plan['variants'][0]['name']]['medianNs'] - 1) * 100
    summary[case] = result
    if 'C' in result and 'D' in result:
        result['D']['regressionPercentVsC'] = (result['D']['medianNs'] / result['C']['medianNs'] - 1) * 100
    (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    (out / (case + '-environment-after.json')).write_text(json.dumps({
        'hostUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'battery': command(['shell', 'dumpsys', 'battery']),
        'load': command(['shell', 'cat', '/proc/loadavg'])}, indent=2), encoding='utf-8')
    print(json.dumps({case: result}), flush=True)
