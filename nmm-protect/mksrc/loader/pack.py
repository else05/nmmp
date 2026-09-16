#!/usr/bin/env python3
"""Strict build-time audit and split-container writer for NMMP-owned ELF files."""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib

LIMIT = 64 * 1024 * 1024
PUBLIC_LIBS = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libz.so'}
PH_TYPES = {0, 1, 2, 4, 6, 0x6474e550, 0x6474e551, 0x6474e552}
DT_TAGS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 20,
           21, 23, 24, 25, 26, 27, 28, 30, 0x6ffffff9, 0x6ffffffb,
           0x6ffffff0, 0x6ffffffe, 0x6fffffff}
RELOCATIONS = {0, 257, 1025, 1026, 1027}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def span(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, 'source range')
    return data[offset:offset + size]


def unpack(fmt, data, offset=0):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def disjoint(ranges, label):
    end = 0
    for start, size in sorted(ranges):
        require(start >= end and size >= 0, label + ' overlap')
        end = start + size


class Elf:
    def __init__(self, data):
        self.data = data
        h = unpack('<16sHHIQQQIHHHHHH', data)
        require(h[0][:7] == b'\x7fELF\x02\x01\x01', 'ELF64 LE identity')
        require(h[1:4] == (3, 183, 1) and h[4] == 0 and h[7] == 0, 'ELF type/machine/flags/entry')
        require(h[8] == 64 and h[9] == 56 and 0 < h[10] <= 32, 'ELF header sizes')
        self.phoff, self.phnum = h[5], h[10]
        self.phdrs = [unpack('<IIQQQQQQ', data, self.phoff + i * 56) for i in range(self.phnum)]
        self.loads = [p for p in self.phdrs if p[0] == 1]
        require(0 < len(self.loads) <= 16, 'LOAD count')
        for p in self.phdrs:
            require(p[0] in PH_TYPES, 'unsupported program header ' + hex(p[0]))
            require(p[5] <= p[6], 'filesz exceeds memsz')
            span(data, p[2], p[5])
            require(p[3] + p[6] <= LIMIT, 'virtual address limit')
            if p[0] == 1:
                require(p[6] and p[1] & ~7 == 0 and p[1] & 4, 'LOAD flags/size')
                require(p[1] & 3 != 3, 'W+X LOAD')
                require(p[7] >= 4096 and p[7] & (p[7] - 1) == 0, 'LOAD alignment')
                require(p[2] % p[7] == p[3] % p[7], 'LOAD congruence')
            if p[0] == 0x6474e551:
                require(p[1] & 1 == 0, 'executable stack')
        disjoint([(p[3], p[6]) for p in self.loads], 'LOAD memory')
        disjoint([(p[2], p[5]) for p in self.loads], 'LOAD file')
        relro = [p for p in self.phdrs if p[0] == 0x6474e552]
        require(len(relro) <= 1, 'multiple RELRO headers')
        for p in relro:
            require(p[6] > 0, 'empty RELRO')
            # Build-time minimum page audit; runtime repeats using its actual page size.
            for page in range(p[3] & ~4095, (p[3] + p[6] + 4095) & ~4095, 4096):
                flags = 0
                for load in self.loads:
                    if page < load[3] + load[6] and load[3] < page + 4096:
                        flags |= load[1]
                require(flags and not flags & 1, 'RELRO outside LOAD pages or executable')
        dyn = [p for p in self.phdrs if p[0] == 2]
        require(len(dyn) == 1 and dyn[0][5] % 16 == 0, 'DYNAMIC layout')
        self.dynamic = dyn[0]
        entries = [unpack('<qQ', data, dyn[0][2] + i) for i in range(0, dyn[0][5], 16)]
        require(any(t == 0 for t, _ in entries), 'missing DT_NULL')
        self.entries = entries[:next(i for i, (t, _) in enumerate(entries) if t == 0)]
        self.dt = {}
        for tag, value in self.entries:
            require(tag in DT_TAGS, 'unsupported dynamic tag ' + hex(tag))
            if tag != 1:
                require(tag not in self.dt, 'duplicate dynamic tag')
                self.dt[tag] = value
        require(self.dt.get(11) == 24 and self.dt.get(9, 24) == 24, 'symbol/rela entry size')
        require(self.dt.get(20, 7) == 7, 'non-RELA PLT')
        require(self.dt.get(30, 0) & ~10 == 0, 'unsupported DT_FLAGS')
        require(self.dt.get(0x6ffffffb, 0) & ~1 == 0, 'unsupported DT_FLAGS_1')
        self.strings = self.at(self.dt.get(5, LIMIT), self.dt.get(10, 0))
        require(self.strings and self.strings[0] == 0, 'dynstr')
        hash_addr = self.dt.get(4, LIMIT)
        nbucket, self.nsym = unpack('<II', self.at(hash_addr, 8))
        require(0 < nbucket <= 65536 and 0 < self.nsym <= 65536, 'SysV hash counts')
        hashes = unpack('<' + 'I' * (nbucket + self.nsym), self.at(hash_addr + 8, 4 * (nbucket + self.nsym)))
        require(all(v < self.nsym for v in hashes), 'hash index')
        self.symbols = [unpack('<IBBHQQ', self.at(self.dt[6] + i * 24, 24)) for i in range(self.nsym)]
        require(self.symbols[0] == (0, 0, 0, 0, 0, 0), 'null symbol')
        for name, info, other, shndx, value, size in self.symbols[1:]:
            self.string(name)
            require(info >> 4 in (0, 1, 2) and info & 15 in (0, 1, 2), 'TLS/IFUNC/symbol type or binding')
            require(other & ~3 == 0, 'symbol visibility')
            require(shndx < 0xff00, 'reserved/absolute symbol')
            if shndx:
                self.target(value, max(size, 1))
        self.needed = [self.string(v) for t, v in self.entries if t == 1]
        require(len(self.needed) == len(set(self.needed)) and set(self.needed) <= PUBLIC_LIBS, 'public dependencies')
        self.relocations = []
        for addr_tag, size_tag in [(7, 8), (23, 2)]:
            addr, size = self.dt.get(addr_tag, 0), self.dt.get(size_tag, 0)
            require(bool(addr) == bool(size) and size % 24 == 0 and size // 24 <= 1000000, 'RELA size')
            for i in range(0, size, 24):
                target, info, addend = unpack('<QQq', self.at(addr + i, 24))
                kind, sym = info & 0xffffffff, info >> 32
                require(kind in RELOCATIONS and sym < self.nsym, 'unsupported relocation or symbol index')
                if kind:
                    require(target % 8 == 0, 'unaligned relocation')
                    p = self.target(target, 8)
                    require(p[1] & 2 and not p[1] & 1, 'text/readonly relocation')
                    if kind == 1027:
                        require(sym == 0 and 0 <= addend < LIMIT, 'RELATIVE addend/symbol')
                        self.target(addend, 1)
                self.relocations.append((target, kind, sym, addend))
        disjoint([(t, 8) for t, k, _, _ in self.relocations if k], 'relocation target')
        for a, s in [(25, 27), (26, 28)]:
            require(bool(self.dt.get(a)) == bool(self.dt.get(s)), 'init/fini array pair')
            if self.dt.get(s):
                require(self.dt[a] % 8 == 0 and self.dt[s] % 8 == 0 and self.dt[s] <= 8192, 'init/fini array alignment/size')
                self.at(self.dt[a], self.dt[s])
        for t in (12, 13):
            if self.dt.get(t):
                require(self.target(self.dt[t], 4)[1] & 1, 'init/fini executable range')
        self.versions = self.read_versions()

    def read_versions(self):
        tags = [self.dt.get(t, 0) for t in (0x6ffffff0, 0x6ffffffe, 0x6fffffff)]
        if not any(tags):
            return {}
        vsym, vneed, count = tags
        require(all(tags) and count <= 5, 'version table triplet/count')
        indices = unpack('<' + 'H' * self.nsym, self.at(vsym, self.nsym * 2))
        versions = {}
        ranges = []
        for i in range(count):
            version, naux, library, aux, next_need = unpack('<HHIII', self.at(vneed, 16))
            require(version == 1 and 0 < naux <= 64 and aux >= 16 and aux % 4 == 0, 'verneed header')
            require(self.string(library) in self.needed, 'version provider')
            ranges.append((vneed, 16))
            at = vneed + aux
            for j in range(naux):
                value, flags, index, name, next_aux = unpack('<IHHII', self.at(at, 16))
                text = self.string(name)
                require(flags == 0 and 2 <= index < 256 and index not in versions, 'version flags/index')
                require(value == elf_hash(text), 'version name hash')
                versions[index] = (library, name)
                ranges.append((at, 16))
                require((j == naux - 1 and next_aux == 0) or
                        (j < naux - 1 and next_aux >= 16 and next_aux % 4 == 0), 'vernaux chain')
                at += next_aux
            require((i == count - 1 and next_need == 0) or
                    (i < count - 1 and next_need >= 16 and next_need % 4 == 0), 'verneed chain')
            vneed += next_need
        disjoint(ranges + [(vsym, self.nsym * 2)], 'version metadata')
        result = {}
        for i, (s, index) in enumerate(zip(self.symbols, indices)):
            require(index < 256, 'hidden/unknown symbol version')
            if i == 0:
                require(index == 0, 'null symbol version')
            elif s[3]:
                require(index == 1, 'defined symbol version')
            elif index > 1:
                require(index in versions, 'undefined version index')
                result[i] = versions[index]
            else:
                require(index == 1, 'local undefined version')
        return result

    def target(self, addr, size):
        for p in self.loads:
            if p[3] <= addr and size <= p[6] and addr - p[3] <= p[6] - size:
                return p
        raise ValueError('virtual target range')

    def at(self, addr, size):
        p = self.target(addr, max(1, size))
        offset = addr - p[3]
        require(size <= p[5] and offset <= p[5] - size, 'virtual file range')
        return span(self.data, p[2] + offset, size)

    def file_vaddr(self, offset, size):
        for p in self.loads:
            if p[2] <= offset and size <= p[5] and offset - p[2] <= p[5] - size:
                return p[3] + offset - p[2]
        raise ValueError('file metadata outside LOAD')

    def string(self, offset):
        require(0 <= offset < len(self.strings), 'string offset')
        end = self.strings.find(b'\0', offset)
        require(end != -1, 'unterminated string')
        return self.strings[offset:end].decode('ascii')


def elf_hash(text):
    value = 0
    for byte in text.encode('ascii'):
        value = (value << 4) + byte
        high = value & 0xf0000000
        value ^= high >> 24
        value &= ~high
    return value


def stub_exports(path, readelf):
    output = subprocess.run([readelf, '--dyn-syms', '--wide', str(path)],
                            capture_output=True, text=True, check=True).stdout
    result = collections.defaultdict(list)
    for line in output.splitlines():
        match = re.match(r'\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+(\w+)\s+(\w+)\s+(\w+)\s+(\S+)\s+(\S+)', line)
        if not match:
            continue
        kind, binding, visibility, section, name = match.groups()
        if section != 'UND' and binding in ('GLOBAL', 'WEAK'):
            result[name.split('@')[0]].append((name, kind, binding, visibility))
    require(result, 'no symbols from NDK stub ' + str(path))
    return result


def audit_native_features(path, readelf):
    output = subprocess.run([readelf, '--symbols', '--wide', str(path)],
                            capture_output=True, text=True, check=True).stdout
    require("'.symtab'" in output, 'unstripped inner symbol table required for C++ audit')
    forbidden = ('_Unwind_', '__gxx_personality', '__cxa_throw', '__cxa_rethrow',
                 '__cxa_allocate_exception', '__emutls_get_address', '__cxa_guard_',
                 '__tls_get_addr', '_ZTI', '_ZTS', 'pthread_key_create')
    for line in output.splitlines():
        match = re.match(r'\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+\w+\s+\w+\s+\w+\s+\S+\s+(\S+)', line)
        if match:
            require(not match[1].startswith(forbidden), 'native exception/RTTI/TLS dependency: ' + match[1])


def imports_for(elf, sysroot, readelf):
    """Determine one public provider; no global symbol search fallback."""
    providers = {}
    for tag, off in elf.entries:
        if tag != 1:
            continue
        name = elf.string(off)
        # Check both target APIs, not only whichever SDK configured the current build.
        providers[name] = (off, [stub_exports(Path(sysroot).parent / str(api) / name, readelf) for api in (26, 27)])
    imports = []
    for index, sym in enumerate(elf.symbols[1:], 1):
        if sym[3]:
            continue
        name = elf.string(sym[0])
        matches = [(library, off) for library, (off, apis) in providers.items() if all(name in names for names in apis)]
        require(len(matches) == 1, 'missing or ambiguous public provider: ' + name)
        library, provider = matches[0]
        version = 0
        if index in elf.versions:
            requested_provider, version = elf.versions[index]
            require(elf.string(requested_provider) == library, 'version provider mismatch')
        for names in providers[library][1]:
            require(len(names[name]) == 1, 'multiple provider versions: ' + name)
            exported, kind, binding, visibility = names[name][0]
            require(kind == {0: 'NOTYPE', 1: 'OBJECT', 2: 'FUNC'}[sym[1] & 15] and visibility == 'DEFAULT', 'provider type/visibility')
            require(exported == name + '@@' + elf.string(version) if version else '@' not in exported or '@@' in exported,
                    'required non-default symbol version: ' + name)
        imports.append((index, provider, version, sym[1] >> 4, sym[1] & 15))
    return imports


def split(elf, imports, entry_name='runtime_inner_bootstrap_v1'):
    matches = [i for i, s in enumerate(elf.symbols) if s[3] and elf.string(s[0]) == entry_name]
    require(len(matches) == 1, 'bootstrap export')
    entry = matches[0]
    require(elf.symbols[entry][1] == 18 and elf.symbols[entry][2] == 0, 'bootstrap symbol ABI')
    blocks = [(1, elf.file_vaddr(elf.phoff, elf.phnum * 56), span(elf.data, elf.phoff, elf.phnum * 56)),
              (2, elf.dynamic[3], elf.at(elf.dynamic[3], elf.dynamic[5]))]
    for kind, addr_tag, size_tag in [(3, 7, 8), (4, 23, 2)]:
        if elf.dt.get(size_tag):
            blocks.append((kind, elf.dt[addr_tag], elf.at(elf.dt[addr_tag], elf.dt[size_tag])))
    disjoint([(addr, len(data)) for _, addr, data in blocks], 'metadata')
    seg_off = 64
    block_off = seg_off + len(elf.loads) * 56
    import_off = block_off + len(blocks) * 32
    cursor = import_off + len(imports) * 20
    block_table = bytearray()
    content = bytearray()
    for kind, addr, data in blocks:
        block_table += struct.pack('<IIQQQ', kind, 0, addr, len(data), cursor)
        content += data
        cursor += len(data)
    seg_table = bytearray()
    for p in elf.loads:
        original = span(elf.data, p[2], p[5])
        data = bytearray(original)
        owned = [(addr, block) for _, addr, block in blocks if p[3] <= addr < p[3] + p[5]]
        for addr, block in owned:
            require(addr - p[3] + len(block) <= len(data), 'metadata straddles LOAD')
            data[addr - p[3]:addr - p[3] + len(block)] = bytes(len(block))
        restored = bytearray(data)
        for addr, block in owned:
            restored[addr - p[3]:addr - p[3] + len(block)] = block
        require(restored == original, 'LOAD restoration mismatch')
        seg_table += struct.pack('<7Q', p[3], p[5], p[6], p[7], p[1], cursor, len(data))
        content += data
        cursor += len(data)
    header = struct.pack('<8sIHH6I3Q', b'PRVIMG01', 1, 183, 3, 0, len(elf.loads), len(blocks),
                         len(imports), elf.nsym, entry, seg_off, block_off, import_off)
    result = header + seg_table + block_table + b''.join(struct.pack('<5I', *i) for i in imports) + content
    require(len(result) == cursor and len(result) <= LIMIT, 'decoded limit')
    return bytes(result)


def c_array(name, data, qualifier='const'):
    rows = [','.join('0x%02x' % b for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return '%s unsigned char %s[%d] = {\n%s\n};\n' % (qualifier, name, len(data), ',\n'.join(rows))


def seal(content, build_id, java='java', key=None, nonce=None):
    key = os.urandom(32) if key is None else key
    nonce = os.urandom(12) if nonce is None else nonce
    require(len(key) == 32 and len(nonce) == 12 and len(build_id) == 16, 'crypto lengths')
    require(64 <= len(content) <= LIMIT, 'decoded size limit')
    compressed = zlib.compress(content, 9)
    require(len(compressed) <= LIMIT, 'compressed size limit')
    header = struct.pack('<8s6I16sQQ12sI', b'PRVPKG01', 1, 1, 183, 1, 1, 80,
                         build_id, len(compressed), len(content), nonce, 0)
    encrypted = subprocess.run([java, str(Path(__file__).with_name('Seal.java'))],
                               input=key + header + compressed, capture_output=True, check=True).stdout
    require(len(encrypted) == len(compressed) + 16, 'JCE output size')
    return header + encrypted, key


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('configure')
    init.add_argument('directory', type=Path)
    pack = commands.add_parser('pack')
    pack.add_argument('elf', type=Path)
    pack.add_argument('directory', type=Path)
    pack.add_argument('--sysroot', required=True)
    pack.add_argument('--java', default='java')
    pack.add_argument('--readelf', required=True)
    pack.add_argument('--stage0-vm', choices=('ON',), default='ON',
                      help='Stage0 VM is mandatory; OFF is no longer supported')
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    if args.command == 'configure':
        identity = args.directory / 'build-id.txt'
        if identity.exists():
            build_id = bytes.fromhex(identity.read_text().strip())
            require(len(build_id) == 16, 'existing build ID must contain 16 bytes')
        else:
            build_id = os.urandom(16)
            identity.write_text(build_id.hex() + '\n')
        header = args.directory / 'BuildId.h'
        source = c_array('nmmp_build_id', build_id, 'static const')
        if not header.exists() or header.read_text() != source:
            header.write_text(source)
        return
    elf = Elf(args.elf.read_bytes())
    audit_native_features(args.elf, args.readelf)
    imports = imports_for(elf, args.sysroot, args.readelf)
    content = split(elf, imports)
    build_id = bytes.fromhex((args.directory / 'build-id.txt').read_text().strip())
    payload, key = seal(content, build_id, args.java)
    source = '#include <stddef.h>\n' + c_array('nmmp_payload', payload)
    source += 'const size_t nmmp_payload_size = sizeof(nmmp_payload);\n'
    import stage0
    programs = stage0.generate(key, build_id)
    source += stage0.c_source(programs)
    (args.directory / 'Payload.c').write_text(source)
    report = {'format_version': 1, 'build_id': build_id.hex(), 'elf_sha256': hashlib.sha256(elf.data).hexdigest(),
              'stage0_vm': True,
              'stage0_program_hashes': ['%08x' % program['hash'] for program in programs],
              'decoded_sha256': hashlib.sha256(content).hexdigest(), 'payload_sha256': hashlib.sha256(payload).hexdigest(),
              'elf_bytes': len(elf.data), 'decoded_bytes': len(content), 'payload_bytes': len(payload),
              'loads': len(elf.loads), 'symbols': elf.nsym, 'needed': elf.needed,
              'native_exception_rtti_tls_symbol_audit': 'passed',
              'relocations': dict(collections.Counter(k for _, k, _, _ in elf.relocations)),
              'imports': [{'symbol': elf.string(elf.symbols[i][0]), 'provider': elf.string(d), 'weak': b == 2,
                           'version': elf.string(v) if v else None, 'default_on_api26_and_27_stubs': True,
                           'type': t} for i, d, v, b, t in imports]}
    (args.directory / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print('NMMP private payload: %d -> %d bytes' % (len(content), len(payload)))


if __name__ == '__main__':
    main()
