"""WSL host OpenJDK JNI regression; writes only beneath a fresh --out directory."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
work = here.parent
repo = work.parents[2]
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
semantic = repo / 'nmmvm/nmmvm/src/test/semantic'
production = repo / 'nmmvm/nmmvm/src/main/cpp'
snapshot = work / 's4-semantic-src'
root = out / 'cpp'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def tree(p): return {x.relative_to(p).as_posix(): sha(x) for x in p.rglob('*') if x.is_file()}
before = tree(snapshot)
shutil.copytree(snapshot, root)
overlay = ['vm/InterpC-portable.cpp', 'vm/include/VmDecodedState.h']
for name in overlay: shutil.copyfile(production / name, root / name)
include = out / 'include/android'
include.mkdir(parents=True)
shutil.copyfile(work / 's5-harness/invariant-host/include/android/log.h', include / 'log.h')
source = (semantic / 'SemanticBridge.cpp').read_text()
old = 'static const vmResolver resolver = {nullptr, resolveMethod, nullptr, resolveClass, nullptr, nullptr};'
new = ('static const vmResolver resolver = [] { vmResolver r = {}; '
       'r.dvmResolveMethod = resolveMethod; r.dvmResolveClass = resolveClass; return r; }();')
assert source.count(old) == 1
adapted = source.replace(old, new)
(out / 'SemanticBridge.cpp').write_text(adapted)
(out / 'semantic-fixture.patch').write_text(''.join(difflib.unified_diff(
    source.splitlines(True), adapted.splitlines(True), fromfile='a/SemanticBridge.cpp', tofile='b/SemanticBridge.cpp')))
jdk = Path(shutil.which('javac')).resolve().parents[1]
results = []
with (out / 'host-tests.log').open('w') as log:
    def run(cmd):
        log.write(json.dumps(list(map(str, cmd))) + '\n'); log.flush()
        r = subprocess.run(list(map(str, cmd)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
        log.write(r.stdout); log.flush()
        if r.returncode:
            print(r.stdout[-7000:], flush=True)
            raise RuntimeError('Command failed: see ' + str(out / 'host-tests.log'))
        return r.stdout
    classes = out / 'classes'
    run(['javac', '-d', classes, semantic / 'SemanticMain.java', semantic / 'DecodedWipeMain.java'])
    for mode, opt, hook in [('debug-hook', '-O0', True), ('release-hook', '-O2', True), ('release', '-O2', False)]:
        build = out / mode
        build.mkdir()
        flags = [opt, '-g', '-fPIC', '-DNDEBUG', '-DHAVE_LITTLE_ENDIAN', '-DHAVE_ENDIAN_H', '-DNMMP_TEST_DEMAND=1',
                 '-I' + str(out / 'include'), '-I' + str(jdk / 'include'), '-I' + str(jdk / 'include/linux'),
                 '-I' + str(root), '-I' + str(root / 'vm'), '-I' + str(root / 'vm/include')]
        if hook: flags += ['-DNMMP_TEST_DECODE_WIPE=1']
        sources = [root / 'vm' / name for name in ['InterpC-portable.cpp', 'Interp.cpp', 'DexCatch.cpp',
                   'Exception.cpp', 'GlobalCache.cpp', 'Demand.cpp', 'VmCodec.cpp', 'JNIWrapper.c', 'NativeVm.c', 'VmInit.c']]
        sources += [out / 'SemanticBridge.cpp']
        if hook: sources += [semantic / 'DecodedWipeBridge.cpp']
        objects = []
        for src in sources:
            obj = build / (src.stem + '.o')
            compiler, standard = ('gcc', '-std=gnu11') if src.suffix == '.c' else ('g++', '-std=gnu++11')
            run([compiler, standard] + flags + ['-c', src, '-o', obj])
            objects.append(obj)
        lib = build / 'libwipe.so'
        run(['g++', '-shared', '-Wl,-z,defs', '-o', lib] + objects + ['-pthread'])
        symbols = run(['nm', '-a', lib])
        assert ('nmmpObserveDecodedState' in symbols) == hook
        assert ('DecodedWipeMain_eval' in symbols) == hook
        for main, extra in [('SemanticMain', ['reader', 'demand'])] + ([('DecodedWipeMain', [])] if hook else []):
            result = run(['java', '--enable-native-access=ALL-UNNAMED', '-Xcheck:jni', '-cp', classes, 'com.nmmedit.semantic.' + main, lib] + extra)
            assert '_PASS ' in result
            assert 'WARNING' not in result and 'FATAL' not in result, result
            print(mode, main, result.strip(), flush=True)
            results.append(dict(mode=mode, test=main, output=result, librarySha256=sha(lib)))
assert before == tree(snapshot), 'Original S4 snapshot changed'
assert all(sha(root / name) == sha(production / name) for name in overlay), 'Overlay drift'
(out / 'manifest.json').write_text(json.dumps(dict(snapshot=str(snapshot), snapshotSha256=before,
    outputSha256=tree(root), overlay=overlay, tests={p.name: sha(p) for p in
    [semantic / 'DecodedWipeBridge.cpp', semantic / 'DecodedWipeMain.java', semantic / 'SemanticMain.java', Path(__file__)]},
    results=results, originalSnapshotUnchanged=True, androidTools=False, runtime='Host OpenJDK real JNI, Android log stub'), indent=2))
