"""Read-only startup noise diagnostics; no ADB, no filtering of accepted samples.

Run with python -B. Outputs are isolated per invocation beside this script.
Paired and leave-one-pair-out statistics are diagnostics, never acceptance substitutes.
"""
import csv
import datetime as dt
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import statistics as st
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
WORK = HERE.parents[1]
ROOT = WORK.parents[2]
OUT = HERE / dt.datetime.now(dt.timezone.utc).strftime('run-%Y%m%dT%H%M%S-%fZ')
OUT.mkdir()


def guard(event, args):
    if event in ('subprocess.Popen', 'os.system', 'socket.connect'):
        raise AssertionError('No external execution or network access permitted')
    if event == 'open':
        path, mode, flags = args
        writing = (isinstance(mode, str) and any(c in mode for c in 'wax+')) or flags & (
            os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_APPEND | os.O_TRUNC)
        if writing and not isinstance(path, int) and not Path(path).resolve().is_relative_to(OUT):
            raise AssertionError('Write outside isolated noise review: ' + str(path))


sys.addaudithook(guard)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def save(name, data):
    (OUT / name).write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def metrics(values):
    x = sorted(values)
    return dict(count=len(x), median=st.median(x), p95=x[math.ceil(.95 * len(x)) - 1],
                minimum=x[0], maximum=x[-1], mean=st.mean(x),
                sdPopulation=st.pstdev(x))


def marginal(pairs):
    return (st.median(p['dMs'] for p in pairs) / st.median(p['aMs'] for p in pairs) - 1) * 100


def corr(x, y):
    mx, my = st.mean(x), st.mean(y)
    return sum((a-mx)*(b-my) for a,b in zip(x,y)) / math.sqrt(
        sum((a-mx)**2 for a in x) * sum((b-my)**2 for b in y))


inputs = {}
scripts = ROOT / 'nmm-protect/scripts'
for path in [scripts/'summarize-enhancement.py', scripts/'measure-startup.py',
             Path('E:/OtherProject/360_jiagu/360_enhance/ACCEPTANCE.md'),
             ROOT/'docs/enhancement/S5_PLAN.md', Path(__file__)]:
    inputs[str(path)] = sha(path)
for s in range(1,4):
    for directory in (WORK/f's5/seed{s}').glob('startup*'):
        if directory.is_dir():
            for path in directory.iterdir():
                if path.is_file() and (path.suffix in ('.json', '.jsonl') or '-memory-' in path.name):
                    inputs[str(path)] = sha(path)
spec = importlib.util.spec_from_file_location('noise_report_checks', scripts/'summarize-enhancement.py')
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)
results = {}
all_pairs = []
for s in range(1,4):
    base = WORK/f's5/seed{s}'/('startup' if s == 1 else 'startup-resumed')
    checked = checks.startup(base)
    assert checked['complete'] and checked['validAttempts'] == 92
    ledger = [json.loads(line) for line in (base/'attempts.jsonl').read_text(encoding='utf-8').splitlines() if line]
    measured = [r for r in ledger if r['valid'] is True and r['warmup'] is False]
    assert len(measured) == 80
    by_round = {r: {x['variant']: x for x in measured if x['round'] == r} for r in range(3,23)}
    variants = {}
    for v in 'ABCD':
        values = [by_round[r][v]['thisTimeMs'] for r in range(3,23)]
        ordered = sorted(values)
        m = metrics(values)
        assert m['median'] == checked['variants'][v]['median']
        assert m['p95'] == checked['variants'][v]['p95']
        m.update(coefficientOfVariation=m['sdPopulation']/m['mean'], sortedMs=ordered,
                 centralOrderStatisticsMs=ordered[9:11],
                 below2500Ms=sum(x < 2500 for x in values),
                 atLeast2500Ms=sum(x >= 2500 for x in values),
                 histogram100Ms={str(lo)+'-'+str(lo+99):sum(lo <= x < lo+100 for x in values)
                                 for lo in range(2200,3200,100)})
        variants[v] = m
    pairs = []
    for r, group in by_round.items():
        a, d = group['A'], group['D']
        stamp_a = dt.datetime.fromisoformat(a['hostUtc'])
        stamp_d = dt.datetime.fromisoformat(d['hostUtc'])
        p = dict(seed=s, round=r, aMs=a['thisTimeMs'], dMs=d['thisTimeMs'],
                 ratioDOverA=d['thisTimeMs']/a['thisTimeMs'],
                 pairedPercent=(d['thisTimeMs']/a['thisTimeMs']-1)*100,
                 deltaMs=d['thisTimeMs']-a['thisTimeMs'], order='ABCD' if r%2==0 else 'DCBA',
                 aHostUtc=a['hostUtc'], dHostUtc=d['hostUtc'],
                 separationSeconds=abs((stamp_d-stamp_a).total_seconds()),
                 descriptiveClass=('low' if a['thisTimeMs']<2500 else 'high')+'-'+
                                  ('low' if d['thisTimeMs']<2500 else 'high'))
        assert (stamp_a < stamp_d) == (r % 2 == 0)
        pairs.append(p)
    formal = marginal(pairs)
    assert abs(formal-checked['variants']['D']['percentVsA']) < 1e-10
    loo = [dict(omittedRound=p['round'], diagnosticMarginalPercent=marginal(pairs[:i]+pairs[i+1:]))
           for i,p in enumerate(pairs)]
    groups = {}
    for name, subset in [('ABCD', [p for p in pairs if p['order']=='ABCD']),
                         ('DCBA', [p for p in pairs if p['order']=='DCBA']),
                         ('rounds3to12', pairs[:10]), ('rounds13to22', pairs[10:])]:
        groups[name] = dict(pairedPercent=metrics([p['pairedPercent'] for p in subset]),
                            descriptiveMarginalPercent=marginal(subset))
    results[f'seed{s}'] = dict(directory=str(base), variants=variants, marginalPercent=formal,
        pairs=pairs, pairedPercent=metrics([p['pairedPercent'] for p in pairs]),
        pairedDifferenceMs=metrics([p['deltaMs'] for p in pairs]),
        pairsAbove10Percent=sum(p['pairedPercent']>10 for p in pairs),
        pairedTimePearsonCorrelation=corr([p['aMs'] for p in pairs],[p['dMs'] for p in pairs]),
        pairSeparationSeconds=metrics([p['separationSeconds'] for p in pairs]),
        leaveOnePairOut=loo,
        leaveOnePairOutRangePercent=[min(x['diagnosticMarginalPercent'] for x in loo),
                                    max(x['diagnosticMarginalPercent'] for x in loo)],
        leaveOnePairOutAbove10=sum(x['diagnosticMarginalPercent']>10 for x in loo),
        descriptiveGroups=groups)
    all_pairs.extend(pairs)

assert all(sha(Path(path)) == value for path,value in inputs.items())
save('results.json', dict(seeds=results, originalsUnchanged=True, originalInputCount=len(inputs),
    adbInvocations=0, diagnosticThresholdMs=2500,
    method='All valid non-warmup rows retained. Same round A/D paired. p95 nearest rank. '
           'Leave-one-pair-out is a 20-run sensitivity diagnostic, not exclusion or resampling evidence. '
           '2500 ms grouping is descriptive and post hoc. No iid bootstrap or significance claim.'))
save('input-sha256.json', inputs)
with (OUT/'paired-rounds.csv').open('w', encoding='utf-8', newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(all_pairs[0])); writer.writeheader(); writer.writerows(all_pairs)
shutil.copyfile(Path(__file__), OUT/'analyze_startup_noise.py')
lines = ['# Startup noise review', '',
         '仅本地分析；不调用 ADB、不更改采样数据。以下配对、分组和逐轮删除计算仅作诊断，不替代 ACCEPTANCE 6.4 的边际中位数比。', '',
         '| seed | A/D 中位数 ms | 规定 D/A 回退 | 配对回退中位数 / P95 | 配对范围 | 逐轮删除配对后规定比值范围 | >10% 次数 |',
         '| --- | --- | --- | --- | --- | --- | --- |']
for name,r in results.items():
    p=r['pairedPercent']; lo,hi=r['leaveOnePairOutRangePercent']
    lines.append(f"| {name} | {r['variants']['A']['median']:g}/{r['variants']['D']['median']:g} | {r['marginalPercent']:+.3f}% | {p['median']:+.3f}% / {p['p95']:+.3f}% | {p['minimum']:+.3f}%～{p['maximum']:+.3f}% | {lo:+.3f}%～{hi:+.3f}% | {r['leaveOnePairOutAbove10']}/20 |")
lines += ['', '## 原始时间结构', '',
          '统一用 2500ms 作事后描述边界。不是拟合出的因果状态、不是剔除标准，也不是新门槛。逐轮数据及主机采集起点间隔见 paired-rounds.csv。']
for name,r in results.items():
    a,d=r['variants']['A'],r['variants']['D']
    seq=', '.join(f"{p['round']}:{p['descriptiveClass']}" for p in r['pairs'])
    gap=r['pairSeparationSeconds']; cv_a=a['coefficientOfVariation']*100; cv_d=d['coefficientOfVariation']*100
    lines += ['', f"- {name}：A 快/慢={a['below2500Ms']}/{a['atLeast2500Ms']}，D={d['below2500Ms']}/{d['atLeast2500Ms']}；A/D CV={cv_a:.2f}%/{cv_d:.2f}%；A/D 排序中间两值={a['centralOrderStatisticsMs']}/{d['centralOrderStatisticsMs']}。",
              f"  同轮 A/D 耗时相关系数 {r['pairedTimePearsonCorrelation']:.3f}；采集起点间隔中位数 {gap['median']:.1f}s、最大 {gap['maximum']:.1f}s，配对并非同时测量。",
              '  轮次状态：'+seq]
lines += ['', '## 判定与独立批次', '',
    '三组边际点估计均不超过10%，但不可据此声称噪声复核已通过。快慢状态按连续时间段集中出现，而不是已证实的独立同分布波动；同轮配对也存在跨状态轮次。seed3 的边际中位数跨簇，删除一整轮配对便能翻转10%判定。', '',
    'ACCEPTANCE 6.4 明确要求“超限或结果受噪声影响时重新做独立批次”。按本次证据，若要完成启动性能验收，应为三组受影响seed各补同seed独立批次；seed3尤其不能仅靠现有+7.84%宣告达标。允许先停止并报告“当前点估计未超限，噪声复核未完成”，而不是必须立即操作设备。', '',
    '独立批次使用冻结的同seed APK/hash、设备、签名及ABCD规程，每变体重新3次预热+20次有效进程冷启动，另建输出目录；当前续采只补齐原批次，不算独立批次。预先固定批次数和判读方法，保留全部旧/新批次并逐批报告，不因更快而选择批次，不把逐轮配对或合并后的有利中位数替代原门槛。若新批次仍有相同状态漂移或门槛翻转，保持验收未完成并定位状态变化原因，不循环采样直到恰好通过。', '',
    '先完成正在执行的正式hotspot流程，再由主线程另行协调独立启动批次；本分析没有操作或改动运行中的任务。', '',
    '## 报告可用限定', '',
    '“当前三组启动批次的规定D/A中位数回退分别为+0.78%、+1.80%、+7.84%，点估计均未超过10%。数据呈明显时间相关的快慢分段，seed3判定对簇占比敏感；同轮配对诊断不能替代规定指标。根据ACCEPTANCE 6.4，启动噪声复核及同seed独立批次尚未完成，不据此宣称稳定达标。”', '',
    '这些数据不能区分ART/系统缓存、后台负载、安装时序或保护器本身的因果贡献；温度读数无效，配对不是因果控制。没有移除异常速度点、没有新造置信区间门槛。']
(OUT/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
save('artifact-sha256.json',{str(p.relative_to(OUT)):sha(p) for p in OUT.iterdir() if p.is_file()})
print('\n'.join(lines[:10]))
print('Evidence:',OUT)
