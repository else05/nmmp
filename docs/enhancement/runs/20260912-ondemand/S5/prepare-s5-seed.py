from pathlib import Path
import sys,shutil,json
w=Path(__file__).resolve().parent; root=w.parents[2]; seed_index=int(sys.argv[1]); jars=json.loads((w/'s5-jars.json').read_text())
seeds=['0123456789abcdef','fedcba9876543210','6a09e667f3bcc909'];seed=seeds[seed_index-1]; base=w/'s5'/('seed'+str(seed_index));base.mkdir(parents=True)
def posix(p):
 p=p.resolve().as_posix();return '/mnt/'+p[0].lower()+p[2:]
for name in ['A','B','C','D']:
 o=base/name;(o/'input').mkdir(parents=True);shutil.copy2(w/'p5-combined/input/original.apk',o/'input/original.apk');shutil.copy2(jars['baseline' if name in ['A','B'] else 'current'],o/'vm-protect.jar')
 h=base/('hotspot-'+name);h.mkdir();shutil.copy2(o/'vm-protect.jar',h/'vm-protect.jar')
lines=['#!/usr/bin/env bash','set -euo pipefail','source /mnt/d/Android/SDK_WSL/nmmp-env.sh','export CMAKE_BUILD_PARALLEL_LEVEL=5',
 'export OMVLL_CONFIG="'+posix(root/'scripts/wsl/omvll-config.py')+'"','export NMMP_TEST_SEED='+seed,
 'base="'+posix(base)+'"','harness="'+posix(w/'s5-harness')+'"']
# Main repository root is parent of nmm-protect.
for name,mode,private in [('B','legacy','ON'),('C','on-demand-v1','OFF')]:
 lines += ['export NMMP_VM_DECODE_MODE='+mode+' NMMP_PRIVATE_LINKER='+private+' NMMP_PRIVATE_STAGE0_VM=ON',
  'cd "$base/'+name+'"',
  'java -jar vm-protect.jar apk input/original.apk /mnt/d/AndroidProjects/sync-ui/app/convertRules.txt /mnt/d/AndroidProjects/sync-ui/app/build/outputs/mapping/release/mapping.txt > build.log 2>&1',
  'echo "seed'+str(seed_index)+' '+name+' APK compiled"',
  'java -cp "$harness/generator-classes:$base/hotspot-'+name+'/vm-protect.jar" GenerateBench "$harness/impl-dex/classes.dex" "$base/hotspot-'+name+'/build" > "$base/hotspot-'+name+'/build.log" 2>&1',
  'echo "seed'+str(seed_index)+' '+name+' hotspot compiled"']
for name,source,mode,private in [('A','B','legacy','OFF'),('D','C','on-demand-v1','ON')]:
 lines += ['export NMMP_VM_DECODE_MODE='+mode+' NMMP_PRIVATE_LINKER='+private+' NMMP_PRIVATE_STAGE0_VM=ON',
  'mkdir -p "$base/'+name+'/input/build" "$base/hotspot-'+name+'/build"',
  'cp -a "$base/'+source+'/input/build/dex2c" "$base/'+name+'/input/build/"',
  'cp -a "$base/hotspot-'+source+'/build/dex2c" "$base/hotspot-'+name+'/build/"',
  'java -cp "$harness/generator-classes:$base/'+name+'/vm-protect.jar" BuildRuntime "$base/'+name+'/input/build" > "$base/'+name+'/build.log" 2>&1',
  'echo "seed'+str(seed_index)+' '+name+' APK runtime compiled"',
  'java -cp "$harness/generator-classes:$base/hotspot-'+name+'/vm-protect.jar" BuildRuntime "$base/hotspot-'+name+'/build" > "$base/hotspot-'+name+'/build.log" 2>&1',
  'echo "seed'+str(seed_index)+' '+name+' hotspot compiled"']
# Correct repository-specific config root, not inferred JAR cache.
s=chr(10).join(lines)+chr(10);s=s.replace(posix(root/'scripts/wsl/omvll-config.py'),posix(w.parents[1]/'scripts/wsl/omvll-config.py'))
(w/('build-s5-seed'+str(seed_index)+'.sh')).write_bytes(s.encode())
(base/'seed.json').write_text(json.dumps({'testSeed':seed,'baselineCommit':'14bc959','methodCount':526,'apiTarget':26},indent=2))
print(base)
