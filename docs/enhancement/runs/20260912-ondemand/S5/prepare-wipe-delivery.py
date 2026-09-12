"""Prepare fresh post-review outputs; preserve all frozen S5 artifacts."""
from pathlib import Path
import hashlib
import json
import shutil
import sys

w = Path(__file__).resolve().parent
root = w.parents[2]
jar = Path(sys.argv[1]).resolve()
original = Path('D:/AndroidProjects/sync-ui/app/build/outputs/apk/release/MdoHelper_v1.6.0M_202609092340_release.apk')
if hashlib.sha256(original.read_bytes()).hexdigest() != '660e2ddc18a500b86ed4ab076b5fe72cbc37ec6400f2ff3ae9200344befb84a5':
    raise ValueError('Authorized input changed')
for name in ['final-wipe-acceptance', 'wipe-legacy-default', 'wipe-hotspot-D']:
    output = w / name
    output.mkdir(exist_ok=False)
    shutil.copyfile(jar, output / 'vm-protect.jar')
    if name != 'wipe-hotspot-D':
        (output / 'input').mkdir()
        shutil.copyfile(original, output / 'input/original.apk')
manifest = dict(jar=str(jar), jarSha256=hashlib.sha256(jar.read_bytes()).hexdigest(),
                previousCandidate=str(w / 'final-acceptance/protected-signed.apk'),
                reason='Explicit decoded scratch cleanup found during final contract review',
                frozenS5ArtifactsChanged=False)
(w / 'wipe-delivery.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
script = '''#!/usr/bin/env bash
set -euo pipefail
root=/mnt/e/OtherProject/safe-toolchain/nmmp
work=$root/nmm-protect/build/on-demand-20260912
source /mnt/d/Android/SDK_WSL/nmmp-env.sh
export OMVLL_CONFIG="$root/nmm-protect/scripts/wsl/omvll-config.py"
export CMAKE_BUILD_PARALLEL_LEVEL=5 NMMP_PRIVATE_STAGE0_VM=ON
unset NMMP_TEST_SEED
export NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON
cd "$work/final-wipe-acceptance"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo 'Post-review unseeded demand/private APK built'
export NMMP_VM_DECODE_MODE=legacy NMMP_PRIVATE_LINKER=OFF
cd "$work/wipe-legacy-default"
java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1
echo 'Post-review default legacy APK built'
export NMMP_TEST_SEED=0123456789abcdef NMMP_VM_DECODE_MODE=on-demand-v1 NMMP_PRIVATE_LINKER=ON
cd "$work/wipe-hotspot-D"
java -cp "$work/s5-harness/generator-classes:$PWD/vm-protect.jar" GenerateBench "$work/s5-harness/impl-dex/classes.dex" "$PWD/build" > build.log 2>&1
echo 'Post-review seed1 ART hotspot library built'
'''
(w / 'build-wipe-delivery.sh').write_bytes(script.encode('utf-8'))
print(json.dumps(manifest))
