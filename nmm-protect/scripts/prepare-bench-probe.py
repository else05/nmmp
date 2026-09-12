"""Instrument a COPY of generated sources. Never apply to a formal timing build."""
from pathlib import Path
import shutil
import sys

source, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
if output == source or source in output.parents:
    raise ValueError('Probe output must be outside the source build')
output.mkdir(parents=True, exist_ok=False)
shutil.copytree(source / 'dex2c', output / 'dex2c')
probe = Path(__file__).resolve().parents[1] / 'apkprotect/src/test/performance/probe_functions.c'
shutil.copy2(probe, output / 'dex2c/generated/probe_functions.c')
cmake = output / 'dex2c/CMakeLists.txt'
with cmake.open('a', encoding='utf-8') as stream:
    stream.write('\n# TEST PROBES ONLY; excluded from all formal performance/release artifacts.\n'
                 'target_link_options(c++_en PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc '
                 '-Wl,--wrap=realloc -Wl,--wrap=free)\n')
code = cmake.read_text(encoding='utf-8')
code = 'if (NMMP_PRIVATE_LINKER)\n    message(FATAL_ERROR "Probe builds require private linker OFF")\nendif ()\n' + code
cmake.write_text(code, encoding='utf-8')
reader = output / 'dex2c/vm/include/VmReader.h'
if reader.exists():
    code = reader.read_text(encoding='utf-8')
    hook = '        if (!(cacheValid_ & (1u << slot))) {'
    if code.count(hook) != 1:
        raise ValueError('Expected exactly one reader hook location')
    code = code.replace('// All positions',
                        'extern "C" void nmmpTestRead(uint32_t, uint32_t, int);\n\n// All positions')
    code = code.replace(hook, '        nmmpTestRead(domain, pos, !(cacheValid_ & (1u << slot)));\n' + hook)
    reader.write_text(code, encoding='utf-8')
(output / 'PROBES_ONLY.txt').write_text(
    'No timing acceptance from this build. Counter slots: 0 allocation calls, 1 requested bytes, '
    '2 maximum allocation, 3 non-null frees; 4..8 byte reads FETCH/OPERAND/WIDE/PAYLOAD/TRIES; '
    '9..13 keystream block misses; 14..18 maximum byte offsets; 19 reserved. '
    'Native allocations only; no claim about ART allocations. Legacy has no reader counters.\n', encoding='utf-8')
