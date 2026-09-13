from pathlib import Path
import hashlib
import json
import subprocess

w = Path(__file__).resolve().parent
old = w.parent / 'on-demand-20260912'
out = w / 'semantic-device'
out.mkdir(exist_ok=False)
adb = [r'D:\Android\SDK\platform-tools\adb.exe', '-s', '192.168.6.118:5555']
remote = '/data/local/tmp/nmmp-codec3-only-20260913'

def run(args):
    p = subprocess.run(adb + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, encoding='utf-8', errors='replace', timeout=180)
    with (out / 'commands.jsonl').open('a', encoding='utf-8') as f:
        f.write(json.dumps(dict(args=args, code=p.returncode, output=p.stdout)) + '\n')
    if p.returncode:
        raise RuntimeError(p.stdout)
    return p.stdout

identity = {name: run(['shell', 'getprop', prop]).strip() for name, prop in
            [('api', 'ro.build.version.sdk'), ('abi', 'ro.product.cpu.abi')]}
assert identity == dict(api='27', abi='arm64-v8a'), identity
run(['shell', 'mkdir', '-p', remote])
run(['push', str(old / 'wipe-semantic-dex/classes.dex'), remote + '/classes.dex'])
run(['push', str(old / 's4-vectors/native-program-vectors.bin'), remote + '/vectors.bin'])
results = []
for mode in ['Release', 'Debug']:
    build = w / ('semantic-' + mode)
    hashes = {}
    for relative, name in [('libnmmp_semantic.so', 'libnmmp_semantic.so'), ('vm/libnmmvm.so', 'libnmmvm.so'),
                           ('nmmp_native_vm_test', 'nmmp_native_vm_test'), ('nmmp_native_root_test', 'nmmp_native_root_test')]:
        source = build / relative
        hashes[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        run(['push', str(source), remote + '/' + name])
    run(['shell', 'chmod 755 ' + remote + '/nmmp_native_vm_test ' + remote + '/nmmp_native_root_test'])
    prefix = 'LD_LIBRARY_PATH=' + remote + ' '
    vectors = run(['shell', prefix + remote + '/nmmp_native_vm_test ' + remote + '/vectors.bin'])
    root = run(['shell', prefix + remote + '/nmmp_native_root_test'])
    prefix = 'CLASSPATH=' + remote + '/classes.dex ' + prefix + 'app_process /system/bin com.nmmedit.semantic.'
    semantic = run(['shell', prefix + 'SemanticMain ' + remote + '/libnmmp_semantic.so reader demand'])
    wipe = run(['shell', prefix + 'DecodedWipeMain ' + remote + '/libnmmp_semantic.so'])
    assert '105 independent' in vectors and '1600 concurrent activations' in root
    assert 'SEMANTIC_PASS checks=1351' in semantic and 'DECODE_WIPE_PASS runs=801' in wipe
    results.append(dict(mode=mode, librarySha256=hashes, vectors=vectors, root=root, semantic=semantic, wipe=wipe))
    print(mode + ': ' + semantic.strip() + '; ' + wipe.strip(), flush=True)
(out / 'summary.json').write_text(json.dumps(dict(identity=identity, results=results), indent=2), encoding='utf-8')
