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

def expected_sequence(plan):
    if (type(plan['warmups']) is not int or plan['warmups'] < 0
            or type(plan['rounds']) is not int or plan['rounds'] <= 0):
        raise ValueError('Invalid warmup or measured round count')
    variants = plan['variants']
    if not variants or len({v['name'] for v in variants}) != len(variants):
        raise ValueError('Variant names must be unique and nonempty')
    return [(v['name'], r) for r in range(plan['warmups'] + plan['rounds'])
            for v in (variants if r % 2 == 0 else list(reversed(variants)))]

def validate_protocol(plan, previous):
    for key in ['serial', 'package', 'activity', 'warmups', 'rounds']:
        if plan[key] != previous[key]:
            raise ValueError('Resume protocol mismatch: ' + key)
    def variants(p):
        return [(v['name'], v['sha256'], v.get('loader', False)) for v in p['variants']]
    if variants(plan) != variants(previous):
        raise ValueError('Resume variant order, APK hash or loader mode mismatch')
    for current, old in zip(plan['variants'], previous['variants']):
        if old.get('loaderId') and current.get('loaderId') != old['loaderId']:
            raise ValueError('Resume loader identity changed or removed')

def validate_ledger(plan, rows, history):
    expected = expected_sequence(plan)
    variants = {v['name']: v for v in plan['variants']}
    reasons = {item['ledgerIndex']: item['reason'] for item in history}
    if len(reasons) != len(history) or any(not isinstance(r, str) or not r.strip() for r in reasons.values()):
        raise ValueError('Every interruption requires one explicit reason')
    valid, interrupted = [], set()
    for index, row in enumerate(rows):
        if len(valid) >= len(expected) or (row.get('variant'), row.get('round')) != expected[len(valid)]:
            raise ValueError('Ledger skips, duplicates or reorders a scheduled attempt')
        variant = variants[row['variant']]
        if (type(row['round']) is not int or row.get('apkSha256') != variant['sha256']
                or row.get('warmup') is not (row['round'] < plan['warmups'])):
            raise ValueError('Ledger APK identity or warmup flag mismatch')
        if row.get('valid') is False:
            if index not in reasons:
                raise ValueError('Undocumented interruption at ledger index ' + str(index))
            interrupted.add(index)
            continue
        if row.get('valid') is not True or type(row.get('thisTimeMs')) is not int or row['thisTimeMs'] <= 0:
            raise ValueError('Invalid successful sample')
        if (any(row.get(key) is not True for key in ['oldPidExited', 'mainWindowDrawnAndOnScreen', 'resumed'])
                or row.get('crashMarkers') != []):
            raise ValueError('Successful sample lacks startup validation')
        loader = row.get('loader', [])
        if variant.get('loader') and not any('ready id=' in line for line in loader):
            raise ValueError('Successful sample lacks loader READY')
        if variant.get('loaderId') and not any(re.search(
                r'ready id=' + re.escape(variant['loaderId']) + r'(?=\s|$)', line) for line in loader):
            raise ValueError('Successful sample has wrong loader identity')
        valid.append(row)
    if interrupted != set(reasons):
        raise ValueError('Resume history does not match the complete ledger')
    return valid

def read_resume_source(path):
    path = Path(path)
    return {'text': path.read_text(encoding='utf-8'),
            'plan': json.loads((path.parent / 'plan.json').read_text(encoding='utf-8')),
            'identity': json.loads((path.parent / 'identity.json').read_text(encoding='utf-8'))}

def validate_device(identity, previous):
    if any(identity[key] != previous[key] for key in ['api', 'device', 'abi']):
        raise ValueError('Resume device identity mismatch')

def load_resume(plan, read_source=read_resume_source, seen=()):
    path = str(Path(plan['resumeFrom']).resolve())
    reason = plan.get('resumeReason')
    if not isinstance(reason, str) or not reason.strip():
        raise ValueError('Resume requires an explicit interruption reason')
    if path in seen:
        raise ValueError('Cyclic resume ancestry')
    prior = read_source(plan['resumeFrom'])
    validate_protocol(plan, prior['plan'])
    rows = [json.loads(line) for line in prior['text'].splitlines() if line]
    if not rows or rows[-1].get('valid') is not False:
        raise ValueError('Resume requires a final interrupted attempt')
    history = []
    if prior['plan'].get('resumeFrom'):
        ancestor = load_resume(prior['plan'], read_source, seen + (path,))
        if rows[:len(ancestor['rows'])] != ancestor['rows']:
            raise ValueError('Resume ledger does not preserve its complete ancestor prefix')
        validate_device(prior['identity'], ancestor['identity'])
        history.extend(ancestor['history'])
    history.append({'ledgerIndex': len(rows) - 1, 'source': plan['resumeFrom'], 'reason': reason})
    valid = validate_ledger(plan, rows, history)
    return dict(prior, rows=rows, history=history, valid=valid)

def summarize(plan, samples):
    valid = validate_ledger(plan, samples, [])
    if len(valid) != len(expected_sequence(plan)):
        raise ValueError('Incomplete valid sample sequence')
    summary = {}
    for variant in plan['variants']:
        ordered = sorted(s['thisTimeMs'] for s in valid if s['variant'] == variant['name'] and not s['warmup'])
        summary[variant['name']] = dict(count=len(ordered), medianMs=statistics.median(ordered),
                                        p95Ms=ordered[(95 * len(ordered) + 99) // 100 - 1],
                                        minMs=min(ordered), maxMs=max(ordered))
    reference = plan['variants'][0]['name']
    for row in summary.values():
        row['regressionPercentVsFirst'] = (row['medianMs'] / summary[reference]['medianMs'] - 1) * 100
    if 'D' in summary and 'C' in summary:
        summary['D']['regressionPercentVsC'] = (summary['D']['medianMs'] / summary['C']['medianMs'] - 1) * 100
    return summary

def local_fixtures(seed2):
    """Read saved seed2 evidence; all mutations are in-memory and ADB/writes are forbidden."""
    import copy
    import unittest
    from unittest.mock import patch

    files = [directory / name for directory in [seed2 / 'startup-retry1', seed2 / 'startup-resumed']
             for name in ['plan.json', 'identity.json', 'attempts.jsonl']]
    files.append(seed2 / 'startup-resumed/summary.json')
    before = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    previous = read_resume_source(seed2 / 'startup-retry1/attempts.jsonl')
    resumed = read_resume_source(seed2 / 'startup-resumed/attempts.jsonl')
    complete = [json.loads(line) for line in resumed['text'].splitlines() if line]

    class Fixtures(unittest.TestCase):
        def setUp(self):
            self.plan = copy.deepcopy(resumed['plan'])
            self.sources = {self.plan['resumeFrom']: copy.deepcopy(previous)}

        def load(self, plan=None):
            return load_resume(plan or self.plan, self.sources.__getitem__)

        def interrupted_again(self):
            failure = dict(complete[60], valid=False, error='Fixture transport interruption')
            self.sources['fixture-second'] = dict(copy.deepcopy(resumed),
                text='\n'.join(json.dumps(row) for row in complete[:60] + [failure]))
            return dict(self.plan, resumeFrom='fixture-second', resumeReason='Documented second interruption')

        def test_saved_seed2_and_summary(self):
            result = self.load()
            self.assertEqual(len(result['valid']), 52)
            self.assertEqual(len(result['rows']), 53)
            self.assertEqual(complete[:53], result['rows'])
            valid = validate_ledger(self.plan, complete, result['history'])
            self.assertEqual(len(valid), 92)
            summary = summarize(self.plan, valid)
            old = json.loads(files[-1].read_text(encoding='utf-8'))
            for variant, values in old.items():
                for key, value in values.items():
                    self.assertEqual(summary[variant][key], value)
                self.assertEqual(summary[variant]['count'], 20)
            self.assertAlmostEqual(summary['D']['regressionPercentVsC'], 1.4475061039413983)

        def test_protocol_changes_rejected(self):
            changes = {'serial': 'different-device', 'package': 'other.app', 'activity': 'other/.Main',
                       'warmups': 4, 'rounds': 19}
            for key, value in changes.items():
                with self.subTest(key=key), self.assertRaises(ValueError):
                    self.load(dict(self.plan, **{key: value}))
            for change in ['order', 'hash', 'loader', 'loaderId', 'duplicate']:
                bad = copy.deepcopy(self.plan)
                if change == 'order': bad['variants'].reverse()
                elif change == 'hash': bad['variants'][0]['sha256'] = 'changed'
                elif change == 'loader': bad['variants'][1]['loader'] = False
                elif change == 'loaderId': bad['variants'][1]['loaderId'] = 'wrong-id'
                else: bad['variants'][1]['name'] = bad['variants'][0]['name']
                with self.subTest(change=change), self.assertRaises(ValueError): self.load(bad)
            bad = copy.deepcopy(self.plan)
            del bad['variants'][1]['loaderId']
            with self.assertRaises(ValueError): validate_protocol(bad, self.plan)

        def test_multiple_interruptions_and_successful_retry(self):
            second = self.interrupted_again()
            result = self.load(second)
            self.assertEqual(len(result['valid']), 59)
            self.assertEqual([h['ledgerIndex'] for h in result['history']], [52, 60])
            self.sources['fixture-third'] = dict(self.sources['fixture-second'], plan=second,
                text=self.sources['fixture-second']['text'] + '\n' + json.dumps(result['rows'][-1]))
            third = dict(second, resumeFrom='fixture-third', resumeReason='Documented third interruption')
            retried = self.load(third)
            self.assertEqual(len(retried['valid']), 59)
            self.assertEqual([h['ledgerIndex'] for h in retried['history']], [52, 60, 61])
            final = validate_ledger(third, retried['rows'] + complete[60:], retried['history'])
            self.assertEqual(final, [r for r in complete if r['valid']])
            self.assertEqual(summarize(third, final)['D']['count'], 20)

        def test_history_reasons_and_prefix_required(self):
            second = self.interrupted_again()
            for reason in ['', '   ', None]:
                with self.subTest(reason=reason), self.assertRaises(ValueError):
                    self.load(dict(second, resumeReason=reason))
            self.sources['fixture-second']['plan']['resumeReason'] = ' '
            with self.assertRaises(ValueError): self.load(second)
            second = self.interrupted_again()
            rows = [json.loads(line) for line in self.sources['fixture-second']['text'].splitlines()]
            rows[0]['thisTimeMs'] += 1
            self.sources['fixture-second']['text'] = '\n'.join(map(json.dumps, rows))
            with self.assertRaises(ValueError): self.load(second)
            result = self.load()
            with self.assertRaises(ValueError): validate_ledger(self.plan, result['rows'], [])
            with self.assertRaises(ValueError): validate_ledger(self.plan, result['rows'], result['history'] * 2)

        def test_bad_ledger_rejected(self):
            result = self.load()
            for change in ['gap', 'duplicate', 'warmup', 'hash', 'time', 'valid', 'drawn']:
                rows = copy.deepcopy(result['rows'])
                if change == 'gap': del rows[0]
                elif change == 'duplicate': rows.insert(1, copy.deepcopy(rows[0]))
                elif change == 'warmup': rows[0]['warmup'] = False
                elif change == 'hash': rows[-1]['apkSha256'] = 'wrong'
                elif change == 'time': rows[0]['thisTimeMs'] = 0
                elif change == 'valid': rows[0]['valid'] = 1
                else: rows[0]['mainWindowDrawnAndOnScreen'] = False
                with self.subTest(change=change), self.assertRaises(ValueError):
                    validate_ledger(self.plan, rows, result['history'])
            with self.assertRaises(ValueError): summarize(self.plan, result['valid'])

        def test_device_cycle_and_finished_source_rejected(self):
            second = self.interrupted_again()
            self.sources['fixture-second']['identity']['api'] = '26'
            with self.assertRaises(ValueError): self.load(second)
            source = self.sources[self.plan['resumeFrom']]
            source['plan'].update(resumeFrom=self.plan['resumeFrom'], resumeReason='Cycle fixture')
            with self.assertRaises(ValueError): self.load()
            self.sources['finished'] = copy.deepcopy(resumed)
            with self.assertRaises(ValueError):
                self.load(dict(self.plan, resumeFrom='finished', resumeReason='Already finished'))

    with patch('subprocess.run', side_effect=AssertionError('ADB/subprocess forbidden')), \
         patch.object(Path, 'write_text', side_effect=AssertionError('Fixture writes forbidden')), \
         patch.object(Path, 'write_bytes', side_effect=AssertionError('Fixture writes forbidden')):
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Fixtures))
    if any(hashlib.sha256(p.read_bytes()).hexdigest() != digest for p, digest in before.items()):
        raise AssertionError('Saved seed2 evidence changed')
    return result.wasSuccessful()

def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--self-test':
        fixture = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / 'build/on-demand-20260912/s5/seed2'
        sys.exit(0 if local_fixtures(fixture) else 1)

    plan = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
    out = Path(sys.argv[2])
    package, activity = plan['package'], plan['activity']
    adb = [plan.get('adb', 'adb')]
    if 'adbServerPort' in plan:
        port = plan['adbServerPort']
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError('Invalid local ADB server port')
        adb += ['-P', str(port)]
    adb += ['-s', plan['serial']]
    variants = plan['variants']
    for variant in variants:
        variant['sha256'] = hashlib.sha256(Path(variant['apk']).read_bytes()).hexdigest()
    expected_sequence(plan)
    resume = load_resume(plan) if plan.get('resumeFrom') else None
    if resume:
        plan['resumeHistory'] = resume['history']
    out.mkdir(parents=True, exist_ok=False)
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
    resume_count = 0
    if resume:
        validate_device(identity, resume['identity'])
        samples.extend(resume['valid'])
        resume_count = len(samples)
        text = resume['text']
        (out / 'attempts.jsonl').write_text(text + ('' if text.endswith('\n') else '\n'), encoding='utf-8')
        print('Resuming after %d valid attempts; %d interruptions remain in the ledger' %
              (resume_count, len(resume['history'])), flush=True)
    ordinal = 0
    for round_index in range(plan['warmups'] + plan['rounds']):
        order = variants if round_index % 2 == 0 else list(reversed(variants))
        for variant in order:
            ordinal += 1
            if ordinal <= resume_count:
                continue
            sample = {'variant': variant['name'], 'round': round_index, 'warmup': round_index < plan['warmups'],
                      'apkSha256': variant['sha256'], 'installedOverExisting': True,
                      'hostUtc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
            try:
                install = command(['install', '-r', variant['apk']])
                if 'Success' not in install: raise RuntimeError(install)
                old_pid = shell('pidof', package, required=False).strip()
                shell('am', 'force-stop', package)
                if shell('pidof', package, required=False).strip(): raise RuntimeError('old process survived force-stop')
                sample['deviceStartEpoch'] = int(shell('date', '+%s').strip())
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
                raw_logs = command(['logcat', '-d', '--pid=' + pid, '-v', 'epoch'])
                logs = '\n'.join(line for line in raw_logs.splitlines()
                                 if re.match(r'^\s*\d+\.\d+\s', line)
                                 and float(line.split()[0]) >= sample['deviceStartEpoch'])
                sample['crashMarkers'] = [x for x in ['FATAL EXCEPTION', 'InternalError', 'UnsatisfiedLinkError',
                                                      'SIGSEGV', 'JNI DETECTED ERROR'] if x in logs]
                if sample['crashMarkers']:
                    (out / ('failed-process-' + variant['name'] + '-' + str(round_index) + '.log')).write_text(raw_logs, encoding='utf-8')
                    raise RuntimeError(str(sample['crashMarkers']))
                loader = [line for line in logs.splitlines() if 'NMMP-Loader' in line]
                sample['loader'] = loader
                if variant.get('loader') and not any('ready id=' in line for line in loader):
                    raise RuntimeError('loader did not publish READY')
                if variant.get('loaderId') and not any('ready id=' + variant['loaderId'] in line for line in loader):
                    raise RuntimeError('loader build identity mismatch')
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
                    pss = re.search(r'(?:TOTAL PSS|TOTAL):\s*(\d+)', memory)
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

    summary = summarize(plan, samples)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
