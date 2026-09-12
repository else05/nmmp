from pathlib import Path
import hashlib
import json

w = Path(__file__).resolve().parent
root = w.parents[2]
out = root / 'docs/enhancement/runs/20260912-ondemand/S5'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


report = read(w / 's5-summary.json')
manifest = dict(
    baselineCommit='14bc959', runtimeBaseCommit='d54abcd',
    implementationStages={'S1': '18dc5b1', 'S2': '2a86c65', 'S3': 'a32211d', 'S4': 'fbcf0ef', 'P5': 'd54abcd'},
    s5SourceChange='Explicit test-only seeded generator streams, real ART performance harness, evidence collectors/reports, and post-review explicit decoded scratch cleanup',
    runtimeOptimizationsApplied=False,
    modes={'defaultDecode': 'legacy', 'defaultPrivateLinker': False,
           'candidateDecode': 'on-demand-v1', 'candidatePrivateLinker': True, 'candidateStage0Vm': True},
    inputs=read(w / 's5-input-hashes.json'), java=read(w / 's5-java-build-results.json'),
    frozenUnseededCandidate=read(w / 'final-acceptance/structure-audit.json'),
    deliveredCandidate=read(w / 'final-wipe-acceptance/structure-audit.json'),
    postReview=read(w / 'wipe-summary.json'),
    observerShutdownDiagnosis=dict(
        nohook=read(w / 'wipe-shutdown-diagnosis/nohook-result.json'),
        fixedBridgeHost=read(w / 'wipe-host/tls-free-review/publication/results.json'),
        failedRun='wipe-device/commands.jsonl', fixedRun='wipe-device-fixed/summary.json'),
    frozenPerformanceScope='The three-seed S5 libraries precede the explicit decoded scratch cleanup; they are not final candidate acceptance',
    frozenDefaultLegacyDevice=read(w / 'current-legacy-default/device-check.json'),
    currentDefaultLegacyDevice=read(w / 'wipe-legacy-device/summary.json') if (w / 'wipe-legacy-device/summary.json').exists() else None,
    taskAdbServer=read(w / 'wipe-adb-isolated-server.json'),
    negativePackageCertificateSeed=read(w / 's5-negative-results.json'),
    sizes=read(w / 's5/sizes.json'), measurements=report,
    diagnostics=read(w / 's5-diagnostic-results.json'),
    unavailableCoverage=['ARM64 API26 device', 'Activated/server-backed complete business flow'],
    attributionLimit='S1, current legacy and current demand measured separately; S2/S3/S4 not individually timed',
    releaseAcceptancePassed=False)
if manifest['postReview']['artifact'] != manifest['deliveredCandidate']:
    raise ValueError('Post-review summary refers to a different delivered candidate')
for build in manifest['java'].values():
    if 'jar' in build and hashlib.sha256(Path(build['jar']).read_bytes()).hexdigest() != build['sha256']:
        raise ValueError('Frozen generator JAR changed')
delivery = read(w / 'wipe-delivery.json')
if (hashlib.sha256(Path(delivery['jar']).read_bytes()).hexdigest() != delivery['jarSha256']
        or delivery['jarSha256'] != manifest['deliveredCandidate']['jarSha256']):
    raise ValueError('Delivered generator JAR changed')
manifest['deliveredGenerator'] = dict(jar=delivery['jar'], sha256=delivery['jarSha256'])
audits = {}
descriptors = None
for index in range(1, 4):
    for variant in 'ABCD':
        base = w / 's5' / ('seed' + str(index)) / variant
        audit = read(base / 'all-method-audit.json')
        actual = (base / 'selected-descriptors.txt').read_bytes()
        if descriptors is None:
            descriptors = actual
        if actual != descriptors or audit['count'] != 526 or not audit['packagedNativeMethodSetExactMatch']:
            raise ValueError('Method set or full-method audit mismatch')
        audits['seed%d-%s' % (index, variant)] = dict(
            methods=audit['count'], packagedNativeMethodSetExactMatch=True,
            reportSha256=hashlib.sha256((base / 'all-method-audit.json').read_bytes()).hexdigest())
manifest['methodAudits'] = audits
manifest['selectedDescriptorSetSha256'] = hashlib.sha256(descriptors).hexdigest()
candidate = w / 'final-acceptance/protected-signed.apk'
if hashlib.sha256(candidate.read_bytes()).hexdigest() != manifest['frozenUnseededCandidate']['apkSha256']:
    raise ValueError('Unseeded candidate changed')
candidate = w / 'final-wipe-acceptance/protected-signed.apk'
if hashlib.sha256(candidate.read_bytes()).hexdigest() != manifest['deliveredCandidate']['apkSha256']:
    raise ValueError('Delivered candidate changed')
if (w / 'wipe-final-device/summary.json').exists():
    rows = [json.loads(line) for line in (w / 'wipe-final-device/attempts.jsonl').read_text(encoding='utf-8').splitlines()]
    if len(rows) != 1 or not rows[0]['valid'] or rows[0]['apkSha256'] != manifest['deliveredCandidate']['apkSha256']:
        raise ValueError('Final restoration mismatch')
    manifest['finalDeviceRestoration'] = rows[0]
if (w / 'wipe-final-stability/summary.json').exists():
    manifest['finalStability'] = read(w / 'wipe-final-stability/summary.json')
manifest['sourceFiles'] = {}
manifest['sourceFilesScope'] = 'Current working tree at manifest generation; these hashes do not describe the frozen pre-wipe S5 build sources.'
for folder in ['nmm-protect/apkprotect/src/main/java', 'nmm-protect/apkprotect/src/test/performance',
               'nmmvm/nmmvm/src/main/cpp/vm', 'nmmvm/nmmvm/src/test/semantic', 'nmm-protect/mksrc/loader']:
    for path in (root / folder).rglob('*'):
        if path.is_file() and path.suffix in ['.java', '.c', '.cpp', '.h', '.py', '.json']:
            manifest['sourceFiles'][path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
for path in [w / name for name in ['render-s5-report.py', 'render-wipe-results.py', 'write-s5-manifest.py']] + [
        root / 'nmm-protect/scripts/summarize-enhancement.py',
        root / 'nmm-protect/scripts/measure-startup.py', root / 'nmm-protect/scripts/measure-hotspots.py',
        root / 'nmmvm/nmmvm/src/test/semantic/CMakeLists.txt']:
    manifest['sourceFiles'][path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('S5 manifest written; full measurement complete:', report['measurementComplete'])
