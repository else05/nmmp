import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location('pack', Path(__file__).resolve().parents[1] / 'pack.py')
pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack)


def fixture():
    data = bytearray(0x2400)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 0, ident, 3, 183, 1, 0, 64, 0, 0, 64, 56, 5, 0, 0, 0)
    headers = [(6, 4, 64, 64, 64, 280, 280, 8),
               (1, 5, 0, 0, 0, 0x1800, 0x1800, 4096),
               (1, 6, 0x2000, 0x3000, 0x3000, 0x400, 0x1000, 4096),
               (2, 6, 0x2100, 0x3100, 0x3100, 16 * 14, 16 * 14, 8),
               (0x6474e552, 4, 0x2000, 0x3000, 0x3000, 0x400, 0x1000, 1)]
    for i, h in enumerate(headers):
        struct.pack_into('<IIQQQQQQ', data, 64 + i * 56, *h)
    strings = b'\0runtime_inner_bootstrap_v1\0'
    data[0x380:0x380 + len(strings)] = strings
    struct.pack_into('<IBBHQQ', data, 0x300 + 24, 1, 18, 0, 1, 0x1000, 8)
    struct.pack_into('<5I', data, 0x400, 1, 2, 1, 0, 0)
    relocs = [(0x3000, 1027, 0x1000), (0x3008, (1 << 32) | 257, 0),
              (0x3010, (1 << 32) | 1025, 0), (0, 0, 0)]
    for i, r in enumerate(relocs):
        struct.pack_into('<QQq', data, 0x500 + i * 24, *r)
    struct.pack_into('<QQq', data, 0x600, 0x3018, (1 << 32) | 1026, 0)
    dynamic = [(4, 0x400), (5, 0x380), (10, len(strings)), (6, 0x300), (11, 24),
               (7, 0x500), (8, 96), (9, 24), (23, 0x600), (2, 24), (20, 7), (25, 0x3200), (27, 8), (0, 0)]
    for i, pair in enumerate(dynamic):
        struct.pack_into('<qQ', data, 0x2100 + i * 16, *pair)
    data[0x1000:0x1008] = bytes.fromhex('00008052c0035fd6')  # mov w0,#0; ret
    return bytes(data)


def version_fixture():
    data = bytearray(fixture())
    strings = b'\0runtime_inner_bootstrap_v1\0libc.so\0LIBC\0external\0'
    provider, version, external = strings.index(b'libc.so'), strings.index(b'LIBC'), strings.index(b'external')
    data[0x380:0x380 + len(strings)] = strings
    struct.pack_into('<IBBHQQ', data, 0x330, external, 18, 0, 0, 0, 0)
    struct.pack_into('<6I', data, 0x400, 1, 3, 1, 0, 0, 0)
    struct.pack_into('<Q', data, 0x2100 + 2 * 16 + 8, len(strings))
    struct.pack_into('<Q', data, 0x2100 + 11 * 16 + 8, 0x3300)
    extra = [(0x6ffffff0, 0x700), (0x6ffffffe, 0x720), (0x6fffffff, 1), (1, provider), (0, 0)]
    for i, entry in enumerate(extra, 13):
        struct.pack_into('<qQ', data, 0x2100 + i * 16, *entry)
    for offset in (32, 40):
        struct.pack_into('<Q', data, 64 + 3 * 56 + offset, 18 * 16)
    struct.pack_into('<3H', data, 0x700, 0, 1, 2)
    struct.pack_into('<HHIII', data, 0x720, 1, 1, provider, 16, 0)
    struct.pack_into('<IHHII', data, 0x730, pack.elf_hash('LIBC'), 0, 2, version, 0)
    return data, provider, version


class PackTest(unittest.TestCase):
    def test_split_restore(self):
        original = fixture()
        elf = pack.Elf(original)
        content = pack.split(elf, [])
        header = pack.unpack('<8sIHH6I3Q', content)
        self.assertEqual(header[:4], (b'PRVIMG01', 1, 183, 3))
        self.assertEqual(header[5:9], (2, 4, 0, 2))
        blocks = [pack.unpack('<IIQQQ', content, header[11] + i * 32) for i in range(4)]
        for i, p in enumerate(elf.loads):
            s = pack.unpack('<7Q', content, header[10] + i * 56)
            restored = bytearray(pack.span(content, s[5], s[6]))
            for _, reserved, addr, size, offset in blocks:
                self.assertEqual(reserved, 0)
                if s[0] <= addr < s[0] + s[1]:
                    self.assertEqual(restored[addr - s[0]:addr - s[0] + size], bytes(size))
                    restored[addr - s[0]:addr - s[0] + size] = pack.span(content, offset, size)
            self.assertEqual(restored, pack.span(original, p[2], p[5]))
        self.assertEqual({r[1] for r in elf.relocations}, pack.RELOCATIONS)

    def reject(self, offset, fmt, value, reason):
        data = bytearray(fixture())
        struct.pack_into(fmt, data, offset, value)
        with self.assertRaisesRegex(ValueError, reason):
            pack.Elf(bytes(data))

    def test_reject_unsupported_features(self):
        self.reject(18, '<H', 62, 'machine')
        self.reject(64, '<I', 7, 'program header')  # PT_TLS
        self.reject(0x2100, '<q', 0x6ffffffc, 'dynamic tag')  # VERDEF is unsupported
        for kind in [1024, 1028, 1032, 0xffffffff]:
            self.reject(0x500 + 8, '<Q', kind, 'relocation')
        self.reject(0x318 + 4, '<B', 26, 'IFUNC')
        self.reject(0x318 + 4, '<B', 22, 'TLS')

    def test_reject_bounds(self):
        self.reject(64 + 56 + 40, '<Q', 1, 'filesz')
        self.reject(64 + 56 + 4, '<I', 7, r'W\+X')
        self.reject(64 + 112 + 16, '<Q', 0, 'overlap')
        self.reject(0x500, '<Q', 0x1000, 'readonly')
        self.reject(0x500, '<Q', 0x80000000, 'target')
        self.reject(0x500 + 16, '<q', -1, 'addend')
        self.reject(0x400 + 4, '<I', 0xffffffff, 'hash counts')
        self.reject(0x400 + 8, '<I', 2, 'hash index')
        self.reject(0x318, '<I', 100, 'string offset')
        self.reject(64 + 4 * 56 + 16, '<Q', 0x10000, 'RELRO')
        self.reject(0x2100 + 13 * 16, '<q', 1, 'DT_NULL')
        self.reject(0x2100 + 6 * 16 + 8, '<Q', 0xffffffffffffffff, 'RELA size')
        for length in [0, 1, 63, 64, 300, 0x1800, 0x23ff]:
            with self.assertRaises(ValueError):
                pack.Elf(fixture()[:length])

    def test_reject_unaligned_init_array(self):
        self.reject(0x2100 + 11 * 16 + 8, '<Q', 0x3201, 'array alignment')

    def test_version_table_boundaries(self):
        data, provider, version = version_fixture()
        self.assertEqual(pack.Elf(bytes(data)).versions, {2: (provider, version)})
        for offset, fmt, value in [(0x704, '<H', 0x8002), (0x704, '<H', 3),
                                   (0x722, '<H', 0), (0x728, '<I', 0xffffffff),
                                   (0x72c, '<I', 16), (0x730, '<I', 0),
                                   (0x734, '<H', 1), (0x736, '<H', 0xffff),
                                   (0x738, '<I', 0xffffffff), (0x73c, '<I', 16)]:
            changed = bytearray(data)
            struct.pack_into(fmt, changed, offset, value)
            with self.assertRaises(ValueError, msg='version mutation at %d' % offset):
                pack.Elf(bytes(changed))


if __name__ == '__main__':
    unittest.main()
