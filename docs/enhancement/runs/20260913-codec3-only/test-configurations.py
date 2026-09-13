from pathlib import Path
import json
import os
import subprocess
import zipfile

w = Path(__file__).resolve().parent
env = os.environ.copy()
names = ['NMMP_VM_DECODE_MODE', 'NMMP_PRIVATE_LINKER', 'NMMP_PRIVATE_STAGE0_VM']
for name in names + ['JAVA_TOOL_OPTIONS', '_JAVA_OPTIONS', 'JDK_JAVA_OPTIONS']:
    env.pop(name, None)
java = r'E:\Scoop\apps\corretto17-jdk\current\bin\java.exe'
base = [java, '-Dfile.encoding=UTF-8', '--class-path', str(w / 'delivery/vm-protect.jar'), str(w / 'CheckOptions.java')]
cases = [('defaults', {}, None), ('explicit-new', dict(zip(names, ['on-demand-v1', 'ON', 'ON'])), None)]
cases += [(name, {name: value}, message) for name, value, message in [
    (names[0], 'legacy', 'legacy has been removed'), (names[1], 'OFF', 'is mandatory'),
    (names[2], 'OFF', 'is mandatory')]]
results = []
for name, options, expected in cases:
    p = subprocess.run(base, env=env | options, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, encoding='utf-8', errors='replace', timeout=45)
    assert (p.returncode == 0 and 'CONFIG_PASS' in p.stdout) if expected is None else (p.returncode != 0 and expected in p.stdout)
    results.append(dict(case=name, exitCode=p.returncode, output=p.stdout))
old_template = w / 'rejected-template4.zip'
with zipfile.ZipFile(w.parent / 'libs/vm-protect-2026-09-13-0526.jar') as archive:
    old_template.write_bytes(archive.read('vmsrc.zip'))
p = subprocess.run(base + [str(old_template)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   text=True, encoding='utf-8', errors='replace', timeout=45)
assert p.returncode != 0 and '期望版本 5' in p.stdout, p.stdout
results.append(dict(case='reject-template4', exitCode=p.returncode, output=p.stdout))
(w / 'configuration-tests.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print('PASS: defaults, explicit new values, three removed choices and template4 rejection')
