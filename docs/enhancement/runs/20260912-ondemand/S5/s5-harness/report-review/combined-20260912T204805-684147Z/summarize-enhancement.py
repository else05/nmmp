"""Recompute S5 results from preserved samples; missing evidence never passes a gate."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import importlib.util

spec = importlib.util.spec_from_file_location('startup_checks', Path(__file__).with_name('measure-startup.py'))
startup_checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(startup_checks)
FORMAL_CASES = {'arithmetic', 'branch', 'shortCall', 'jniObject', 'arrayPayload',
                'exception', 'recursive', 'multiThread', 'largeShort'}


def formal_protocol(plan, startup_mode):
    if ([v['name'] for v in plan['variants']] != list('ABCD') or plan['rounds'] != 20
            or plan['warmups'] != (3 if startup_mode else 5)):
        raise ValueError('Not the frozen formal ABCD/warmup/20-round protocol')
    if not startup_mode and set(plan['cases']) != FORMAL_CASES:
        raise ValueError('Formal hotspot report requires all nine declared scenarios')


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def ledger(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]


def metrics(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return dict(count=len(values), median=statistics.median(values),
                p95=values[math.ceil(.95 * len(values)) - 1], minimum=values[0], maximum=values[-1],
                coefficientOfVariation=statistics.pstdev(values) / statistics.mean(values))


def compare(result):
    for numerator, denominator in [('B', 'A'), ('C', 'A'), ('D', 'A'), ('D', 'C')]:
        if numerator in result and denominator in result and result[denominator]['count']:
            result[numerator]['percentVs' + denominator] = (
                result[numerator]['median'] / result[denominator]['median'] - 1) * 100


def startup(path):
    if not (path / 'summary.json').exists():
        return {'complete': False, 'directory': str(path)}
    plan, rows = read(path / 'plan.json'), ledger(path / 'attempts.jsonl')
    formal_protocol(plan, True)
    resume = startup_checks.load_resume(plan) if plan.get('resumeFrom') else None
    if resume:
        if rows[:len(resume['rows'])] != resume['rows']:
            raise ValueError('Startup resume did not preserve the complete original ledger')
        startup_checks.validate_device(read(path / 'identity.json'), resume['identity'])
    valid = startup_checks.validate_ledger(plan, rows, resume['history'] if resume else [])
    variants = plan['variants']
    expected = [(v['name'], r) for r in range(plan['warmups'] + plan['rounds'])
                for v in (variants if r % 2 == 0 else list(reversed(variants)))]
    if [(row['variant'], row['round']) for row in valid] != expected:
        raise ValueError('Startup sequence mismatch: ' + str(path))
    hashes = {v['name']: v['sha256'] for v in variants}
    for row in valid:
        if (row['apkSha256'] != hashes[row['variant']] or
                row['warmup'] != (row['round'] < plan['warmups']) or
                not all(row[key] for key in ['oldPidExited', 'mainWindowDrawnAndOnScreen', 'resumed']) or
                row['crashMarkers']):
            raise ValueError('Invalid accepted startup evidence: ' + str(path))
    result = {v['name']: metrics([r['thisTimeMs'] for r in valid
                                 if r['variant'] == v['name'] and not r['warmup']]) for v in variants}
    compare(result)
    stored = read(path / 'summary.json')
    for name, value in result.items():
        if (value['median'], value['p95'], value['count']) != (
                stored[name]['medianMs'], stored[name]['p95Ms'], stored[name]['count']):
            raise ValueError('Startup summary mismatch')
        pss = []
        for row in valid:
            if row['variant'] != name or row['warmup']:
                continue
            if 'totalPssKb' in row:
                pss.append(row['totalPssKb'])
                continue
            source = path
            memory = source / (name + '-memory-' + str(row['round']) + '.log')
            while not memory.exists():
                previous = read(source / 'plan.json').get('resumeFrom')
                if not previous:
                    break
                source = Path(previous).parent
                memory = source / memory.name
            if memory.exists():
                match = re.search(r'(?:TOTAL PSS|TOTAL):\s*(\d+)', memory.read_text(encoding='utf-8'))
                if match:
                    pss.append(int(match[1]))
        value['pssKb'] = metrics(pss)
    return dict(complete=True, unit='ms', directory=str(path), variants=result,
                validAttempts=len(valid), invalidAttempts=[r for r in rows if not r['valid']],
                timingGatePassed=result['D']['percentVsA'] <= 10)


def hotspots(path):
    if not (path / 'rounds.jsonl').exists():
        return {'complete': False, 'directory': str(path)}
    plan, rows = read(path / 'plan.json'), ledger(path / 'rounds.jsonl')
    formal_protocol(plan, False)
    if any(r['case'] not in FORMAL_CASES or r['variant'] not in set('ABCD') for r in rows):
        raise ValueError('Unexpected hotspot ledger scenario or variant')
    stored = read(path / 'summary.json')
    cases = {}
    for case in plan['cases']:
        samples = [r for r in rows if r['case'] == case]
        if not samples:
            continue
        if len({r['checksum'] for r in samples}) != 1:
            raise ValueError('Hotspot checksum mismatch')
        result = {}
        for variant in plan['variants']:
            name = variant['name']
            selected = [r for r in samples if r['variant'] == name]
            expected = [(batch, r if r < 0 else r + batch * plan['rounds'] // 2)
                        for batch in range(2) for r in range(-plan['warmups'], plan['rounds'] // 2)]
            if [(r['batch'], r['round']) for r in selected] != expected:
                raise ValueError('Hotspot round sequence mismatch')
            if any(r['iterations'] != plan['selectedIterations'][case] or r['elapsedNs'] <= 0 for r in selected):
                raise ValueError('Hotspot iterations/time mismatch')
            times = [r['elapsedNs'] for r in selected if r['round'] >= 0]
            value = metrics(times)
            value['roundsUnderOneSecond'] = sum(t < 1_000_000_000 for t in times)
            memory = []
            for batch in range(2):
                raw = (path / (name + '-' + case + '-measured-' + str(batch) + '.log')).read_text(encoding='utf-8')
                process_rows = [json.loads(line) for line in raw.splitlines() if line.startswith('{')]
                batch_rows = [r for r in selected if r['batch'] == batch]
                if len(process_rows) != len(batch_rows):
                    raise ValueError('Raw hotspot log round count differs from ledger')
                for observed, saved in zip(process_rows, batch_rows):
                    observed['variant'] = name
                    observed['batch'] = batch
                    if observed['round'] >= 0:
                        observed['round'] += batch * plan['rounds'] // 2
                    if observed != saved:
                        raise ValueError('Raw hotspot result differs from ledger')
                hwm = re.search(r'^VmHWM:\s*(\d+) kB', raw, re.M)
                rss = re.search(r'^VmRSS:\s*(\d+) kB', raw, re.M)
                memory.append(dict(batch=batch, peakRssKb=int(hwm[1]) if hwm else None,
                                   finalRssKb=int(rss[1]) if rss else None))
            value['processMemory'] = memory
            if (value['median'], value['p95'], value['count']) != (
                    stored[case][name]['medianNs'], stored[case][name]['p95Ns'], stored[case][name]['count']):
                raise ValueError('Hotspot summary mismatch')
            result[name] = value
        compare(result)
        diagnostic = case == 'largeShort' and 'largeShortIterations' in plan
        cases[case] = dict(variants=result, iterations=plan['selectedIterations'][case],
                           durationLimitedDiagnostic=diagnostic,
                           timingGatePassed=result['D']['percentVsA'] <= 10,
                           durationProtocolMet=not diagnostic and all(v['roundsUnderOneSecond'] == 0 for v in result.values()))
    return dict(complete=len(cases) == len(plan['cases']), unit='ns', directory=str(path), cases=cases)


def combined_hotspots(base):
    ordinary = hotspots(base / 'hotspots')
    separate = hotspots(base / 'hotspots-large-diagnostic')
    if not separate.get('cases'):
        return ordinary
    if not ordinary.get('cases') or set(separate['cases']) != {'largeShort'}:
        raise ValueError('Unexpected separate diagnostic coverage')
    if set(ordinary['cases']) & set(separate['cases']):
        raise ValueError('Separate diagnostic would replace existing measured results')
    first = read(base / 'hotspots/identity.json')
    second = read(base / 'hotspots-large-diagnostic/identity.json')
    for key in ['api', 'abi', 'runnerSha256']:
        if first[key] != second[key]:
            raise ValueError('Diagnostic device/runner identity mismatch')
    if ([(v['name'], v['sha256']) for v in first['variants']] !=
            [(v['name'], v['sha256']) for v in second['variants']]):
        raise ValueError('Diagnostic library identity mismatch')
    if (read(base / 'hotspots/plan.json')['serial'] !=
            read(base / 'hotspots-large-diagnostic/plan.json')['serial']):
        raise ValueError('Diagnostic serial mismatch')
    result = dict(ordinary, cases={**ordinary['cases'], **separate['cases']},
                  separateDiagnosticDirectory=separate['directory'])
    result['complete'] = set(result['cases']) == FORMAL_CASES
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('work', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    seeds = {}
    for index in range(1, 4):
        base = args.work / 's5' / ('seed' + str(index))
        starts = base / ('startup-resumed' if index in (2, 3) else 'startup')
        seeds['seed' + str(index)] = dict(startup=startup(starts),
                                         startupIndependent=startup(base / 'startup-independent'),
                                         hotspots=combined_hotspots(base))
    complete = all(s['startup']['complete'] and s['startupIndependent']['complete']
                   and s['hotspots']['complete'] for s in seeds.values())
    gates = [s['startup'].get('timingGatePassed', False) for s in seeds.values()]
    gates.extend(s['startupIndependent'].get('timingGatePassed', False) for s in seeds.values())
    gates.extend(c['timingGatePassed'] and c['durationProtocolMet']
                 for s in seeds.values() for c in s['hotspots'].get('cases', {}).values())
    report = dict(seeds=seeds, measurementComplete=complete, performanceGatePassed=complete and all(gates),
                  releaseAcceptancePassed=False,
                  externalGaps=['No API26 device validation', 'Activated/server-backed business paths not validated'],
                  startupNoiseReviewComplete=all(s['startupIndependent']['complete'] for s in seeds.values()),
                  memoryScope='APK PSS is sampled after startup; harness VmHWM is process peak RSS, including ART. Neither proves absence of leaks.')
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(measurementComplete=complete, performanceGatePassed=report['performanceGatePassed'])))


if __name__ == '__main__':
    main()
