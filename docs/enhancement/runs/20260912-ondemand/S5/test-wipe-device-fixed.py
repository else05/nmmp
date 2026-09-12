from pathlib import Path
import hashlib
import json
import subprocess

w = Path(__file__).resolve().parent
out = w / 'wipe-device-fixed'
out.mkdir(exist_ok=False)
adb = [r'D:\Android\SDK\platform-tools\adb.exe', '-P', '5038', '-s', '192.168.6.118:5555']
remote = '/data/local/tmp/nmmp-on-demand/wipe-fixed-reviewed'


def run(args):
    result = subprocess.run(adb + args, text=True, encoding='utf-8', errors='replace',
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    with (out / 'commands.jsonl').open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(dict(args=args, code=result.returncode, output=result.stdout)) + '\n')
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout


identity = {key: run(['shell', 'getprop', prop]).strip() for key, prop in
            [('api', 'ro.build.version.sdk'), ('abi', 'ro.product.cpu.abi'), ('device', 'ro.product.device')]}
if identity['api'] != '27' or identity['abi'] != 'arm64-v8a':
    raise RuntimeError('Unexpected device')
(out / 'identity.json').write_text(json.dumps(identity, indent=2), encoding='utf-8')
run(['shell', 'mkdir', '-p', remote])
run(['push', str(w / 'wipe-semantic-dex/classes.dex'), remote + '/classes.dex'])
run(['push', str(w / 's4-vectors/native-program-vectors.bin'), remote + '/native-program-vectors.bin'])
results = []
for mode in ['Release', 'Debug']:
    build = w / ('wipe-fixed-semantic-' + mode)
    libraries = {}
    for relative, name in [('libnmmp_semantic.so', 'libnmmp_semantic.so'), ('vm/libnmmvm.so', 'libnmmvm.so'),
                           ('nmmp_native_vm_test', 'nmmp_native_vm_test'), ('nmmp_native_root_test', 'nmmp_native_root_test')]:
        source = build / relative
        libraries[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        run(['push', str(source), remote + '/' + name])
    run(['shell', 'chmod 755 ' + remote + '/nmmp_native_vm_test ' + remote + '/nmmp_native_root_test'])
    prefix = 'LD_LIBRARY_PATH=' + remote + ' '
    outputs = {}
    outputs['nativeVm'] = run(['shell', prefix + remote + '/nmmp_native_vm_test ' + remote + '/native-program-vectors.bin'])
    outputs['root'] = run(['shell', prefix + remote + '/nmmp_native_root_test'])
    prefix = 'CLASSPATH=' + remote + '/classes.dex ' + prefix + 'app_process /system/bin com.nmmedit.semantic.'
    outputs['semantic'] = run(['shell', prefix + 'SemanticMain ' + remote + '/libnmmp_semantic.so reader demand'])
    outputs['wipe'] = run(['shell', prefix + 'DecodedWipeMain ' + remote + '/libnmmp_semantic.so'])
    if ('PASS: 105 independent Java/native program vectors' not in outputs['nativeVm']
            or 'SEMANTIC_PASS checks=1351' not in outputs['semantic']
            or 'DECODE_WIPE_PASS runs=801' not in outputs['wipe']):
        raise RuntimeError('Missing expected native/semantic/wipe result')
    (out / (mode + '.log')).write_text('\n'.join(outputs.values()), encoding='utf-8')
    results.append(dict(mode=mode, librarySha256=libraries, outputs=outputs))
    print(mode + ': ' + outputs['semantic'].strip() + '; ' + outputs['wipe'].strip(), flush=True)
for mode in ['Release', 'Debug']:
    build = w / ('wipe-fixed-legacy-semantic-' + mode)
    libraries = {}
    for relative, name in [('libnmmp_semantic.so', 'libnmmp_semantic.so'), ('vm/libnmmvm.so', 'libnmmvm.so')]:
        source = build / relative
        libraries[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        run(['push', str(source), remote + '/' + name])
    output = run(['shell', 'CLASSPATH=' + remote + '/classes.dex LD_LIBRARY_PATH=' + remote
                  + ' app_process /system/bin com.nmmedit.semantic.SemanticMain '
                  + remote + '/libnmmp_semantic.so reader'])
    if 'SEMANTIC_PASS checks=1338' not in output:
        raise RuntimeError('Missing legacy semantic result')
    (out / ('legacy-' + mode + '.log')).write_text(output, encoding='utf-8')
    results.append(dict(mode='legacy-' + mode, librarySha256=libraries, output=output))
    print('legacy-' + mode + ': ' + output.strip(), flush=True)
(out / 'summary.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
