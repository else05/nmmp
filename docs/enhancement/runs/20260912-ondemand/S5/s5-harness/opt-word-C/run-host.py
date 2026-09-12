"""Host-only checks; run with WSL python3. Never invokes Android tools."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

experiment = Path(__file__).resolve().parent
repo = experiment.parents[4]
w = experiment.parents[1]
source = w / 's5/seed1/hotspot-C/build/dex2c'
output = experiment / 'build/dex2c'
host = experiment / 'host'
host.mkdir(exist_ok=True)
manifest = json.loads((experiment / 'source-manifest.json').read_text())


def inventory(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}


if inventory(source) != manifest['sourceSha256'] or inventory(output) != manifest['outputSha256']:
    raise RuntimeError('Source/copy drift before host tests')
protected = [repo / 'nmmvm/nmmvm/src/main/cpp/vm/include/VmReader.h',
             repo / 'nmm-protect/build/libs/vm-protect-2026-09-13-0136.jar',
             w / 's5/seed1/hotspot-C/vm-protect.jar']
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
results = []
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
with (experiment / 'host-tests.log').open('w') as log:
    for profile, flags in [('release', ['-O2']), ('asan-ubsan', [
            '-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-fno-pie', '-no-pie'])]:
        for name, test, include in [
            ('original-reader', repo / 'nmmvm/nmmvm/src/test/cpp/VmReaderTest.cpp', source / 'vm/include'),
            ('optimized-reader', repo / 'nmmvm/nmmvm/src/test/cpp/VmReaderTest.cpp', output / 'vm/include'),
            ('word-differential', experiment / 'tests/WordDifferentialTest.cpp', output / 'vm/include')]:
            binary = host / (name + '-' + profile)
            command = ['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror'] + flags + [
                str(test), '-I', str(include), '-o', str(binary)]
            log.write(json.dumps(command) + '\n'); log.flush()
            compiled = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True, timeout=120)
            log.write(compiled.stdout); log.flush()
            if compiled.returncode:
                raise RuntimeError('Compile failed: ' + name + '/' + profile)
            run = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, env=env, timeout=60)
            log.write(run.stdout); log.flush()
            results.append({'name': name, 'profile': profile, 'exitCode': run.returncode,
                            'output': run.stdout, 'compileCommand': command})
            print(profile, name, run.returncode, run.stdout.strip(), flush=True)
            if run.returncode:
                raise RuntimeError('Test failed: ' + name + '/' + profile)
if inventory(source) != manifest['sourceSha256'] or inventory(output) != manifest['outputSha256']:
    raise RuntimeError('Source/copy drift during host tests')
if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != value for p, value in hashes.items()):
    raise RuntimeError('Production header/JAR drift during host tests')
functions = output / 'generated/classes_native_functions.c'
native_methods = functions.read_text().count('vmExecuteToken(')
if native_methods != 8:
    raise RuntimeError('Expected original 8-native harness')
(experiment / 'host-results.json').write_text(json.dumps({
    'tests': results, 'passed': len(results), 'nativeMethods': native_methods,
    'sourceAndCopyMatchManifest': True, 'productionHeaderAndJarSha256': hashes,
    'androidBuild': False, 'deviceExecution': False, 'performanceMeasured': False}, indent=2))
