from pathlib import Path
import hashlib
import json
import shutil
import sys
import zipfile

w = Path(__file__).resolve().parent
root = w.parents[2]
jar = Path(sys.argv[1]).resolve()
original = Path('D:/AndroidProjects/sync-ui/app/build/outputs/apk/release/MdoHelper_v1.6.0M_202609092340_release.apk')
assert hashlib.sha256(original.read_bytes()).hexdigest() == '660e2ddc18a500b86ed4ab076b5fe72cbc37ec6400f2ff3ae9200344befb84a5'
template = root / 'nmm-protect/apkprotect/src/main/resources/vmsrc.zip'
with zipfile.ZipFile(jar) as archive:
    assert archive.read('vmsrc.zip') == template.read_bytes()
with zipfile.ZipFile(template) as archive:
    assert '#define NMMP_VM_TEMPLATE_VERSION 5' in archive.read('vm/include/VmCodecConfig.h').decode()
    assert '#define NMMP_VM_CODEC_VERSION 3' in archive.read('vm/include/VmCodecConfig.h').decode()
    assert not any(n in archive.namelist() for n in ['vm/Codec.cpp', 'vm/Demand.cpp'])
    assert 'vmEncodedCode' not in archive.read('vm/include/vm.h').decode()
    assert 'nmmp_key_share_' not in archive.read('loader/Outer.c').decode()
    for folder in ['vm', 'cutils']:
        for source in (root / 'nmmvm/nmmvm/src/main/cpp' / folder).rglob('*'):
            if source.is_file() and source.name != 'CMakeLists.txt':
                assert archive.read(folder + '/' + source.relative_to(root / 'nmmvm/nmmvm/src/main/cpp' / folder).as_posix()) == source.read_bytes(), source
out = w / 'delivery'
(out / 'input').mkdir(parents=True, exist_ok=False)
shutil.copyfile(jar, out / 'vm-protect.jar')
shutil.copyfile(original, out / 'input/original.apk')
(w / 'delivery.json').write_text(json.dumps(dict(jar=str(jar), jarSha256=hashlib.sha256(jar.read_bytes()).hexdigest(),
    templateVersion=5, codecVersion=3, privateLinker=True, stage0Vm=True, templateExact=True), indent=2), encoding='utf-8')
print('Final template verified; fresh default-mode APK input prepared')
