from pathlib import Path
import hashlib
import json
import re
import subprocess
import zipfile

w = Path(__file__).resolve().parent
repo = w.parents[2]
out = w / 'delivery'
java = Path('E:/Scoop/apps/corretto17-jdk/current/bin/java.exe')
llvm = Path('D:/Android/SDK/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin')
signer = repo / 'docs/enhancement/runs/20260912-154155/S0/sign-protected.py'
# Reuse the authorized local signing routine. It never reports credentials.
(out / 'original-signature.log').write_bytes((repo / 'nmm-protect/build/validation-20260912-private-loader/original-signature.log').read_bytes())
source = signer.read_text(encoding='utf-8').replace('out = Path(__file__).resolve().parent', 'out = Path(%r)' % str(out))
source = source.replace('root = out.parents[4]', 'root = Path(%r)' % str(repo))
source = source.replace('work = root / "nmm-protect/build/validation-20260912-device"', 'work = out')
exec(compile(source, str(signer), 'exec'), {'__file__': str(signer)})

classes = w / 'auditor-classes'
classes.mkdir(exist_ok=False)
subprocess.run([str(java.with_name('javac.exe')), '-encoding', 'UTF-8', '-cp', str(out / 'vm-protect.jar'),
                '-d', str(classes), str(repo / 'nmm-protect/scripts/AuditDemandMethods.java')], check=True)
with (out / 'method-audit.log').open('w', encoding='utf-8') as log:
    subprocess.run([str(java), '-cp', str(classes) + ';' + str(out / 'vm-protect.jar'),
                    'AuditDemandMethods', str(out), str(out), str(out / 'protected-signed.apk')],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
audit = json.loads((out / 'all-method-audit.json').read_text(encoding='utf-8'))
assert audit['count'] == 526 and audit['packagedNativeMethodSetExactMatch']
assert (out / 'selected-descriptors.txt').read_bytes() == (w.parent / 'on-demand-20260912/final-wipe-acceptance/selected-descriptors.txt').read_bytes()
build = out / 'input/build'
private = build / '.cxx/cmake/Release/arm64-v8a/private'
symbols = {}
for name, path in [('inner', private / 'libnmmp_inner.so'), ('outer', build / 'obj/sym/arm64-v8a/libc++_en.so')]:
    symbols[name] = subprocess.check_output([str(llvm / 'llvm-nm.exe'), '--defined-only', str(path)], text=True)
    (out / (name + '-symbols.txt')).write_text(symbols[name], encoding='utf-8')
assert all(n in symbols['inner'] for n in ['vmExecuteToken', 'nmmpNativeRun'])
assert 'nmmpNativeRun' in symbols['outer']
assert not any(n in symbols['outer'] for n in ['vmInterpret', 'vmExecuteToken', 'nmmp_key_share_'])
assert not re.search(r'\bvmExecute(?:Demand)?$', symbols['inner'], re.M)
assert not any(n in symbols['inner'] + symbols['outer'] for n in ['nmmpObserveDecoded', 'nmmpTest', 'BenchProbe'])
loader = json.loads((private / 'audit.json').read_text(encoding='utf-8'))
assert loader['stage0_vm'] and len(loader['stage0_program_hashes']) == 4
assert hashlib.sha256((private / 'libnmmp_inner.so').read_bytes()).hexdigest() == loader['elf_sha256']
with zipfile.ZipFile(out / 'protected-signed.apk') as apk:
    assert apk.read('lib/arm64-v8a/libc++_en.so') == (build / 'obj/strip/arm64-v8a/libc++_en.so').read_bytes()
manifest = json.loads((w / 'delivery.json').read_text(encoding='utf-8'))
manifest.update(apkSha256=hashlib.sha256((out / 'protected-signed.apk').read_bytes()).hexdigest(),
    apkBytes=(out / 'protected-signed.apk').stat().st_size, selectedMethods=526,
    oldExecutionAbsent=True, oldKeySharesAbsent=True, probeSymbolsAbsent=True,
    loaderBuildId=loader['build_id'], innerSha256=loader['elf_sha256'])
(w / 'result.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
plan = dict(adb=r'D:\Android\SDK\platform-tools\adb.exe', serial='192.168.6.118:5555',
    package='org.savior.sync', activity='org.savior.sync/.MainActivity', warmups=0, rounds=1,
    purpose='Default codec3-only functional identity check, not performance acceptance',
    variants=[dict(name='D', apk=str(out / 'protected-signed.apk'), loader=True, loaderId=loader['build_id'][:8])])
(w / 'startup-plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
print(json.dumps(manifest, indent=2))
