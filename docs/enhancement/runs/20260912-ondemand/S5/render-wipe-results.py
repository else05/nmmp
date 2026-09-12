from pathlib import Path
import json
import hashlib
import math
import statistics

w = Path(__file__).resolve().parent
root = w.parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8')) if path.exists() else None


def validate_device_results(rows):
    modes = {'Release', 'Debug', 'legacy-Release', 'legacy-Debug'}
    if len(rows) != 4 or {r['mode'] for r in rows} != modes:
        raise ValueError('Incomplete device mode coverage')
    base = w / 'wipe-device-fixed'
    identity = read(base / 'identity.json')
    if identity['api'] != '27' or identity['abi'] != 'arm64-v8a':
        raise ValueError('Wipe device identity mismatch')
    commands = [json.loads(s) for s in (base / 'commands.jsonl').read_text(encoding='utf-8').splitlines() if s]
    if not commands or any(c['code'] != 0 for c in commands):
        raise ValueError('Wipe device command did not exit successfully')
    for row in rows:
        legacy = row['mode'].startswith('legacy-')
        mode = row['mode'].removeprefix('legacy-')
        build = w / ('wipe-fixed-' + ('legacy-' if legacy else '') + 'semantic-' + mode)
        expected = {'semantic': 'SEMANTIC_PASS checks=' + ('1338' if legacy else '1351')}
        if not legacy:
            expected.update(nativeVm='PASS: 105 independent Java/native program vectors',
                            root='PASS: native VM recovers and publishes root once across 1600 concurrent activations',
                            wipe='DECODE_WIPE_PASS runs=801')
        outputs = {'semantic': row['output']} if legacy else row['outputs']
        for key, marker in expected.items():
            if marker not in outputs[key] or not any(c['output'] == outputs[key] for c in commands):
                raise ValueError('Missing successful wipe device output: ' + key)
        expected_libraries = {'libnmmvm.so', 'libnmmp_semantic.so'}
        if not legacy:
            expected_libraries |= {'nmmp_native_vm_test', 'nmmp_native_root_test'}
        if set(row['librarySha256']) != expected_libraries:
            raise ValueError('Incomplete wipe device library identity')
        for name, sha in row['librarySha256'].items():
            path = build / ('vm/libnmmvm.so' if name == 'libnmmvm.so' else name)
            if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
                raise ValueError('Wipe device library hash mismatch')


result = dict(artifact=read(w / 'final-wipe-acceptance/structure-audit.json'),
              build=read(w / 'wipe-build-checks.json'), device=read(w / 'wipe-device-fixed/summary.json'),
              legacyDevice=read(w / 'wipe-legacy-device/summary.json'),
              finalDevice=read(w / 'wipe-final-device/summary.json'),
              stability=read(w / 'wipe-final-stability/summary.json'),
              performanceScope='Seed1 arithmetic/branch/shortCall only, not final three-seed acceptance',
              releaseAcceptancePassed=False, cases={})
if result['device']:
    validate_device_results(result['device'])
result['observerShutdownDiagnosis'] = dict(
    failedCommands='wipe-device/commands.jsonl', crashBuffer='wipe-device/crash-buffer.log',
    nohook=read(w / 'wipe-shutdown-diagnosis/nohook-result.json'),
    fixedBridgeHost=read(w / 'wipe-host/tls-free-review/publication/results.json'))
base = w / 'wipe-hotspots'
if (base / 'summary.json').exists():
    plan, stored = read(base / 'plan.json'), read(base / 'summary.json')
    identity = read(base / 'identity.json')
    frozen_identity = read(w / 's5/seed1/hotspots/identity.json')
    frozen_plan = read(w / 's5/seed1/hotspots/plan.json')
    if (any(identity[k] != frozen_identity[k] for k in ['api', 'abi', 'runnerSha256'])
            or plan['serial'] != frozen_plan['serial']
            or hashlib.sha256(Path(plan['runner']).read_bytes()).hexdigest() != identity['runnerSha256']):
        raise ValueError('Post-review device/runner identity mismatch')
    expected_libraries = {'A': w / 's5/seed1/hotspot-A', 'C': w / 's5/seed1/hotspot-D', 'D': w / 'wipe-hotspot-D'}
    if [v['name'] for v in identity['variants']] != ['A', 'C', 'D']:
        raise ValueError('Post-review library identity order mismatch')
    for planned, observed in zip(plan['variants'], identity['variants']):
        path = expected_libraries[observed['name']] / 'build/obj/strip/arm64-v8a/libc++_en.so'
        if (planned != observed or Path(planned['library']).resolve() != path.resolve()
                or hashlib.sha256(path.read_bytes()).hexdigest() != observed['sha256']):
            raise ValueError('Post-review library role/hash mismatch')
    result['hotspotIdentity'] = {k: identity[k] for k in ['api', 'abi', 'runnerSha256', 'variants']}
    if ([v['name'] for v in plan['variants']] != ['A', 'C', 'D'] or plan['warmups'] != 5
            or plan['rounds'] != 20 or set(plan['cases']) != {'arithmetic', 'branch', 'shortCall'}):
        raise ValueError('Post-review timing protocol changed')
    rows = [json.loads(line) for line in (base / 'rounds.jsonl').read_text(encoding='utf-8').splitlines()]
    for case in plan['cases']:
        if case not in stored:
            continue
        variants = {}
        for name in ['A', 'C', 'D']:
            selected = [r for r in rows if r['case'] == case and r['variant'] == name]
            if len(selected) != 30 or len({r['checksum'] for r in selected}) != 1:
                raise ValueError('Invalid post-review round/checksum count')
            for batch in range(2):
                original = [json.loads(line) for line in (base / (name + '-' + case + '-measured-' + str(batch) + '.log')).read_text(encoding='utf-8').splitlines() if line.startswith('{')]
                batch_rows = [r for r in selected if r['batch'] == batch]
                for row in original:
                    row.update(variant=name, batch=batch)
                    if row['round'] >= 0:
                        row['round'] += batch * 10
                if original != batch_rows or [r['round'] for r in batch_rows] != list(range(-5, 0)) + list(range(batch * 10, batch * 10 + 10)):
                    raise ValueError('Post-review raw log/order mismatch')
            times = sorted(r['elapsedNs'] for r in selected if r['round'] >= 0)
            if len(times) != 20 or min(times) <= 0 or any(r['iterations'] != plan['selectedIterations'][case] for r in selected):
                raise ValueError('Post-review timing/iterations invalid')
            value = dict(count=20, medianNs=statistics.median(times), p95Ns=times[math.ceil(.95 * len(times)) - 1],
                         roundsUnderOneSecond=sum(t < 1_000_000_000 for t in times))
            if any(value[k] != stored[case][name][k] for k in value):
                raise ValueError('Post-review stored summary mismatch')
            variants[name] = value
        if len({r['checksum'] for r in rows if r['case'] == case}) != 1:
            raise ValueError('Post-review variant result mismatch')
        result['cases'][case] = dict(variants=variants,
            percentVsOriginal=(variants['D']['medianNs'] / variants['A']['medianNs'] - 1) * 100,
            percentVsBeforeWipe=(variants['D']['medianNs'] / variants['C']['medianNs'] - 1) * 100,
            durationProtocolMet=all(v['roundsUnderOneSecond'] == 0 for v in variants.values()))
complete = all(result[k] for k in ['device', 'legacyDevice', 'finalDevice', 'stability']) and len(result['cases']) == 3
result['checksComplete'] = bool(complete)
path = root / 'docs/enhancement/POST_REVIEW.md'
text = path.read_text(encoding='utf-8').split('<!-- DEVICE_RESULTS -->')[0].rstrip()
text = text.replace('设备结果及修复后定向性能数据将在采集完成后补充。全部输出保留本地，不覆盖外部已部署JAR或脚本。',
                    '全部输出保留本地，不覆盖外部已部署JAR或脚本。')
if complete:
    text = text.replace('源码及主机验证已完成；修复后候选的设备验证进行中。',
                        '源码、主机/API27功能验证及定向对照已完成；完整发布验收未通过。')
lines = [text, '', '<!-- DEVICE_RESULTS -->', '', '## 修复后设备验证', '']
diagnosis = result['observerShutdownDiagnosis']
if diagnosis['nohook'] and diagnosis['fixedBridgeHost']:
    if (diagnosis['nohook']['exitCode'] != 0 or
            'SEMANTIC_PASS checks=1351' not in diagnosis['nohook']['semantic'] or
            any(r['processExitCode'] != 0 or not r['bridgeTlsAbsent'] for r in diagnosis['fixedBridgeHost']['results'])):
        raise ValueError('Observer shutdown diagnosis evidence failed')
    lines += ['旧hook Release在1351项断言完成后退出139；崩溃位于线程退出析构路径，不能计为完整通过。相同runtime的hook OFF在ART通过1351项且exit0。测试桥随后移除两处TLS，使用受锁保护的线程帧表及一次发布的方法描述符；fresh主机回归、并发首次发布和无TLS符号检查通过。修复后ART结果仅取 `wipe-device-fixed`，不覆盖旧失败。证据见 `wipe-shutdown-diagnosis`、`wipe-device`、`wipe-host/tls-free-review`。', '']
if result['device']:
    lines.append('ARM64/API27 的按需 Debug/Release 各通过1351语义断言、105条独立 native VM 向量及root测试；两组实际擦除观察各通过6408次退出。legacy Debug/Release 各通过1338语义断言。完整库SHA、命令及输出见 `wipe-device-fixed/summary.json` 和 `commands.jsonl`。')
else:
    lines.append('尚未完成。')
if result['stability']:
    s = result['stability']
    if (s['apkSha256'] != result['artifact']['apkSha256'] or not s['samePidAllSamples']
            or s['crashMarkers'] or not s['mainWindowVisible'] or not s['resumed']):
        raise ValueError('Final candidate stability evidence invalid')
    lines += ['', f"最终设备已恢复本页的增强候选，核对安装SHA、loader build ID与当前主窗口。连续{s['elapsedSeconds']:.1f}秒观察中主PID保持{s['pid']}，未出现检查的崩溃标记，结束时主窗口仍首绘可见且resumed。该观察处于未激活状态，不代表完整业务或无泄漏证明。"]
lines += ['', '## 修复后定向性能', '',
          '同seed1、相同迭代数、两批各5预热/10测量。A为原始legacy OFF；C为冻结的修复前增强private ON；D为修复后增强private ON。这里的C定义仅用于本次修复对照，与S5四组合表不同。所有库均无计数探针。', '',
          '| 场景 | A中位/P95 ms | 修复前中位/P95 ms | 修复后中位/P95 ms | 修复后/原始 | 修复后/修复前 | 时长规程 |',
          '| --- | --- | --- | --- | --- | --- | --- |']
for name, case in result['cases'].items():
    cells = [name] + [f"{case['variants'][n]['medianNs']/1e6:.2f}/{case['variants'][n]['p95Ns']/1e6:.2f}" for n in ['A', 'C', 'D']]
    cells += [f"{case[k]:+.2f}%" for k in ['percentVsOriginal', 'percentVsBeforeWipe']]
    cells += ['满足' if case['durationProtocolMet'] else '有不足1秒样本，不计通过']
    lines.append('| ' + ' | '.join(cells) + ' |')
lines += ['', '这只是单seed、三项热点的修复复查；不能替代修复后完整三组九场景、API26和全部激活业务验收。新模式保持实验选项。原始证据见 [S5目录](runs/20260912-ondemand/S5/)，其中 `wipe-*` 与 `final-wipe-acceptance` 为本次修复产物；其余冻结S5数据仍属于修复前版本。', '']
path.write_text('\n'.join(lines), encoding='utf-8')
(w / 'wipe-summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(dict(checksComplete=result['checksComplete'], completedCases=list(result['cases']))))
