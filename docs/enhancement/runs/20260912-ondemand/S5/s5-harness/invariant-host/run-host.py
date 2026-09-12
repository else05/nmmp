"""Compile actual original/isolated interpreters for host OpenJDK JNI; no Android/ADB."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil
import subprocess

here = Path(__file__).resolve().parent
work = here.parent
repo = work.parents[3]
jdk = Path(shutil.which('javac')).resolve().parents[1]
semantic = repo / 'nmmvm/nmmvm/src/test/semantic'
classes = here / 'classes'
classes.mkdir(exist_ok=True)
source = (semantic / 'SemanticBridge.cpp').read_text()
old = 'static const vmResolver resolver = {nullptr, resolveMethod, nullptr, resolveClass, nullptr, nullptr};'
new = ('static const vmResolver resolver = [] { vmResolver r = {}; '
       'r.dvmResolveMethod = resolveMethod; r.dvmResolveClass = resolveClass; return r; }();')
if source.count(old) != 1:
    raise RuntimeError('Unexpected existing semantic fixture')
adapted = source.replace(old, new)
(here / 'SemanticBridge.cpp').write_text(adapted)
(here / 'semantic-fixture.patch').write_text(''.join(difflib.unified_diff(
    source.splitlines(True), adapted.splitlines(True), fromfile='a/SemanticBridge.cpp', tofile='b/SemanticBridge.cpp')))
results = []


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_copy(name):
    experiment = work / ('opt-invariant-' + name)
    m = json.loads((experiment / 'manifest.json').read_text())
    # Manifest paths were recorded on Windows; use these exact corresponding WSL roots.
    source_root = work / 'current-legacy/build/dex2c' if name == 'L' else work.parent / 's5/seed1/hotspot-C/build/dex2c'
    for root, hashes in [(source_root, m['sourceSha256']), (experiment / 'build/dex2c', m['outputSha256'])]:
        actual = {p.relative_to(root).as_posix(): sha(p) for p in root.rglob('*') if p.is_file()}
        if actual != hashes:
            raise RuntimeError('Runtime source drift: ' + str(root))
    return source_root, experiment / 'build/dex2c'


with (here / 'host-tests.log').open('w') as log:
    def run(command, timeout=120):
        log.write(json.dumps(list(map(str, command))) + '\n'); log.flush()
        r = subprocess.run(list(map(str, command)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, timeout=timeout)
        log.write(r.stdout); log.flush()
        if r.returncode:
            print(r.stdout[-5000:], flush=True)
            raise RuntimeError('Host command failed, see host-tests.log')
        return r.stdout

    run(['javac', '-d', classes, semantic / 'SemanticMain.java', here / 'InvariantMain.java'])
    for name in ['L', 'C']:
        original, optimized = verify_copy(name)
        for kind, root in [('original', original), ('optimized', optimized)]:
            build = here / (name + '-' + kind)
            build.mkdir(exist_ok=True)
            flags = ['-O2', '-g', '-fPIC', '-DNDEBUG', '-DHAVE_LITTLE_ENDIAN', '-DHAVE_ENDIAN_H',
                     '-I' + str(here / 'include'), '-I' + str(jdk / 'include'), '-I' + str(jdk / 'include/linux'),
                     '-I' + str(root), '-I' + str(root / 'vm'), '-I' + str(root / 'vm/include')]
            if name == 'C': flags += ['-DNMMP_TEST_DEMAND=1']
            cpp = ['InterpC-portable.cpp', 'Interp.cpp', 'DexCatch.cpp', 'Exception.cpp', 'GlobalCache.cpp']
            c = ['JNIWrapper.c']
            if name == 'C':
                cpp += ['Demand.cpp', 'VmCodec.cpp']
                c += ['NativeVm.c', 'VmInit.c']
            objects = []
            for file in [root / 'vm' / p for p in cpp + c] + [here / 'SemanticBridge.cpp', here / 'InvariantBridge.cpp']:
                obj = build / (file.stem + '.o')
                compiler, standard = ('gcc', '-std=gnu11') if file.suffix == '.c' else ('g++', '-std=gnu++11')
                run([compiler, standard] + flags + ['-c', file, '-o', obj])
                objects.append(obj)
            library = build / 'libinvariant.so'
            run(['g++', '-shared', '-Wl,-z,defs', '-o', library] + objects + ['-pthread'])
            for main, args in [('com.nmmedit.semantic.SemanticMain', ['reader'] + (['demand'] if name == 'C' else [])),
                               ('InvariantMain', [])]:
                output = run(['java', '-Xcheck:jni', '-cp', classes, main, library] + args, 60)
                if '_PASS checks=' not in output:
                    raise RuntimeError('Missing semantic success marker')
                results.append({'variant': name, 'interpreter': kind, 'test': main,
                                'output': output, 'librarySha256': sha(library)})
                print(name, kind, main, output.strip(), flush=True)
        verify_copy(name)
(here / 'host-results.json').write_text(json.dumps({'runs': results, 'passed': len(results),
    'runtime': 'Host OpenJDK real JNI; Android log output stubbed', 'androidBuild': False,
    'deviceExecution': False, 'performanceMeasured': False, 'sourceManifestsUnchanged': True,
    'fixtureSourceSha256': {p.name: sha(p) for p in [semantic / 'SemanticMain.java',
        here / 'SemanticBridge.cpp', here / 'InvariantMain.java', here / 'InvariantBridge.cpp',
        here / 'include/android/log.h']}}, indent=2))
