"""Fresh process concurrent first resolution and absence of bridge TLS symbols."""
from pathlib import Path
import hashlib
import json
import subprocess

here = Path(__file__).resolve().parent
out = here / 'publication'
out.mkdir(exist_ok=False)
results = []
with (out / 'commands.log').open('w') as log:
    def run(cmd):
        cmd = list(map(str, cmd))
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        log.write(json.dumps(dict(command=cmd, exitCode=r.returncode, output=r.stdout)) + '\n'); log.flush()
        assert r.returncode == 0, r.stdout
        return r.stdout
    run(['javac', '-d', out, here / 'ColdPublicationMain.java'])
    for mode in ['debug-hook', 'release-hook']:
        obj = here / 'host' / mode / 'DecodedWipeBridge.o'
        symbols = run(['nm', '-a', obj])
        forbidden = ['__cxa_thread_atexit', '__tls_get_addr', '__emutls', '_ZTH', '_ZTW']
        assert not any(s in symbols for s in forbidden)
        sections = run(['readelf', '-SW', obj])
        assert '.tdata' not in sections and '.tbss' not in sections
        result = run(['java', '--enable-native-access=ALL-UNNAMED', '-Xcheck:jni',
                      '-cp', str(out) + ':' + str(here / 'host/classes'), 'ColdPublicationMain',
                      here / 'host' / mode / 'libwipe.so'])
        assert 'COLD_PUBLICATION_PASS' in result and 'WARNING' not in result
        print(mode, result.strip(), flush=True)
        results.append(dict(mode=mode, output=result, bridgeTlsAbsent=True, processExitCode=0))
repo = here.parents[4]
frozen = json.loads((here / 'runtime-before.json').read_text())
assert all(hashlib.sha256((repo / p).read_bytes()).hexdigest() == h for p, h in frozen.items())
bridge = repo / 'nmmvm/nmmvm/src/test/semantic/DecodedWipeBridge.cpp'
assert bridge.read_bytes() == (here / 'DecodedWipeBridge.after.cpp').read_bytes()
(out / 'results.json').write_text(json.dumps(dict(results=results, productionRuntimeUnchanged=True,
    frozenBridgeSha256=hashlib.sha256(bridge.read_bytes()).hexdigest(), androidTestPerformed=False), indent=2))
