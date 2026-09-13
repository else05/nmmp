from pathlib import Path
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET

w = Path(__file__).resolve().parent
repo = w.parents[2]
dest = repo / 'docs/enhancement/runs/20260913-codec3-only'
dest.mkdir(exist_ok=False)
(dest / '.gitattributes').write_text('* -text\n', encoding='utf-8')
files = [p for p in w.iterdir() if p.is_file() and p.suffix in {'.json', '.log', '.py', '.sh', '.java'}]
for sub in ['semantic-device', 'startup', 'observation']:
    files.extend(p for p in (w / sub).rglob('*') if p.is_file())
for name in ['build.log', 'all-method-audit.json', 'method-audit.log', 'native-method-audit.log',
             'original-signature.log', 'protected-signature.log', 'selected-descriptors.txt',
             'inner-symbols.txt', 'outer-symbols.txt']:
    files.append(w / 'delivery' / name)
for source in files:
    target = dest / source.relative_to(w)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
counts = {}
for module in ['apkprotect', 'arsc']:
    totals = dict(tests=0, failures=0, errors=0, skipped=0)
    for source in (repo / 'nmm-protect' / module / 'build/test-results/test').glob('TEST-*.xml'):
        root = ET.parse(source).getroot()
        for key in totals:
            totals[key] += int(root.get(key, '0'))
        target = dest / 'junit' / module / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    assert totals['tests'] and totals['failures'] == totals['errors'] == 0
    counts[module] = totals
result = json.loads((w / 'result.json').read_text())
for name, key in [('vm-protect.jar', 'jarSha256'), ('protected-signed.apk', 'apkSha256')]:
    assert hashlib.sha256((w / 'delivery' / name).read_bytes()).hexdigest() == result[key]
summary = {'baseline': 'e7e1e15', 'java': counts, 'artifact': result,
           'performanceAccepted': False, 'scope': 'API27 ARM64, unactivated app; single startup and one minute idle observation'}
(dest / 'verification.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
hashes = {p.relative_to(dest).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in sorted(dest.rglob('*')) if p.is_file()}
(dest / 'sha256.json').write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'files': len(hashes), 'java': counts, 'evidence': str(dest)}, indent=2))
