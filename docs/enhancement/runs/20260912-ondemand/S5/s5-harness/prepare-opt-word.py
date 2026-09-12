"""Prepare the isolated single-variable word-cache experiment, never a formal build."""
from pathlib import Path
import difflib
import hashlib
import json
import shutil

w = Path(__file__).resolve().parent.parent
source = w / 's5/seed1/hotspot-C/build/dex2c'
experiment = w / 's5-harness/opt-word-C'
output = experiment / 'build/dex2c'
relative = 'vm/include/VmReader.h'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p)
            for p in sorted(root.rglob('*')) if p.is_file()}


before = inventory(source)
if relative not in before or output.exists() or source in output.resolve().parents:
    raise RuntimeError('Missing source, existing output, or non-isolated destination')
original = (source / relative).read_bytes()
code = original.decode('utf-8')
newline = '\r\n' if '\r\n' in code else '\n'
code = code.replace('\r\n', '\n')


def replace_once(old, new):
    global code
    if code.count(old) != 1:
        raise RuntimeError('Unexpected reader source: ' + old)
    code = code.replace(old, new)


replace_once('    uint8_t decode(uint8_t byte, uint32_t pos, uint32_t domain) {\n'
             '        if (!boundaries_) return byte;\n',
             '    uint64_t keyBlock(uint32_t pos, uint32_t domain) {\n')
replace_once('        return byte ^ uint8_t(cache_[slot] >> ((pos & 7) * 8));\n',
             '        return cache_[slot];\n'
             '    }\n'
             '    uint8_t decode(uint8_t byte, uint32_t pos, uint32_t domain) {\n'
             '        if (!boundaries_) return byte;\n'
             '        return byte ^ uint8_t(keyBlock(pos, domain) >> ((pos & 7) * 8));\n')
replace_once('        uint16_t low = decode(code_[pos], uint32_t(pos), domain);\n'
             '        return low | (uint16_t(decode(code_[pos + 1], uint32_t(pos + 1), domain)) << 8);\n',
             '        // An even byte position and its successor share one 8-byte key block.\n'
             '        uint16_t key = boundaries_\n'
             '                ? uint16_t(keyBlock(uint32_t(pos), domain) >> ((pos & 7) * 8)) : 0;\n'
             '        uint16_t low = code_[pos] ^ uint8_t(key);\n'
             '        uint16_t high = code_[pos + 1] ^ uint8_t(key >> 8);\n'
             '        return low | (high << 8);\n')
shutil.copytree(source, output)
(output / relative).write_bytes(code.replace('\n', newline).encode('utf-8'))
after = inventory(output)
changed = [p for p in sorted(before.keys() | after.keys()) if before.get(p) != after.get(p)]
if changed != [relative] or inventory(source) != before:
    raise RuntimeError('Unexpected source/copy changes')
patch = ''.join(difflib.unified_diff(original.decode('utf-8').replace('\r\n', '\n').splitlines(True),
                                   code.splitlines(True), fromfile='a/' + relative, tofile='b/' + relative))
(experiment / 'opt-word.patch').write_text(patch, encoding='utf-8', newline='\n')
manifest = {'source': str(source), 'output': str(output), 'files': len(before),
            'changedFiles': changed, 'sourceSha256': before, 'outputSha256': after,
            'sourceUnchangedAfterCopy': True, 'patchSha256': digest(experiment / 'opt-word.patch')}
(experiment / 'source-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(experiment / 'EXPERIMENT_ONLY.txt').write_text(
    'Single-variable word key-block lookup experiment. No probe instrumentation.\n'
    'Not a formal performance result; no Android build or device execution by preparer.\n', encoding='utf-8')
print(json.dumps({'output': str(output), 'files': len(before), 'changedFiles': changed}))
