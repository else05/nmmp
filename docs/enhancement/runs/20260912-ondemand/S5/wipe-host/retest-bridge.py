"""Narrow recheck after making only the test entry padding assertion portable."""
from pathlib import Path
import hashlib
import json
import subprocess

here = Path(__file__).resolve().parent
old = here / 'run2'
out = here / 'padding-member-retest'
out.mkdir(exist_ok=False)
commands = []
for line in (old / 'host-tests.log').read_text().splitlines():
    if line.startswith('["'):
        commands.append(json.loads(line))
results = []
with (out / 'tests.log').open('w') as log:
    def run(cmd):
        log.write(json.dumps(cmd) + '\n'); log.flush()
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=120)
        log.write(result.stdout); log.flush()
        assert result.returncode == 0, result.stdout
        return result.stdout
    for mode in ['debug-hook', 'release-hook']:
        obj = out / (mode + '-bridge.o')
        compile_cmd = next(c for c in commands if c[0] == 'g++' and '-c' in c and
                           c[c.index('-c') + 1].endswith('/DecodedWipeBridge.cpp') and '/' + mode + '/' in c[-1])
        source = Path(compile_cmd[compile_cmd.index('-c') + 1])
        compile_cmd[-1] = str(obj)
        run(compile_cmd)
        link_cmd = next(c for c in commands if c[0] == 'g++' and '-shared' in c and '/' + mode + '/' in c[4])
        lib = out / (mode + '.so')
        link_cmd[4] = str(lib)
        link_cmd = [str(obj) if x.endswith('/DecodedWipeBridge.o') else x for x in link_cmd]
        run(link_cmd)
        result = run(['java', '--enable-native-access=ALL-UNNAMED', '-Xcheck:jni', '-cp', str(old / 'classes'),
                      'com.nmmedit.semantic.DecodedWipeMain', str(lib)])
        assert 'DECODE_WIPE_PASS' in result and 'WARNING' not in result
        print(mode, result.strip(), flush=True)
        results.append(dict(mode=mode, result=result, librarySha256=hashlib.sha256(lib.read_bytes()).hexdigest()))
    size = run([str(here / 'size')])
(out / 'results.json').write_text(json.dumps(dict(results=results, sizeof=size,
    bridgeSha256=hashlib.sha256(source.read_bytes()).hexdigest(), reusedInterpreterManifest=str(old / 'manifest.json')), indent=2))
