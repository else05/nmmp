from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import zipfile

w = Path(__file__).resolve().parent
java = r'E:\Scoop\apps\corretto17-jdk\current\bin\java.exe'
llvm = Path('D:/Android/SDK/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin')
delivery = json.loads((w / 'wipe-delivery.json').read_text(encoding='utf-8'))
randomness = json.loads((w / 'wipe-randomness-evidence.json').read_text(encoding='utf-8'))
randomness_keys = ['testSeedAbsentInSameEnvironmentProbe', 'secureRandomInSameEnvironmentProbe',
                   'explicitBuildSeedUnset', 'jvmOptionInjectionBannerAbsentInCapturedBuildLog']
if not all(randomness.get(key) is True for key in randomness_keys):
    raise ValueError('Production randomness evidence incomplete')


def run(args):
    return subprocess.check_output([str(arg) for arg in args], text=True, encoding='utf-8', errors='replace')


for name, auditor in [('final-wipe-acceptance', 'AuditDemandMethods'), ('wipe-legacy-default', 'AuditMethods')]:
    base = w / name
    subprocess.run([sys.executable, '-B', str(w / 'sign-variant.py'), name], check=True)
    with (base / 'method-audit.log').open('w', encoding='utf-8') as stream:
        subprocess.run([java, '-cp', str(w / 's5-harness/auditor-classes') + ';' + delivery['jar'],
                        auditor, str(base), str(base), str(base / 'protected-signed.apk')],
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
    audit = json.loads((base / 'all-method-audit.json').read_text(encoding='utf-8'))
    if (audit['count'] != 526 or not audit['packagedNativeMethodSetExactMatch']
            or (base / 'selected-descriptors.txt').read_bytes() != (w / 's5/seed1/A/selected-descriptors.txt').read_bytes()):
        raise ValueError('Post-review method coverage changed')

base = w / 'final-wipe-acceptance'
build = base / 'input/build'
private = build / '.cxx/cmake/Release/arm64-v8a/private'
outer = build / 'obj/sym/arm64-v8a/libc++_en.so'
inner = private / 'libnmmp_inner.so'
symbols = {}
for name, path in [('outer', outer), ('inner', inner)]:
    symbols[name] = run([llvm / 'llvm-nm.exe', '--defined-only', path])
    (base / (name + '-symbols.txt')).write_text(symbols[name], encoding='utf-8')
headers = run([llvm / 'llvm-readelf.exe', '-l', '-d', '--wide', outer])
(base / 'outer-headers.txt').write_text(headers, encoding='utf-8')
loader = json.loads((private / 'audit.json').read_text(encoding='utf-8'))
with zipfile.ZipFile(base / 'protected-signed.apk') as apk:
    libraries = [n for n in apk.namelist() if n.endswith('.so')]
    if apk.read('lib/arm64-v8a/libc++_en.so') != (build / 'obj/strip/arm64-v8a/libc++_en.so').read_bytes():
        raise ValueError('Packaged protection library differs from build')
checks = dict(
    outerStage0Vm='nmmpNativeRun' in symbols['outer'] and loader['stage0_vm'],
    outerBusinessVmAbsent=not any(n in symbols['outer'] for n in ['vmInterpret', 'vmExecuteToken', 'initializeSeed']),
    innerTokenAndNativeVm=all(n in symbols['inner'] for n in ['vmExecuteToken', 'nmmpNativeRun']),
    oldOrTransitionalExecutionAbsent=not re.search(r'\bvmExecute(?:Demand)?$', symbols['inner'], re.M),
    probeSymbolsAbsent=not any(n in symbols['outer'] + symbols['inner'] for n in ['nmmpTest', 'nmmpObserveDecoded', 'BenchProbe', '__wrap_malloc']),
    oldKeySharesAbsent=not any(n in symbols['outer'] for n in ['nmmp_key_share', 'nmmpKeyShare', 'key_share_']),
    directInnerNeededAbsent=not any('inner' in line for line in headers.splitlines() if '(NEEDED)' in line),
    tlsSegmentAbsent=not re.search(r'^\s*TLS\s', headers, re.M),
    wxLoadAbsent=not any('W' in ''.join(line.split()[6:-1]) and 'E' in ''.join(line.split()[6:-1])
                        for line in headers.splitlines() if re.match(r'^\s*LOAD\s', line)))
if not all(checks.values()):
    raise ValueError('Post-review structure audit failed: ' + repr(checks))
summary = dict(apkSha256=hashlib.sha256((base / 'protected-signed.apk').read_bytes()).hexdigest(),
               jarSha256=delivery['jarSha256'], mode='on-demand-v1', codecVersion=3, templateVersion=4,
               signatureBinding=True, privateLinker=True, stage0Vm=True,
               fixedTestSeedUsed=not all(randomness[key] for key in randomness_keys), randomnessEvidence=randomness,
               selectedMethods=526, explicitDecodedScratchCleanup=True,
               innerSha256=hashlib.sha256(inner.read_bytes()).hexdigest(), apkLibraries=libraries, **checks)
if summary['innerSha256'] != loader['elf_sha256']:
    raise ValueError('Loader audit hash mismatch')
(base / 'structure-audit.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
