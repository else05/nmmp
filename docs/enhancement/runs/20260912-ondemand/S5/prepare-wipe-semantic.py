from pathlib import Path
import hashlib
import json
import shutil
import subprocess

w = Path(__file__).resolve().parent
root = w.parents[2]
production = root / 'nmmvm/nmmvm/src/main/cpp'
source = w / 's4-semantic-src'
target = w / 'wipe-semantic-src'
before = {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in source.rglob('*') if p.is_file()}
shutil.copytree(source, target)
for relative in ['vm/InterpC-portable.cpp', 'vm/include/VmDecodedState.h']:
    shutil.copyfile(production / relative, target / relative)
after = {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
         for p in target.rglob('*') if p.is_file()}
changed = [p for p in after if after[p] != before.get(p)]
if set(changed) != {'vm/InterpC-portable.cpp', 'vm/include/VmDecodedState.h'}:
    raise ValueError('Unexpected semantic runtime changes')
(w / 'wipe-semantic-source.json').write_text(json.dumps(dict(snapshot=before, output=after, changed=changed), indent=2), encoding='utf-8')
classes = w / 'wipe-semantic-classes'
dex = w / 'wipe-semantic-dex'
classes.mkdir(exist_ok=False)
dex.mkdir(exist_ok=False)
jdk = Path('E:/Scoop/apps/corretto17-jdk/current/bin')
semantic = root / 'nmmvm/nmmvm/src/test/semantic'
subprocess.run([str(jdk / 'javac.exe'), '--release', '8', '-d', str(classes),
                str(semantic / 'SemanticMain.java'), str(semantic / 'DecodedWipeMain.java')], check=True)
subprocess.run([str(jdk / 'java.exe'), '-cp', 'D:/Android/SDK/build-tools/34.0.0/lib/d8.jar',
                'com.android.tools.r8.D8', '--min-api', '26', '--output', str(dex)]
               + [str(p) for p in classes.rglob('*.class')], check=True)
script = (w / 'build-s4-semantic.sh').read_text(encoding='utf-8').replace('s4-semantic-', 'wipe-semantic-')
script = script.replace('-DNMMP_TEST_DEMAND=ON', '-DNMMP_TEST_DEMAND=ON -DNMMP_TEST_DECODE_WIPE=ON')
(w / 'build-wipe-semantic.sh').write_bytes(script.encode('utf-8'))
print('Prepared new ARM64 semantic source snapshot and DEX')
