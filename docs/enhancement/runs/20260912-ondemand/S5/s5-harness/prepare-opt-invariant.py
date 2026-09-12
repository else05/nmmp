"""Isolate per-call reader/capacity loads; never edit the source harness or runtime."""
from pathlib import Path
import difflib
import hashlib
import json
import re
import shutil

work = Path(__file__).resolve().parent
w = work.parent
sources = {'L': work / 'current-legacy/build/dex2c', 'C': w / 's5/seed1/hotspot-C/build/dex2c'}
relative = 'vm/InterpC-portable.cpp'


def inventory(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file()}


for name, source in sources.items():
    experiment = work / ('opt-invariant-' + name)
    output = experiment / 'build/dex2c'
    if experiment.exists() or source.resolve() in output.resolve().parents:
        raise RuntimeError('Existing or non-isolated experiment destination')
    before = inventory(source)
    original = (source / relative).read_bytes()
    text = original.decode('utf-8')
    newline = '\r\n' if '\r\n' in text else '\n'
    text = text.replace('\r\n', '\n')
    adapter_start = text.index('\njvalue vmInterpret(\n')
    entry = text.index('\njvalue vmInterpretReader(')
    macros, adapter, body = text[:adapter_start], text[adapter_start:entry], text[entry:]
    # All replacements must be boolean guards; actual reader pointers stay untouched.
    for section in [macros, body]:
        if re.search(r'code->reader(?!\s*(?:&&|\)))', section):
            raise RuntimeError('Unexpected non-boolean reader use')
    counts = {'readerBooleanUses': (macros + body).count('code->reader'),
              'capacityUses': (macros + body).count('code->registerCapacity')}
    if counts != {'readerBooleanUses': 28, 'capacityUses': 4}:
        raise RuntimeError('Unexpected invariant use sites: ' + repr(counts))
    def replace(section):
        return section.replace('code->reader', 'checkedRegisters').replace('code->registerCapacity', 'registerCapacity')
    macros, body = replace(macros), replace(body)
    signature = '                         const vmResolver *dvmResolver, VmReader *inputReader) {\n'
    if body.count(signature) != 1:
        raise RuntimeError('Unexpected vmInterpretReader signature')
    body = body.replace(signature, signature +
        '    const bool checkedRegisters = code->reader != nullptr;\n'
        '    const uint32_t registerCapacity = checkedRegisters ? code->registerCapacity : 0;\n')
    changed = macros + adapter + body
    if changed[changed.index('\njvalue vmInterpret(\n'):changed.index('\njvalue vmInterpretReader(')] != adapter:
        raise RuntimeError('Adapter changed')
    shutil.copytree(source, output)
    (output / relative).write_bytes(changed.replace('\n', newline).encode('utf-8'))
    after = inventory(output)
    differences = [p for p in sorted(before.keys() | after.keys()) if before.get(p) != after.get(p)]
    if differences != [relative] or inventory(source) != before:
        raise RuntimeError('Unexpected source/copy drift')
    patch = ''.join(difflib.unified_diff(text.splitlines(True), changed.splitlines(True),
                                        fromfile='a/' + relative, tofile='b/' + relative))
    (experiment / 'invariant.patch').write_text(patch, encoding='utf-8', newline='\n')
    manifest = {'source': str(source), 'output': str(output), 'files': len(before),
                'sourceSha256': before, 'outputSha256': after, 'changedFiles': differences,
                'replacedUses': counts, 'adapterUnchanged': True, 'readerHeaderUnchanged': True,
                'capacityReadGuardedByCheckedRegisters': True,
                'wordOptimizationApplied': False, 'sourceUnchanged': True,
                'patchSha256': hashlib.sha256(patch.encode('utf-8')).hexdigest()}
    (experiment / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (experiment / 'EXPERIMENT_ONLY.txt').write_text(
        'Isolated per-call invariant-load experiment; no word/probe changes.\n'
        'No performance or ART result is claimed by source preparation.\n', encoding='utf-8')
    print(name, len(before), 'files; changes:', differences, counts)
