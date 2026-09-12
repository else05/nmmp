"""Copy textual acceptance evidence only. APK/SO/JAR/signing material remain local build artifacts."""
from pathlib import Path
import hashlib
import json
import shutil

w = Path(__file__).resolve().parent
root = w.parents[2]
out = root / 'docs/enhancement/runs/20260912-ondemand/S5'
out.mkdir(parents=True, exist_ok=True)
copied = []


def copy(source, relative=None):
    if not source.is_file():
        return
    relative = relative or source.relative_to(w)
    destination = out / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    copied.append(dict(path=destination.relative_to(out).as_posix(), bytes=source.stat().st_size,
                       sha256=hashlib.sha256(source.read_bytes()).hexdigest()))


def text_directory(path):
    if path.is_dir():
        for source in path.iterdir():
            if source.suffix in ['.json', '.jsonl', '.log', '.md', '.txt', '.csv', '.patch', '.py', '.sh', '.java', '.cpp']:
                copy(source)


for pattern in ['s5-*.json', 's5-*.log', 's5-*.md', 's5-*.patch', 's1-stage-*.json', 's1-stage-*.log',
                's1-stage-*.md', 's1-stage-*.patch', 'build-s5-*.sh', 'prepare-s5-seed.py',
                'finish-s5-seed.py', 'audit-s5-native.sh', 'run-s5-probes.py', 'test-s5-negatives.py',
                'build-final-acceptance.sh', 'build-current-legacy-default.sh', 'render-s5-report.py',
                'collect-s5-evidence.py', 'write-s5-manifest.py', 'run-s5-device-tail.py', 'observe-final-device.py',
                'final-restoration-plan.json', 's5-device-tail.jsonl', 'run-s5-ordinary.py',
                'run-s5-startup-independent*.py', 'run-s5-large-diagnostic.py',
                'wipe-*.json', 'wipe-*.log', '*wipe*.py', 'build-wipe-*.sh',
                'check-wipe-randomness.sh', 'RandomnessCheck.java', 'run-final-device-checks.py', 'wipe-device-jobs.jsonl',
                'cleanup-task-adb.ps1']:
    for source in w.glob(pattern):
        if source.name != 's5-progress-summary.json':
            copy(source)
for name in ['final-acceptance', 'current-legacy-default', 's5-negative-package',
             's5-negative-certificate', 's5-negative-seed', 'seed-compatibility', 'final-device', 'final-stability',
             'final-wipe-acceptance', 'wipe-legacy-default', 'wipe-hotspot-D', 'wipe-device',
             'wipe-device-fixed', 'wipe-shutdown-diagnosis', 'wipe-hotspots', 'wipe-legacy-device', 'wipe-final-device', 'wipe-final-stability']:
    text_directory(w / name)
for source in (w / 's5').glob('*.json'):
    copy(source)
for index in range(1, 4):
    base = w / 's5' / ('seed' + str(index))
    text_directory(base)
    for name in ['A', 'B', 'C', 'D', 'startup', 'startup-retry1', 'startup-resumed',
                 'hotspots', 'hotspots-large-diagnostic']:
        text_directory(base / name)
    for folder in base.glob('startup-independent*'):
        if folder.is_dir():
            text_directory(folder)
    for name in 'ABCD':
        copy(base / ('hotspot-' + name) / 'build.log')
for name in ['probe-results', 'diagnosis', 'diagnosis-invariant', 'opt-word-C', 'stage-S1',
             'opt-invariant-C', 'opt-invariant-L', 'invariant-host', 'report-review', 'startup-noise-review']:
    text_directory(w / 's5-harness' / name)
for review_run in (w / 's5-harness/report-review').iterdir():
    if review_run.is_dir():
        text_directory(review_run)
for review_run in (w / 's5-harness/startup-noise-review').glob('run-*'):
    text_directory(review_run)
text_directory(w / 'wipe-host')
for folder in (w / 'wipe-host').rglob('*'):
    if folder.is_dir():
        text_directory(folder)
for name in ['input-methods.json', 'HarnessSelfCheck.java', 'prepare-opt-word.py', 'prepare-opt-invariant.py']:
    copy(w / 's5-harness' / name)
for module in ['apkprotect', 'arsc']:
    for source in (root / 'nmm-protect' / module / 'build/test-results/test').glob('TEST-*.xml'):
        copy(source, Path('wipe-java-tests' if (w / 'wipe-build-checks.json').exists() else 'java-tests') / module / source.name)
copy(root / 'nmm-protect/scripts/wsl/omvll-config.py', Path('omvll-config.py'))
plan = Path('E:/OtherProject/360_jiagu/360_enhance')
plan_hashes = {source.relative_to(plan).as_posix(): hashlib.sha256(source.read_bytes()).hexdigest()
               for folder in [plan, plan / 'private_linker'] for source in folder.glob('*.md')}
(out / 'plan-hashes.json').write_text(json.dumps(plan_hashes, indent=2), encoding='utf-8')
copied = [dict(path=p.relative_to(out).as_posix(), bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'evidence-files.json']
(out / 'evidence-files.json').write_text(json.dumps(copied, indent=2), encoding='utf-8')
print('Copied %d textual evidence files, %d bytes' % (len(copied), sum(row['bytes'] for row in copied)))
