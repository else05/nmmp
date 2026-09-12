from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[3]
code=[
 'README.md','docs/enhancement/ON_DEMAND_FORMAT.md','docs/enhancement/ON_DEMAND_WORK.md','docs/enhancement/S5_PLAN.md','docs/enhancement/STATUS.md',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/DemandModule.java',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/NativeProgram.java',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/ProtectionContext.java',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/GeneratorRandom.java',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/dex2c/converter/instructionrewriter/RandomInstructionRewriter.java',
 'nmm-protect/apkprotect/src/main/java/com/nmmedit/apkprotect/util/CmakeUtils.java',
 'nmm-protect/apkprotect/src/main/resources/vmsrc.zip',
 'nmm-protect/apkprotect/src/test/java/com/nmmedit/apkprotect/dex2c/GeneratorRandomTest.java',
 'nmm-protect/apkprotect/src/test/performance',
 'nmm-protect/scripts/measure-startup.py','nmm-protect/scripts/measure-hotspots.py',
 'nmm-protect/scripts/generate-bench-body.py','nmm-protect/scripts/prepare-bench-probe.py','nmm-protect/scripts/summarize-enhancement.py',
 'nmmvm/nmmvm/src/main/cpp/vm/InterpC-portable.cpp',
 'nmmvm/nmmvm/src/main/cpp/vm/include/VmDecodedState.h',
 'nmmvm/nmmvm/src/test/semantic/CMakeLists.txt',
 'nmmvm/nmmvm/src/test/semantic/DecodedWipeBridge.cpp',
 'nmmvm/nmmvm/src/test/semantic/DecodedWipeMain.java']
docs=['docs/enhancement/POST_REVIEW.md','docs/enhancement/USAGE.md','docs/enhancement/S5_RESULTS.md','docs/enhancement/private-linker/VALIDATION.md']
for args in [['add','-f','--']+code,['add','-f','--']+docs+['docs/enhancement/runs/20260912-ondemand/S5'],['diff','--cached','--check','--']+code+docs]:
 subprocess.run(['git']+args,cwd=root,check=True)
print('Source/doc changes staged and checked; raw evidence whitespace preserved')
