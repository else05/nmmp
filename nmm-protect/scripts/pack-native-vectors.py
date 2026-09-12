"""Convert Java-exported native VM vectors into the dependency-free C++ test input."""
import json
from pathlib import Path
import struct
import sys

source, target = map(Path, sys.argv[1:])
vectors = json.loads(source.read_text(encoding='utf-8'))['vectors']
out = bytearray(struct.pack('<I', len(vectors)))
for vector in vectors:
    name = vector['name'].encode('utf-8')
    code = bytes.fromhex(vector['codeHex'])
    ops = bytes.fromhex(vector['opcodesHex'])
    assert len(ops) == 14
    out += struct.pack('<I', len(name)) + name
    out += struct.pack('<I', len(code)) + code
    out += struct.pack('<Q', int(vector['keyHex'], 16)) + ops
    out += struct.pack('<II', int(vector['hashHex'], 16), len(vector['inputHex']))
    for value in vector['inputHex']:
        out += struct.pack('<Q', int(value, 16))
    out += struct.pack('<IQ', vector['success'], int(vector['outputHex'], 16))
target.write_bytes(out)
print(f'Packed {len(vectors)} vectors: {target}')
