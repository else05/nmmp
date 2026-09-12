"""Read current evidence, render into memory, and reject corrupted report fixtures; no ADB."""
from pathlib import Path
from unittest.mock import patch
import contextlib
import copy
import difflib
import hashlib
import io
import json
import runpy
import sys

sys.dont_write_bytecode = True
out = Path(__file__).resolve().parent
w = out.parents[2]
root = w.parents[2]
original_read = Path.read_text
original_bytes = Path.read_bytes
original_exists = Path.exists
cache = {}
events = []


def execute(name, overrides=None, blobs=None):
    writes = {}
    overrides, blobs = overrides or {}, blobs or {}
    def read(p, *a, **kw):
        if p in overrides: return overrides[p]
        if p not in cache: cache[p] = original_read(p, *a, **kw)
        return cache[p]
    def write(p, text, *a, **kw): writes[p] = text; return len(text)
    def subprocess(cmd, **kw):
        if name != 'render-s5-report.py' or 'summarize-enhancement.py' not in str(cmd):
            raise AssertionError('External execution forbidden')
    with patch.object(Path, 'read_text', read), patch.object(Path, 'write_text', write), \
            patch.object(Path, 'read_bytes', lambda p: blobs[p] if p in blobs else original_bytes(p)), \
            patch.object(Path, 'exists', lambda p: p in overrides or p in blobs or original_exists(p)), \
            patch('subprocess.run', subprocess), contextlib.redirect_stdout(io.StringIO()):
        env = runpy.run_path(str(w / name))
    return writes, env


def rejected(label, name, overrides, blobs=None):
    try: execute(name, overrides, blobs)
    except ValueError as ex: events.append(dict(case=label, rejected=str(ex)))
    else: raise AssertionError('Accepted invalid fixture: ' + label)


rendered, _ = execute('render-s5-report.py')
text = rendered[root / 'docs/enhancement/S5_RESULTS.md']
assert '9033d186fe875858552177856c126f51b34264d0d52ca579138fd584be497747' in text
assert '28c13bbb3952d69640ad5bae626f0f5b7d04346e701e43dded0efd5e3734b469' in text
events.append(dict(case='S5 separate baseline/current generator hashes', passed=True))
rendered, env = execute('render-wipe-results.py')
post = rendered[root / 'docs/enhancement/POST_REVIEW.md']
summary = json.loads(rendered[w / 'wipe-summary.json'])
assert not summary['releaseAcceptancePassed']
assert '退出139' in post and 'hook OFF在ART通过1351项且exit0' in post
events.append(dict(case='Current partial wipe evidence renders without claiming pending device success',
                   checksComplete=summary['checksComplete'], cases=list(summary['cases'])))
identity = json.loads(cache[w / 'wipe-hotspots/identity.json'])
changed = copy.deepcopy(identity); changed['variants'][2]['sha256'] = '0' * 64
rejected('wrong hotspot library hash', 'render-wipe-results.py', {w / 'wipe-hotspots/identity.json':json.dumps(changed)})
changed = copy.deepcopy(identity); changed['api'] = '26'
rejected('wrong hotspot device identity', 'render-wipe-results.py', {w / 'wipe-hotspots/identity.json':json.dumps(changed)})

# A completed synthetic device suite must include exact outputs, exit0 and matching fresh library hashes.
rows, commands, blobs = [], [], {}
for mode in ['Release', 'Debug', 'legacy-Release', 'legacy-Debug']:
    legacy = mode.startswith('legacy-')
    outputs = {'semantic':'SEMANTIC_PASS checks=' + ('1338' if legacy else '1351')}
    names = ['libnmmvm.so', 'libnmmp_semantic.so']
    if not legacy:
        names += ['nmmp_native_vm_test', 'nmmp_native_root_test']
        outputs.update(nativeVm='PASS: 105 independent Java/native program vectors',
                       root='PASS: native VM recovers and publishes root once across 1600 concurrent activations',
                       wipe='DECODE_WIPE_PASS runs=801')
    hashes = {}
    build = w / ('wipe-fixed-' + ('legacy-' if legacy else '') + 'semantic-' + mode.removeprefix('legacy-'))
    for name in names:
        data = (mode + name).encode(); hashes[name] = hashlib.sha256(data).hexdigest()
        blobs[build / ('vm/libnmmvm.so' if name == 'libnmmvm.so' else name)] = data
    rows.append(dict(mode=mode, librarySha256=hashes, **({'output':outputs['semantic']} if legacy else {'outputs':outputs})))
    commands.extend(dict(code=0, output=value) for value in outputs.values())
overrides = {w / 'wipe-device-fixed/summary.json':json.dumps(rows),
             w / 'wipe-device-fixed/identity.json':json.dumps(dict(api='27',abi='arm64-v8a')),
             w / 'wipe-device-fixed/commands.jsonl':'\n'.join(map(json.dumps,commands))}
execute('render-wipe-results.py',overrides,blobs)
events.append(dict(case='Complete valid device fixture accepted',passed=True))
bad = copy.deepcopy(commands); bad[0]['code'] = 139
rejected('PASS marker followed by exit139','render-wipe-results.py',
         overrides | {w / 'wipe-device-fixed/commands.jsonl':'\n'.join(map(json.dumps,bad))},blobs)
bad = copy.deepcopy(rows); bad[0]['librarySha256']['libnmmvm.so'] = '0'*64
rejected('wrong fresh device library hash','render-wipe-results.py',
         overrides | {w / 'wipe-device-fixed/summary.json':json.dumps(bad)},blobs)

manifest, _ = execute('write-s5-manifest.py',{w / 'wipe-summary.json':json.dumps(summary)})
saved = json.loads(next(iter(manifest.values())))
assert saved['measurements']['measurementComplete'] and not saved['measurements']['performanceGatePassed']
assert saved['deliveredGenerator']['sha256'] == '28d6f83f8d099fecbcbdfbbd7acc3b41cb7e9b0347260920430d742fa9530ad7'
events.append(dict(case='Manifest separates current source hashes and frozen S5 provenance',passed=True))
bad = copy.deepcopy(summary); bad['artifact']['apkSha256'] = '0'*64
rejected('mixed candidate summary','write-s5-manifest.py',{w / 'wipe-summary.json':json.dumps(bad)})
for name in ['render-s5-report.py','render-wipe-results.py','write-s5-manifest.py']:
    before = (out / (name+'.before')).read_text(encoding='utf-8')
    after = (w/name).read_text(encoding='utf-8')
    (out / (name+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
        fromfile='a/'+name,tofile='b/'+name)),encoding='utf-8')
(out/'results.json').write_text(json.dumps(dict(events=events,
    reportFilesWritten=False, rawEvidenceChanged=False, adbCalled=False,
    sourceSha256={name:hashlib.sha256((w/name).read_bytes()).hexdigest() for name in
                  ['render-s5-report.py','render-wipe-results.py','write-s5-manifest.py']}),indent=2),encoding='utf-8')
print(json.dumps(events,indent=2))
