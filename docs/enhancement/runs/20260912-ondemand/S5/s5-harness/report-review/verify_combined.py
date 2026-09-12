"""Small local-only deferred/combined report review, reusing prior fixtures."""
import ast
import copy
import datetime
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import sys
from contextlib import redirect_stdout
from unittest.mock import patch

sys.dont_write_bytecode = True
HERE=Path(__file__).resolve().parent
WORK=HERE.parents[1]
SCRIPTS=WORK.parents[2]/'nmm-protect/scripts'
OUT=HERE/('combined-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S-%fZ'))
OUT.mkdir()
SOURCE=HERE/'run-20260912T195618-052891Z/fixtures/hotspot-valid'
events=[]


def guard(event,args):
    if event in ('subprocess.Popen','os.system','socket.connect'):
        raise AssertionError('External process/network forbidden')
    if event=='open':
        p,mode,flags=args
        writing=(isinstance(mode,str) and any(c in mode for c in 'wax+')) or flags & (
            os.O_WRONLY|os.O_RDWR|os.O_CREAT|os.O_APPEND|os.O_TRUNC)
        if writing and not isinstance(p,int) and not Path(p).resolve().is_relative_to(OUT):
            raise AssertionError('Write outside current review')


sys.addaudithook(guard)
def read(p): return json.loads(p.read_text(encoding='utf-8'))
def save(p,v): p.write_text(json.dumps(v,indent=2)+'\n',encoding='utf-8')
def rows(p): return [json.loads(x) for x in p.read_text(encoding='utf-8').splitlines() if x]
def save_rows(p,v): p.write_text(''.join(json.dumps(x)+'\n' for x in v),encoding='utf-8')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
watched={str(p):sha(p) for p in SOURCE.iterdir() if p.is_file()}
for name in ('summarize-enhancement.py','measure-startup.py','measure-hotspots.py'):
    p=SCRIPTS/name; watched[str(p)]=sha(p); shutil.copyfile(p,OUT/name)
shutil.copyfile(Path(__file__),OUT/'verify_combined.py')
spec=importlib.util.spec_from_file_location('review_combined',SCRIPTS/'summarize-enhancement.py')
report=importlib.util.module_from_spec(spec); spec.loader.exec_module(report)
base=OUT/'combined-fixture'; base.mkdir()
for name,keep in [('hotspots',lambda c:c!='largeShort'),('hotspots-large-diagnostic',lambda c:c=='largeShort')]:
    p=base/name; shutil.copytree(SOURCE,p)
    plan=read(p/'plan.json'); plan['serial']='fixture-device'
    if name.endswith('diagnostic'): plan['largeShortIterations']=1
    plan['deferredCases']=[c for c in plan['cases'] if not keep(c)]
    save(p/'plan.json',plan)
    save_rows(p/'rounds.jsonl',[r for r in rows(p/'rounds.jsonl') if keep(r['case'])])
    save(p/'summary.json',{c:v for c,v in read(p/'summary.json').items() if keep(c)})
    save(p/'identity.json',dict(api='27',abi='arm64-v8a',runnerSha256='r'*64,
        variants=[dict(name=n,sha256=n*64) for n in 'ABCD']))
valid=report.combined_hotspots(base)
assert valid['complete'] and len(valid['cases'])==9
assert valid['cases']['largeShort']['durationLimitedDiagnostic'] is True
assert valid['cases']['largeShort']['durationProtocolMet'] is False
assert all(c['durationProtocolMet'] for n,c in valid['cases'].items() if n!='largeShort')
assert all(v['roundsUnderOneSecond']==0 for v in valid['cases']['largeShort']['variants'].values())
save(OUT/'valid-combined-result.json',valid)
events.append('PASS: diagnostic remains duration-ineligible even with all rounds >1s')

second=base/'hotspots-large-diagnostic'
identity=read(second/'identity.json'); plan=read(second/'plan.json')
for key in ['api','abi','runnerSha256','library','serial','replacement']:
    changed=copy.deepcopy(identity); changed_plan=copy.deepcopy(plan)
    if key=='library': changed['variants'][0]['sha256']='changed'
    elif key=='serial': changed_plan['serial']='another-device'
    elif key!='replacement': changed[key]='changed'
    save(second/'identity.json',changed); save(second/'plan.json',changed_plan)
    if key=='replacement':
        original=rows(base/'hotspots/rounds.jsonl')
        save_rows(base/'hotspots/rounds.jsonl',rows(SOURCE/'rounds.jsonl'))
        save(base/'hotspots/summary.json',read(SOURCE/'summary.json'))
    try:
        report.combined_hotspots(base)
    except ValueError as e: events.append('PASS rejection '+key+': '+str(e))
    else: raise AssertionError('Accepted mismatched '+key)
    if key=='replacement':
        save_rows(base/'hotspots/rounds.jsonl',original)
        save(base/'hotspots/summary.json',{c:v for c,v in read(SOURCE/'summary.json').items() if c!='largeShort'})
    save(second/'identity.json',identity); save(second/'plan.json',plan)

# Execute the exact collector scheduling tail only. Device setup/run()/threads are never executed.
tree=ast.parse((SCRIPTS/'measure-hotspots.py').read_text(encoding='utf-8'))
start=next(i for i,n in enumerate(tree.body) if isinstance(n,ast.Assign)
           and any(isinstance(t,ast.Name) and t.id=='all_samples' for t in n.targets))
tail=compile(ast.Module(body=tree.body[start:],type_ignores=[]),'collector-scheduling-tail','exec')
save(OUT/'collector-tail-ast.json',ast.dump(ast.Module(body=tree.body[start:],type_ignores=[]),include_attributes=False))
def schedule(name,deferred=(),fixed=None,slow_ns=500_000_000):
    out=OUT/name; out.mkdir()
    p=read(SOURCE/'plan.json'); p['deferredCases']=list(deferred)
    if fixed is not None: p['largeShortIterations']=fixed
    calls=[]
    def fake_run(v,c,iterations,warmups,rounds,label,timeout_seconds=600):
        calls.append([v['name'],c,iterations,warmups,rounds,label,timeout_seconds])
        ns={'A':1_000_000,'B':2_000_000,'C':3_000_000,'D':slow_ns}[v['name']]
        return [dict(variant=v['name'],case=c,round=r,iterations=iterations,elapsedNs=ns,checksum=7)
                for r in range(-warmups,rounds)]
    g=dict(plan=p,out=out,deferred_cases=set(deferred),run=fake_run,math=math,statistics=statistics,
           json=json,datetime=datetime,command=lambda a:'synthetic-environment')
    log=io.StringIO()
    with redirect_stdout(log): exec(tail,g)
    save(out/'calls.json',calls)
    (out/'execution.log').write_text(log.getvalue(),encoding='utf-8')
    return p,calls
p,normal=schedule('schedule-normal')
assert all(n==1500 for n in p['selectedIterations'].values())
assert all(t==2250 for t in p['selectedTimeoutSeconds'].values())
for i,c in enumerate(p['cases']):
    rotation=list('ABCD')[i%4:]+list('ABCD')[:i%4]
    selected=[x for x in normal if x[1]==c]
    assert [x[0] for x in selected]==rotation+rotation+list(reversed(rotation))
_,deferred=schedule('schedule-deferred',deferred=('branch','largeShort'))
assert deferred==[x for x in normal if x[1] not in ('branch','largeShort')]
p,fixed=schedule('schedule-fixed-large',fixed=10000)
assert [x for x in fixed if x[1]!='largeShort']==[x for x in normal if x[1]!='largeShort']
assert p['selectedIterations']['largeShort']==10000 and p['selectedTimeoutSeconds']['largeShort']==15000
p,_=schedule('schedule-timeout-floor',slow_ns=4_000_000)
assert set(p['selectedTimeoutSeconds'].values())=={600}
events.append('PASS: deferred middle/last cases preserve ordinary iterations and rotation; fixed large affects only large')
events.append('PASS: calibration timeout ceiling 2250s / fixed-large 15000s / minimum 600s')

# Local end-to-end aggregation: retain the missing-independent gate, then challenge independence.
fake_work=OUT/'work'; fake_work.mkdir()
for s in range(1,4):
    name='startup' if s==1 else 'startup-resumed'
    source=WORK/f's5/seed{s}'/name; dest=fake_work/f's5/seed{s}'/name; dest.mkdir(parents=True)
    for f in ['plan.json','identity.json','attempts.jsonl','summary.json']:
        watched[str(source/f)]=sha(source/f); shutil.copyfile(source/f,dest/f)
def aggregate(label):
    target=OUT/(label+'.json')
    with patch.object(sys,'argv',['summarize',str(fake_work),str(target)]), \
         patch.object(report,'combined_hotspots',return_value=copy.deepcopy(valid)),redirect_stdout(io.StringIO()):
        report.main()
    return read(target)
r=aggregate('missing-independent')
assert not r['measurementComplete'] and not r['startupNoiseReviewComplete'] and not r['performanceGatePassed']
events.append('PASS: missing independent batch prevents completion/pass')
for s in range(1,4):
    base_s=fake_work/f's5/seed{s}'
    shutil.copytree(base_s/('startup' if s==1 else 'startup-resumed'),base_s/'startup-independent')
r=aggregate('copied-old-batch-as-independent')
assert r['startupNoiseReviewComplete'] is True
assert r['performanceGatePassed'] is False  # Diagnostic duration remains disqualifying.
events.append('FINDING: copying the old batches into startup-independent incorrectly marks noise review complete')
p=fake_work/'s5/seed1/startup-independent'
plan=read(p/'plan.json'); plan['variants'][0]['sha256']='different-build'
save(p/'plan.json',plan)
values=rows(p/'attempts.jsonl')
for row in values:
    if row['variant']=='A': row['apkSha256']='different-build'
save_rows(p/'attempts.jsonl',values)
identity=read(p/'identity.json'); identity['api']='26'; save(p/'identity.json',identity)
r=aggregate('different-build-independent')
assert r['startupNoiseReviewComplete'] is True
events.append('FINDING: independent batch with changed own APK hash/device identity is not compared to original')
assert all(sha(Path(p))==v for p,v in watched.items())
save(OUT/'results.json',dict(events=events,originalsUnchanged=True,adbInvocations=0,
    limits='Synthetic transport-free scheduling tail, reused synthetic hotspot fixtures, real startup copies. '
           'Aggregate tests stub only combined_hotspots with its already validated diagnostic result. '
           'These are not device or performance tests; fixed diagnostic iterations imply no user duration approval.'))
save(OUT/'input-sha256.json',watched)
save(OUT/'artifact-sha256.json',{str(p.relative_to(OUT)):sha(p) for p in OUT.rglob('*') if p.is_file()})
print('\n'.join(events)); print('Evidence:',OUT)
