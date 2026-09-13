"""Stage0 semantics and pack integration. --fixture DIR exports native fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
import random
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_formats as fmt
import pack
import stage0
from test_pack import fixture

MASK = (1 << 64) - 1
FIXTURE_KEY = bytes(range(32))
FIXTURE_BUILD_ID = bytes(range(16))


class FixedRandom:
    def __init__(self):
        self.counter = 0

    def __call__(self, size):
        self.counter += 1
        return hashlib.sha256(struct.pack('<Q', self.counter)).digest()[:size]


def reference(program, inputs):
    """Independent scalar decoder/interpreter for the five emitted semantics."""
    code, table = program['code'], program['opcodes']
    if (not code or len(code) % fmt.INSTRUCTION_BYTES
            or len(code) > fmt.MAX_INSTRUCTIONS * fmt.INSTRUCTION_BYTES
            or len(inputs) > fmt.REGISTERS or len(set(table)) != fmt.OP_COUNT
            or stage0.checksum(code) != program['hash']):
        return False, fmt.FAILURE_VALUE
    plain = bytearray()
    for position, byte in enumerate(code):
        state = program['key'] ^ ((fmt.DOMAIN * 0xd6e8feb86659fd93) & MASK) ^ (position >> 3)
        state = ((state ^ (state >> 30)) * 0xbf58476d1ce4e5b9) & MASK
        state = ((state ^ (state >> 27)) * 0x94d049bb133111eb) & MASK
        state ^= state >> 31
        plain.append(byte ^ ((state >> (8 * (position % 8))) & 255))
    instructions = []
    for start in range(0, len(plain), fmt.INSTRUCTION_BYTES):
        data = plain[start:start + fmt.INSTRUCTION_BYTES]
        if data[fmt.OFFSET_OP] not in table:
            return False, 0
        op = table.index(data[fmt.OFFSET_OP])
        dst, a, b = (data[index] for index in (fmt.OFFSET_DST, fmt.OFFSET_A, fmt.OFFSET_B))
        imm = struct.unpack_from('<Q', data, fmt.OFFSET_IMM)[0]
        target = struct.unpack_from('<H', data, fmt.OFFSET_TARGET)[0]
        if any(data[fmt.OFFSET_RESERVED:]) or max(dst, a, b) >= fmt.REGISTERS:
            return False, 0
        for value, field in ((dst, fmt.USES_DST), (a, fmt.USES_A), (b, fmt.USES_B),
                             (imm, fmt.USES_IMM), (target, fmt.USES_TARGET)):
            if value and not (fmt.USED_FIELDS[op] & field):
                return False, 0
        if op == fmt.LOAD_INPUT and a >= len(inputs):
            return False, 0
        if op in (fmt.JULT, fmt.JMP) and target >= len(code) // fmt.INSTRUCTION_BYTES:
            return False, 0
        instructions.append((op, dst, a, b, imm, target))
    registers = [0] * fmt.REGISTERS
    pc = 0
    for _ in range(fmt.MAX_STEPS):
        if pc >= len(instructions):
            return False, 0
        op, dst, a, b, imm, target = instructions[pc]
        pc += 1
        if op == fmt.LOAD_INPUT:
            registers[dst] = inputs[a]
        elif op == fmt.CONST:
            registers[dst] = imm
        elif op == fmt.XOR:
            registers[dst] = registers[a] ^ registers[b]
        elif op == fmt.AND:
            registers[dst] = registers[a] & registers[b]
        elif op == fmt.OR:
            registers[dst] = registers[a] | registers[b]
        elif op == fmt.ADD:
            registers[dst] = (registers[a] + registers[b]) & MASK
        elif op == fmt.SUB:
            registers[dst] = (registers[a] - registers[b]) & MASK
        elif op == fmt.MUL:
            registers[dst] = (registers[a] * registers[b]) & MASK
        elif op == fmt.MOV:
            registers[dst] = registers[a]
        elif op in (fmt.SHL, fmt.SHR):
            if registers[b] > 63:
                return False, 0
            registers[dst] = ((registers[a] << registers[b]) & MASK
                              if op == fmt.SHL else registers[a] >> registers[b])
        elif op == fmt.JULT:
            if registers[a] < registers[b]:
                pc = target
        elif op == fmt.JMP:
            pc = target
        elif op == fmt.RETURN:
            success = registers[fmt.STATUS_REGISTER] == fmt.SUCCESS_STATUS
            return success, registers[fmt.VALUE_REGISTER] if success else fmt.FAILURE_VALUE
        else:
            raise AssertionError('unexpected stage0 semantic: %d' % op)
    return False, 0


class Stage0Test(unittest.TestCase):
    def test_four_words_and_both_build_halves(self):
        for key in (FIXTURE_KEY, bytes(32), bytes([255]) * 32):
            for build_id in (FIXTURE_BUILD_ID, bytes(16), bytes([255]) * 16,
                             bytes.fromhex('0123456789abcdeffedcba9876543210')):
                programs = stage0.generate(key, build_id, FixedRandom())
                inputs = list(struct.unpack('<2Q', build_id))
                recovered = bytearray()
                for program, expected in zip(programs, struct.unpack('<4Q', key)):
                    self.assertEqual(reference(program, inputs), (True, expected))
                    recovered.extend(struct.pack('<Q', reference(program, inputs)[1]))
                    for half in (0, 1):
                        for delta in (-1, 1):
                            wrong = inputs.copy()
                            wrong[half] = (wrong[half] + delta) & MASK
                            self.assertEqual(reference(program, wrong), (False, 0))
                    self.assertEqual(reference(program, inputs[:1]), (False, 0))
                self.assertEqual(bytes(recovered), key)

    def test_independent_random_material_and_encoding(self):
        first = stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID, FixedRandom())
        self.assertEqual(first, stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID, FixedRandom()))
        self.assertEqual(len({program['key'] for program in first}), 4)
        self.assertEqual(len({program['opcodes'] for program in first}), 4)
        for program in first:
            self.assertEqual(len(program['opcodes']), fmt.OP_COUNT)
            self.assertEqual(len(set(program['opcodes'])), fmt.OP_COUNT)
            self.assertIn(len(program['code']) // fmt.INSTRUCTION_BYTES, (17, 19, 21))
            self.assertEqual(stage0.transform(stage0.transform(program['code'], program['key']),
                                             program['key']), program['code'])
        # Exercise the production RNG path too, with no global PRNG seed.
        self.assertNotEqual(stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID),
                            stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID))

    def test_plain_program_structure_varies_across_seeds(self):
        structures = set()
        for seed in range(20):
            source = random.Random(seed)
            random_bytes = lambda size, source=source: bytes(source.randrange(256) for _ in range(size))
            program = stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID, random_bytes)[0]
            plain = stage0.transform(program['code'], program['key'])
            instructions = []
            for start in range(0, len(plain), fmt.INSTRUCTION_BYTES):
                data = plain[start:start + fmt.INSTRUCTION_BYTES]
                instructions.append((program['opcodes'].index(data[fmt.OFFSET_OP]),
                                     data[fmt.OFFSET_DST], data[fmt.OFFSET_A], data[fmt.OFFSET_B],
                                     struct.unpack_from('<Q', data, fmt.OFFSET_IMM)[0],
                                     struct.unpack_from('<H', data, fmt.OFFSET_TARGET)[0]))
            structures.add(tuple(instructions))
            self.assertEqual(reference(program, struct.unpack('<2Q', FIXTURE_BUILD_ID)),
                             (True, struct.unpack('<Q', FIXTURE_KEY[:8])[0]))
        self.assertGreaterEqual(len(structures), 12)

    def test_stored_zero_and_corruption(self):
        class ByteSequence:
            counter = 255

            def __call__(self, size):
                if size != 1:
                    return bytes(size)
                self.counter = (self.counter + 1) & 255
                return bytes([self.counter])
        programs = stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID, ByteSequence())
        self.assertIn(0, programs[0]['opcodes'])
        inputs = struct.unpack('<2Q', FIXTURE_BUILD_ID)
        for program, expected in zip(programs, struct.unpack('<4Q', FIXTURE_KEY)):
            self.assertEqual(reference(program, inputs), (True, expected))
            for offset in range(len(program['code'])):
                changed = dict(program)
                code = bytearray(program['code'])
                code[offset] ^= 1
                changed['code'] = bytes(code)
                self.assertEqual(reference(changed, inputs), (False, 0))

    def test_lengths(self):
        for key, build_id in ((b'', FIXTURE_BUILD_ID), (bytes(31), FIXTURE_BUILD_ID),
                              (bytes(33), FIXTURE_BUILD_ID), (FIXTURE_KEY, bytes(15)),
                              (FIXTURE_KEY, bytes(17))):
            with self.assertRaises(ValueError):
                stage0.generate(key, build_id)

    def test_pack_always_uses_stage0(self):
        for option in (None, 'ON'):
            with self.subTest(option=option):
                written = {}
                argv = ['pack.py', 'pack', 'inner.so', 'output', '--sysroot', 'unused', '--readelf', 'unused']
                if option:
                    argv.extend(['--stage0-vm', option])
                with mock.patch.object(sys, 'argv', argv), \
                        mock.patch.object(Path, 'mkdir'), \
                        mock.patch.object(Path, 'read_bytes', return_value=fixture()), \
                        mock.patch.object(Path, 'read_text', return_value=FIXTURE_BUILD_ID.hex()), \
                        mock.patch.object(Path, 'write_text', autospec=True,
                                          side_effect=lambda path, text: written.update({path.name: text})), \
                        mock.patch.object(pack, 'audit_native_features'), \
                        mock.patch.object(pack, 'imports_for', return_value=[]), \
                        mock.patch.object(pack, 'seal', return_value=(b'fixture-payload', FIXTURE_KEY)):
                    pack.main()
                source = written['Payload.c']
                report = json.loads(written['audit.json'])
                self.assertTrue(report['stage0_vm'])
                self.assertEqual(len(report['stage0_program_hashes']), 4)
                self.assertIn('nmmp_stage0_programs[4]', source)
                self.assertNotIn('nmmp_key_share_a', source)
                self.assertNotIn('nmmp_key_share_b', source)
                self.assertNotIn(FIXTURE_KEY.hex(), json.dumps(report))

    def test_pack_rejects_removed_key_shares(self):
        with mock.patch.object(sys, 'argv', ['pack.py', 'pack', 'inner.so', 'output',
                '--sysroot', 'unused', '--readelf', 'unused', '--stage0-vm', 'OFF']), \
                mock.patch.object(Path, 'mkdir') as mkdir:
            with self.assertRaises(SystemExit) as failure:
                pack.main()
            self.assertEqual(failure.exception.code, 2)
            mkdir.assert_not_called()

    def test_configure_preserves_identity_and_header(self):
        files = {}
        with mock.patch.object(sys, 'argv', ['pack.py', 'configure', 'output']), \
                mock.patch.object(Path, 'mkdir'), \
                mock.patch.object(Path, 'exists', autospec=True, side_effect=lambda path: path.name in files), \
                mock.patch.object(Path, 'read_text', autospec=True, side_effect=lambda path: files[path.name]), \
                mock.patch.object(Path, 'write_text', autospec=True,
                                  side_effect=lambda path, text: files.update({path.name: text})) as write, \
                mock.patch.object(pack.os, 'urandom', return_value=FIXTURE_BUILD_ID) as random_bytes:
            pack.main()
            first = dict(files)
            self.assertEqual(write.call_count, 2)
            pack.main()
            self.assertEqual(files, first)
            self.assertEqual(write.call_count, 2)
            random_bytes.assert_called_once_with(16)
            del files['BuildId.h']
            pack.main()
            self.assertEqual(files, first)
            self.assertEqual(write.call_count, 3)
            random_bytes.assert_called_once_with(16)
            for invalid in ('zz', '01' * 15, '01' * 17):
                files['build-id.txt'] = invalid
                with self.assertRaises(ValueError):
                    pack.main()
            self.assertEqual(write.call_count, 3)


def export_fixture(directory):
    directory.mkdir(parents=True, exist_ok=True)
    programs = stage0.generate(FIXTURE_KEY, FIXTURE_BUILD_ID, FixedRandom())
    (directory / 'stage0-programs.c').write_text(stage0.c_source(programs), encoding='utf-8')
    inputs = list(struct.unpack('<2Q', FIXTURE_BUILD_ID))
    vectors = []
    for index, (program, word) in enumerate(zip(programs, struct.unpack('<4Q', FIXTURE_KEY))):
        cases = [('valid', inputs, True, word)]
        for half in (0, 1):
            for delta in (-1, 1):
                wrong = inputs.copy()
                wrong[half] = (wrong[half] + delta) & MASK
                cases.append(('bad-build-%d-%d' % (half, delta), wrong, False, 0))
        cases.append(('missing-input', inputs[:1], False, 0))
        for name, values, success, value in cases:
            if reference(program, values) != (success, value):
                raise AssertionError('fixture reference mismatch')
            vectors.append(dict(name='word-%d-%s' % (index, name), codeHex=program['code'].hex(),
                                keyHex='%016x' % program['key'], opcodesHex=program['opcodes'].hex(),
                                hashHex='%08x' % program['hash'], inputHex=['%016x' % x for x in values],
                                success=success, outputHex='%016x' % value))
    report = dict(format='nmmp-stage0-v1', keyHex=FIXTURE_KEY.hex(), buildIdHex=FIXTURE_BUILD_ID.hex(),
                  instructionBytes=fmt.INSTRUCTION_BYTES, domainHex='%08x' % fmt.DOMAIN, vectors=vectors)
    (directory / 'stage0-vectors.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Exported stage0-programs.c and %d vectors to %s' % (len(vectors), directory))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--fixture':
        result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Stage0Test))
        if not result.wasSuccessful():
            raise SystemExit(1)
        export_fixture(Path(sys.argv[2]))
    else:
        unittest.main()
