from pathlib import Path
import hashlib
import json
import xml.etree.ElementTree as ET
import zipfile

w = Path(__file__).resolve().parent
root = w.parents[2]
delivery = json.loads((w / 'wipe-delivery.json').read_text(encoding='utf-8'))
template = root / 'nmm-protect/apkprotect/src/main/resources/vmsrc.zip'
with zipfile.ZipFile(delivery['jar']) as jar:
    if jar.read('vmsrc.zip') != template.read_bytes():
        raise ValueError('JAR template mismatch')
with zipfile.ZipFile(template) as archive:
    for name in ['vm/InterpC-portable.cpp', 'vm/include/VmDecodedState.h']:
        if archive.read(name) != (root / 'nmmvm/nmmvm/src/main/cpp' / name).read_bytes():
            raise ValueError('Final decoded cleanup source mismatch')
    if any('DecodedWipeBridge' in n or 'DecodedWipeMain' in n for n in archive.namelist()):
        raise ValueError('Test bridge packaged in template')
tests = {}
for module in ['apkprotect', 'arsc']:
    totals = dict(tests=0, failures=0, errors=0, skipped=0)
    for path in (root / 'nmm-protect' / module / 'build/test-results/test').glob('TEST-*.xml'):
        suite = ET.parse(path).getroot()
        for key in totals:
            totals[key] += int(suite.attrib[key])
    if totals['tests'] == 0 or any(totals[k] for k in ['failures', 'errors', 'skipped']):
        raise ValueError('Java test verification failed')
    tests[module] = totals
(w / 'wipe-build-checks.json').write_text(json.dumps(dict(java=tests,
    jarSha256=delivery['jarSha256'], templateSha256=hashlib.sha256(template.read_bytes()).hexdigest(),
    packagedTemplateExact=True, decodedRuntimeSourceExact=True, testBridgeAbsent=True), indent=2), encoding='utf-8')
adb = r'D:\Android\SDK\platform-tools\adb.exe'
for directory, name, loader, output in [('final-wipe-acceptance', 'D', True, 'wipe-final-restoration-plan.json'),
                                       ('wipe-legacy-default', 'L', False, 'wipe-legacy-device-plan.json')]:
    base = w / directory
    variant = dict(name=name, apk=str(base / 'protected-signed.apk'), loader=loader)
    if loader:
        variant['loaderId'] = (base / 'input/build/.cxx/cmake/Release/arm64-v8a/private/build-id.txt').read_text().strip()[:8]
    plan = dict(adb=adb, serial='192.168.6.118:5555', package='org.savior.sync', activity='org.savior.sync/.MainActivity',
                warmups=0, rounds=1, purpose='Post-review function/identity check, not a formal performance batch', variants=[variant])
    (w / output).write_text(json.dumps(plan, indent=2), encoding='utf-8')
script = (w / 'observe-final-device.py').read_text(encoding='utf-8')
script = script.replace("'final-stability'", "'wipe-final-stability'").replace("'final-device/attempts.jsonl'", "'wipe-final-device/attempts.jsonl'")
(w / 'observe-wipe-final-device.py').write_bytes(script.encode('utf-8'))
plan = dict(adb=adb, serial='192.168.6.118:5555', runner=str(w / 's5-harness/runner-dex/classes.dex'),
            warmups=5, rounds=20, cases=dict(arithmetic=100, branch=100, shortCall=10000),
            purpose='Seed1 targeted post-review comparison; A=frozen original legacy OFF, C=frozen pre-wipe demand/private ON, D=post-wipe demand/private ON. Not a three-seed final acceptance.',
            variants=[dict(name=n, library=str(path / 'build/obj/strip/arm64-v8a/libc++_en.so')) for n, path in
                      [('A', w / 's5/seed1/hotspot-A'), ('C', w / 's5/seed1/hotspot-D'), ('D', w / 'wipe-hotspot-D')]])
(w / 'wipe-hotspot-plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
print(json.dumps(tests))
