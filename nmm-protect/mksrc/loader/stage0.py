"""Generate loader-only native programs; no dependency on the inner VM root."""
import os
import struct

import native_formats as fmt

MASK64 = (1 << 64) - 1


def transform(data, key):
    """MethodCodec(key), id=0, absolute byte positions in the native VM domain."""
    result = bytearray(len(data))
    for offset in range(0, len(data), 8):
        value = (key ^ ((0xd6e8feb86659fd93 * fmt.DOMAIN) & MASK64) ^ (offset // 8))
        value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & MASK64
        value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & MASK64
        value ^= value >> 31
        for i in range(min(8, len(data) - offset)):
            result[offset + i] = data[offset + i] ^ ((value >> (i * 8)) & 255)
    return bytes(result)


def checksum(data):
    value = 0x811c9dc5
    for byte in data:
        value = ((value ^ byte) * 0x01000193) & 0xffffffff
    return value


def instruction(opcodes, op, dst=0, a=0, b=0, imm=0, target=0):
    data = bytearray(fmt.INSTRUCTION_BYTES)
    data[fmt.OFFSET_OP] = opcodes[op]
    data[fmt.OFFSET_DST] = dst
    data[fmt.OFFSET_A] = a
    data[fmt.OFFSET_B] = b
    struct.pack_into('<Q', data, fmt.OFFSET_IMM, imm)
    struct.pack_into('<H', data, fmt.OFFSET_TARGET, target)
    return bytes(data)


def generate(key, build_id, random_bytes=None):
    """Return four descriptors in LE key-word order. RNG injection is for tests."""
    if len(key) != 32 or len(build_id) != 16:
        raise ValueError('stage0 requires a 32-byte key and 16-byte build ID')
    random_bytes = os.urandom if random_bytes is None else random_bytes
    low, high = struct.unpack('<2Q', build_id)
    programs = []
    for word in struct.unpack('<4Q', key):
        program_key = int.from_bytes(random_bytes(8), 'little')
        opcodes = []
        while len(opcodes) < fmt.OP_COUNT:
            stored = random_bytes(1)[0]
            if stored not in opcodes:
                opcodes.append(stored)
        opcodes = bytes(opcodes)
        share = int.from_bytes(random_bytes(8), 'little')
        insns = []
        # Both unsigned comparisons enforce equality, including the high bit.
        for index, expected in enumerate((low, high)):
            insns.extend([
                (fmt.LOAD_INPUT, dict(dst=2, a=index)),
                (fmt.CONST, dict(dst=3, imm=expected)),
                (fmt.JULT, dict(a=2, b=3)),
                (fmt.JULT, dict(a=3, b=2)),
            ])
        insns.extend([
            (fmt.CONST, dict(dst=2, imm=share)),
            (fmt.CONST, dict(dst=3, imm=share ^ word)),
            (fmt.XOR, dict(dst=fmt.VALUE_REGISTER, a=2, b=3)),
            (fmt.CONST, dict(dst=fmt.STATUS_REGISTER, imm=fmt.SUCCESS_STATUS)),
            (fmt.RETURN, {}),
            (fmt.RETURN, {}),
        ])
        for op, fields in insns:
            if op == fmt.JULT:
                fields['target'] = len(insns) - 1
        plain = b''.join(instruction(opcodes, op, **fields) for op, fields in insns)
        code = transform(plain, program_key)
        programs.append(dict(code=code, key=program_key, opcodes=opcodes, hash=checksum(code)))
    return programs


def c_source(programs):
    if len(programs) != 4:
        raise ValueError('stage0 requires four programs')
    lines = ['#include "NativeVm.h"', '#ifdef __cplusplus', 'extern "C" {', '#endif']
    for index, program in enumerate(programs):
        lines.append('static const uint8_t nmmp_stage0_code_%d[] = {' % index)
        code = program['code']
        lines.extend('    ' + ','.join('0x%02x' % b for b in code[start:start + 16]) + ','
                     for start in range(0, len(code), 16))
        lines.append('};')
    # Explicit external linkage also allows the fixture to be compiled as C++.
    lines.append('extern const NmmpNativeProgram nmmp_stage0_programs[4];')
    lines.append('const NmmpNativeProgram nmmp_stage0_programs[4] = {')
    for index, program in enumerate(programs):
        lines.append('    {nmmp_stage0_code_%d, %d, UINT64_C(0x%016x), {%s}, UINT32_C(0x%08x)},' %
                     (index, len(program['code']), program['key'],
                      ','.join('0x%02x' % b for b in program['opcodes']), program['hash']))
    lines.append('};')
    lines.extend(['#ifdef __cplusplus', '}', '#endif'])
    return '\n'.join(lines) + '\n'
