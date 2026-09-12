"""Local evidence only. Never executes ADB; writes only within this review directory.

Run from any directory: python -B <this file>
Each run preserves its fixtures, source snapshots, logs and SHA256 manifest separately.
"""
import copy
import datetime
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import types
import unittest
from contextlib import redirect_stdout, redirect_stderr
from unittest.mock import patch

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
WORK = HERE.parents[1]
ROOT = WORK.parents[2]
SCRIPTS = ROOT / 'nmm-protect/scripts'
OUT = HERE / datetime.datetime.now(datetime.timezone.utc).strftime('run-%Y%m%dT%H%M%S-%fZ')
OUT.mkdir()
READ_HASHES = {}
REJECTIONS = []


def audit(event, args):
    if event in ('subprocess.Popen', 'os.system', 'socket.connect'):
        raise AssertionError('External execution/network forbidden: ' + event)
    if event == 'open':
        path, mode, flags = args
        writing = (isinstance(mode, str) and any(c in mode for c in 'wax+')) or flags & (
            os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_APPEND | os.O_TRUNC)
        if writing and not isinstance(path, int) and not Path(path).resolve().is_relative_to(OUT):
            raise AssertionError('Write outside review output: ' + str(path))


sys.addaudithook(audit)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def rows(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]


def save_rows(path, values):
    path.write_text(''.join(json.dumps(v) + '\n' for v in values), encoding='utf-8')


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def forbidden(*args, **kwargs):
    raise AssertionError('Import must not perform CLI I/O or execute a process')


# Both imports must work with no CLI arguments and without writes/processes.
with patch.object(sys, 'argv', ['import-only']), patch('subprocess.run', side_effect=forbidden), \
        patch.object(Path, 'mkdir', side_effect=forbidden), \
        patch.object(Path, 'write_text', side_effect=forbidden), \
        patch.object(Path, 'write_bytes', side_effect=forbidden):
    collector = load_module('review_collector', SCRIPTS / 'measure-startup.py')
    report = load_module('review_summarizer', SCRIPTS / 'summarize-enhancement.py')

for name in ('measure-startup.py', 'summarize-enhancement.py'):
    path = SCRIPTS / name
    READ_HASHES[str(path)] = digest(path)
    target = OUT / 'sources' / name
    target.parent.mkdir(exist_ok=True)
    shutil.copyfile(path, target)
shutil.copyfile(Path(__file__), OUT / 'sources' / 'verify_report.py')
for seed in range(1, 4):
    base = WORK / 's5' / ('seed' + str(seed))
    for directory in base.glob('startup*'):
        if directory.is_dir():
            for path in directory.iterdir():
                if path.is_file() and (path.name in ('plan.json', 'identity.json', 'summary.json', 'attempts.jsonl')
                                       or '-memory-' in path.name):
                    READ_HASHES[str(path)] = digest(path)


def startup_copy(name, seed=1):
    source = WORK / 's5' / ('seed' + str(seed)) / ('startup' if seed == 1 else 'startup-resumed')
    target = OUT / 'fixtures' / name
    target.mkdir(parents=True)
    for filename in ('plan.json', 'identity.json', 'summary.json', 'attempts.jsonl'):
        shutil.copyfile(source / filename, target / filename)
    # Ancestor references deliberately continue to point to read-only original evidence.
    return target


HOTSPOT = OUT / 'fixtures' / 'hotspot-valid'
HOTSPOT.mkdir(parents=True)
cases = ['arithmetic', 'branch', 'shortCall', 'jniObject', 'arrayPayload',
         'exception', 'recursive', 'multiThread', 'largeShort']
plan = dict(variants=[dict(name=n) for n in 'ABCD'], warmups=5, rounds=20,
            cases={c: 1 for c in cases}, selectedIterations={c: 1 for c in cases})
samples, summaries = [], {}
for ci, case in enumerate(cases):
    summaries[case] = {}
    for n in 'ABCD':
        summaries[case][n] = dict(count=20, medianNs=1_200_000_000, p95Ns=1_200_000_000)
    order = list('ABCD')[ci % 4:] + list('ABCD')[:ci % 4]
    for batch in range(2):
        for n in order if batch == 0 else list(reversed(order)):
            raw_rows = [dict(case=case, round=r, iterations=1, elapsedNs=1_200_000_000,
                             checksum=7) for r in range(-5, 10)]
            raw = ''.join(json.dumps(r) + '\n' for r in raw_rows)
            (HOTSPOT / f'{n}-{case}-measured-{batch}.log').write_text(
                raw + 'VmHWM: 123 kB\nVmRSS: 120 kB\n', encoding='utf-8')
            for r in raw_rows:
                samples.append(dict(r, variant=n, batch=batch,
                                    round=r['round'] if r['round'] < 0 else r['round'] + batch * 10))
save(HOTSPOT / 'plan.json', plan)
save(HOTSPOT / 'summary.json', summaries)
save_rows(HOTSPOT / 'rounds.jsonl', samples)


def hotspot_copy(name):
    target = OUT / 'fixtures' / name
    shutil.copytree(HOTSPOT, target)
    return target


def run_fake_cli(plan, label, fail_install=False):
    """Exercise the actual __main__ entry and nested closures with a fake transport."""
    base = OUT / 'cli' / label
    base.mkdir(parents=True)
    p = base / 'input-plan.json'
    save(p, plan)
    output = base / 'output'
    calls, state = [], dict(pid='', variant=None)
    identity = read(Path(plan['resumeFrom']).parent / 'identity.json') if plan.get('resumeFrom') else dict(
        api='27', device='fixture-device', abi='arm64-v8a')
    prefix = [plan.get('adb', 'adb'), '-s', plan['serial']]
    apk_hashes = {v['apk']: digest(Path(v['apk'])) for v in plan['variants']}

    def respond(argv, **kwargs):
        assert argv[:3] == prefix, argv
        args = argv[3:]
        calls.append(args)
        text = ''
        if args[0] == 'install':
            assert args[1] == '-r'
            if fail_install:
                return types.SimpleNamespace(returncode=1, stdout='', stderr='fixture transport interruption')
            state['variant'] = next(v for v in plan['variants'] if v['apk'] == args[2])
            text = 'Success\n'
        elif args[0] == 'logcat':
            assert args[-2:] == ['-v', 'epoch']
            # Historical crash must be excluded; current loader ID must survive the generator closure.
            text = '999.0 321 321 E old: JNI DETECTED ERROR\n'
            if state['variant'].get('loader'):
                text += '1001.0 321 321 I NMMP-Loader: ready id=' + state['variant']['loaderId'] + '\n'
        else:
            assert args[0] == 'shell', args
            a = args[1:]
            if a[0] == 'getprop':
                text = identity[dict(zip(['ro.build.version.sdk', 'ro.product.device', 'ro.product.cpu.abi'],
                                        ['api', 'device', 'abi']))[a[1]]]
            elif a == ['dumpsys', 'battery']: text = 'fixture battery\n'
            elif a == ['cat', '/proc/loadavg']: text = '0 0 0 1/1 1\n'
            elif a == ['pidof', plan['package']]: text = state['pid']
            elif a == ['am', 'force-stop', plan['package']]: state['pid'] = ''
            elif a == ['date', '+%s']: text = '1000\n'
            elif a == ['am', 'start', '-W', '-n', plan['activity']]:
                state['pid'] = '321'
                text = 'Status: ok\nThisTime: 2500\n'
            elif a == ['dumpsys', 'window', 'windows']:
                text = '  Window #0 ' + plan['package'] + '/.MainActivity HAS_DRAWN isOnScreen=true 321:'
            elif a == ['dumpsys', 'activity', 'activities']:
                text = 'ResumedActivity ' + plan['package'] + '/.MainActivity'
            elif a == ['pm', 'path', plan['package']]: text = 'package:/fixture/base.apk\n'
            elif a == ['sha256sum', '/fixture/base.apk']:
                text = apk_hashes[state['variant']['apk']] + ' /fixture/base.apk\n'
            elif a == ['dumpsys', 'meminfo', plan['package']]: text = 'TOTAL: 54321 TOTAL SWAP PSS: 0\n'
            else: raise AssertionError('Unexpected fake transport command: ' + repr(a))
        return types.SimpleNamespace(returncode=0, stdout=text, stderr='')

    log = io.StringIO()
    try:
        with patch.object(sys, 'argv', [str(SCRIPTS / 'measure-startup.py'), str(p), str(output)]), \
                patch('subprocess.run', side_effect=respond), patch('time.sleep', side_effect=forbidden), \
                redirect_stdout(log), redirect_stderr(log):
            runpy.run_path(str(SCRIPTS / 'measure-startup.py'), run_name='__main__')
    finally:
        save(base / 'fake-transport-calls.json', calls)
        (base / 'cli.log').write_text(log.getvalue(), encoding='utf-8')
    return output, calls


class Review(unittest.TestCase):
    def reject(self, function, path, message):
        with self.assertRaisesRegex(ValueError, message) as caught:
            function(path)
        REJECTIONS.append(dict(fixture=str(path.relative_to(OUT)), exception=str(caught.exception)))

    def test_01_real_startups(self):
        results = {}
        for s in range(1, 4):
            p = WORK / f's5/seed{s}' / ('startup' if s == 1 else 'startup-resumed')
            r = report.startup(p)
            self.assertTrue(r['complete'])
            self.assertEqual(r['validAttempts'], 92)
            for v in r['variants'].values():
                self.assertEqual(v['count'], 20)
                self.assertEqual(v['pssKb']['count'], 5)
            results['seed' + str(s)] = r
        save(OUT / 'real-startups.json', results)

    def test_02_dropped_interruption(self):
        p = startup_copy('startup-dropped-interruption', 2)
        save_rows(p / 'attempts.jsonl', [r for r in rows(p / 'attempts.jsonl') if r['valid']])
        self.reject(report.startup, p, 'complete original ledger')

    def test_03_wrong_loader_id(self):
        p = startup_copy('startup-wrong-loader', 2)
        values = rows(p / 'attempts.jsonl')
        # Seed1 predates loaderId in plans. Use a seed2 row after its preserved prefix
        # so rejection specifically exercises loader identity, not prefix equality.
        next(r for r in values[53:] if r['variant'] == 'B')['loader'] = ['ready id=ffffffff']
        save_rows(p / 'attempts.jsonl', values)
        self.reject(report.startup, p, 'wrong loader identity')

    def test_04_zero_measured_time(self):
        p = startup_copy('startup-zero-time')
        values = rows(p / 'attempts.jsonl')
        min((r for r in values if not r['warmup'] and r['variant'] == 'A'),
            key=lambda r: r['thisTimeMs'])['thisTimeMs'] = 0
        save_rows(p / 'attempts.jsonl', values)
        self.reject(report.startup, p, 'Invalid successful sample')

    def test_05_resume_reason_and_protocol(self):
        for key, value, message in [('resumeReason', ' ', 'explicit interruption reason'),
                                    ('serial', 'other-device', 'Resume protocol mismatch')]:
            p = startup_copy('startup-bad-' + key, 2)
            plan = read(p / 'plan.json'); plan[key] = value; save(p / 'plan.json', plan)
            self.reject(report.startup, p, message)

    def test_06_full_hotspot_positive_control(self):
        result = report.hotspots(HOTSPOT)
        self.assertTrue(result['complete'])
        self.assertEqual(len(result['cases']), 9)
        self.assertTrue(all(c['timingGatePassed'] and c['durationProtocolMet'] for c in result['cases'].values()))
        save(OUT / 'synthetic-hotspot-positive.json', result)

    def test_07_formal_protocol_mutations(self):
        for name in ['one-case-two-rounds', 'one-case', 'warmups', 'rounds', 'variant-order']:
            p = hotspot_copy('hotspot-' + name)
            plan = read(p / 'plan.json')
            if name.startswith('one-case'): plan['cases'] = {'largeShort': 1}
            if name == 'one-case-two-rounds':
                plan.update(warmups=0, rounds=2)
                # Preserve the former counterexample's internal consistency: the old
                # summarizer accepted these eight records, not a mismatched ledger.
                values = [r for r in rows(p / 'rounds.jsonl')
                          if r['case'] == 'largeShort' and r['round'] in (0, 10)]
                for r in values:
                    r['round'] = r['batch']
                    raw = {k: v for k, v in r.items() if k not in ('batch', 'variant')}
                    raw['round'] = 0
                    (p / f"{r['variant']}-largeShort-measured-{r['batch']}.log").write_text(
                        json.dumps(raw) + '\nVmHWM: 123 kB\nVmRSS: 120 kB\n', encoding='utf-8')
                save_rows(p / 'rounds.jsonl', values)
                save(p / 'summary.json', {'largeShort': {n: dict(count=2, medianNs=1_200_000_000,
                      p95Ns=1_200_000_000) for n in 'ABCD'}})
            if name == 'warmups': plan['warmups'] = 0
            if name == 'rounds': plan['rounds'] = 2
            if name == 'variant-order': plan['variants'].reverse()
            save(p / 'plan.json', plan)
            self.reject(report.hotspots, p, 'formal|Formal')
        p = startup_copy('startup-wrong-warmups')
        plan = read(p / 'plan.json'); plan['warmups'] = 0; save(p / 'plan.json', plan)
        self.reject(report.startup, p, 'frozen formal')

    def test_08_raw_ledger_mismatch(self):
        p = hotspot_copy('hotspot-uniform-wrong-checksum')
        values = rows(p / 'rounds.jsonl')
        for value in values: value['checksum'] = 0
        save_rows(p / 'rounds.jsonl', values)
        self.reject(report.hotspots, p, 'Raw hotspot result differs')
        for name in ['missing', 'round', 'time', 'checksum']:
            p = hotspot_copy('hotspot-raw-' + name)
            path = p / 'A-arithmetic-measured-1.log'
            lines = path.read_text(encoding='utf-8').splitlines()
            if name == 'missing': del lines[5]
            else:
                r = json.loads(lines[5]); r[{'round': 'round', 'time': 'elapsedNs', 'checksum': 'checksum'}[name]] += 1
                lines[5] = json.dumps(r)
            path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            self.reject(report.hotspots, p, 'Raw hotspot')

    def test_09_self_test_entry(self):
        log = io.StringIO()
        with patch.object(sys, 'argv', [str(SCRIPTS / 'measure-startup.py'), '--self-test', str(WORK / 's5/seed2')]), \
                redirect_stdout(log), redirect_stderr(log), self.assertRaises(SystemExit) as caught:
            runpy.run_path(str(SCRIPTS / 'measure-startup.py'), run_name='__main__')
        (OUT / 'collector-self-test.log').write_text(log.getvalue(), encoding='utf-8')
        self.assertEqual(caught.exception.code, 0)
        self.assertIn('Ran 6 tests', log.getvalue())

    def test_10_cli_fresh_repeated_closures(self):
        for index in (1, 2):
            apk = OUT / f'fake-apk-{index}.bin'; apk.write_bytes(b'local fixture, not an APK')
            apk_b = OUT / f'fake-apk-B-{index}.bin'; apk_b.write_bytes(b'local loader fixture, not an APK')
            plan = dict(serial=f'fixture-{index}', adb=f'FAKE_ONLY_{index}', package=f'review{index}.app',
                        activity=f'review{index}.app/.MainActivity', warmups=0, rounds=2,
                        variants=[dict(name='A', apk=str(apk), loader=False),
                                  dict(name='B', apk=str(apk_b), loader=True, loaderId='12345678')])
            output, calls = run_fake_cli(plan, 'fresh-' + str(index))
            self.assertEqual([(r['variant'], r['round']) for r in rows(output / 'attempts.jsonl')],
                             [('A', 0), ('B', 0), ('B', 1), ('A', 1)])
            self.assertTrue(all(r['valid'] and r['totalPssKb'] == 54321 for r in rows(output / 'attempts.jsonl')))
            self.assertEqual(read(output / 'summary.json')['B']['count'], 2)

    def test_11_cli_resume_closures(self):
        plan = read(WORK / 's5/seed2/startup-resumed/plan.json')
        output, calls = run_fake_cli(plan, 'resume-real-prefix')
        old = Path(plan['resumeFrom']).read_text(encoding='utf-8')
        self.assertTrue((output / 'attempts.jsonl').read_text(encoding='utf-8').startswith(old))
        self.assertEqual(len(rows(output / 'attempts.jsonl')), 93)
        self.assertEqual(sum(c[0] == 'install' for c in calls), 40)
        self.assertTrue(all(v['count'] == 20 for v in read(output / 'summary.json').values()))

    def test_12_cli_failure_ledger(self):
        apk = OUT / 'fake-apk-1.bin'
        plan = dict(serial='fixture-failure', package='failure.app', activity='failure.app/.MainActivity',
                    warmups=0, rounds=1, variants=[dict(name='A', apk=str(apk))])
        with self.assertRaisesRegex(RuntimeError, 'fixture transport interruption'):
            run_fake_cli(plan, 'install-failure', fail_install=True)
        output = OUT / 'cli/install-failure/output'
        self.assertFalse(rows(output / 'attempts.jsonl')[0]['valid'])
        self.assertFalse((output / 'summary.json').exists())


log = io.StringIO()
with patch('subprocess.run', side_effect=forbidden):
    result = unittest.TextTestRunner(stream=log, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Review))
unchanged = all(digest(Path(path)) == sha for path, sha in READ_HASHES.items())
main_code = collector.main.__code__
closures = []
def inspect_closures(code):
    for c in code.co_consts:
        if isinstance(c, types.CodeType):
            closures.append(dict(name=c.co_name, firstLine=c.co_firstlineno, freeVariables=c.co_freevars))
            inspect_closures(c)
inspect_closures(main_code)
save(OUT / 'closure-inspection.json', dict(mainCellVariables=main_code.co_cellvars, nestedClosures=closures))
(OUT / 'tests.log').write_text(log.getvalue(), encoding='utf-8')
save(OUT / 'results.json', dict(testsRun=result.testsRun, failures=len(result.failures), errors=len(result.errors),
    passed=result.wasSuccessful() and unchanged, originalsUnchanged=unchanged, readOnlyImportPassed=True,
    adbInvocations=0, externalProcesses=0, rejections=REJECTIONS,
    limitation='CLI transport responses and hotspot samples are synthetic; these are collector/report tests, not device or performance evidence.'))
save(OUT / 'source-and-evidence-sha256.json', READ_HASHES)
save(OUT / 'artifact-sha256.json', {str(p.relative_to(OUT)): digest(p) for p in OUT.rglob('*') if p.is_file()})
print(log.getvalue())
print('Evidence: ' + str(OUT))
if not unchanged: print('ERROR: an original source/evidence file changed during review')
sys.exit(0 if result.wasSuccessful() and unchanged else 1)
