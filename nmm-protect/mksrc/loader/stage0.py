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
        program_key = int.from_bytes(random_bytes(8)[:8], 'little')
        random_byte = lambda: random_bytes(1)[0]
        opcodes = []
        while len(opcodes) < fmt.OP_COUNT:
            stored = random_byte()
            if stored not in opcodes:
                opcodes.append(stored)
        opcodes = bytes(opcodes)
        share = int.from_bytes(random_bytes(8)[:8], 'little')
        registers = list(range(2, fmt.REGISTERS))
        for index in range(len(registers) - 1, 0, -1):
            other = random_byte() % (index + 1)
            registers[index], registers[other] = registers[other], registers[index]
        template = random_byte() % 3
        first_check = random_byte() & 1
        second_check = 1 - first_check
        order = [0, 1, 2, 3]
        for index in range(len(order) - 1, 0, -1):
            other = random_byte() % (index + 1)
            order[index], order[other] = order[other], order[index]
        success_size = (5, 7, 9)[template]
        sizes = (5, 5, success_size, 1)
        offsets, position = {}, 1
        for block in order:
            offsets[block] = position
            position += sizes[block]
        insns = [(fmt.JMP, dict(target=offsets[first_check]))]
        for block in order:
            if block in (0, 1):
                value_reg, expected_reg = registers[block * 2:block * 2 + 2]
                pair = [
                    (fmt.LOAD_INPUT, dict(dst=value_reg, a=block)),
                    (fmt.CONST, dict(dst=expected_reg, imm=(low, high)[block])),
                ]
                if random_byte() & 1:
                    pair.reverse()
                next_block = second_check if block == first_check else 2
                insns.extend(pair + [
                    (fmt.JULT, dict(a=value_reg, b=expected_reg, target=offsets[3])),
                    (fmt.JULT, dict(a=expected_reg, b=value_reg, target=offsets[3])),
                    (fmt.JMP, dict(target=offsets[next_block])),
                ])
            elif block == 2:
                left, right, temporary, carry = registers[4:8]
                insns.extend([
                    (fmt.CONST, dict(dst=left, imm=share)),
                    (fmt.CONST, dict(dst=right, imm=share ^ word)),
                ])
                if template == 0:
                    insns.append((fmt.XOR, dict(dst=fmt.VALUE_REGISTER, a=left, b=right)))
                elif template == 1:
                    insns.extend([
                        (fmt.OR, dict(dst=temporary, a=left, b=right)),
                        (fmt.AND, dict(dst=carry, a=left, b=right)),
                        (fmt.SUB, dict(dst=fmt.VALUE_REGISTER, a=temporary, b=carry)),
                    ])
                else:
                    insns.extend([
                        (fmt.ADD, dict(dst=temporary, a=left, b=right)),
                        (fmt.AND, dict(dst=carry, a=left, b=right)),
                        (fmt.CONST, dict(dst=left, imm=1)),
                        (fmt.SHL, dict(dst=carry, a=carry, b=left)),
                        (fmt.SUB, dict(dst=fmt.VALUE_REGISTER, a=temporary, b=carry)),
                    ])
                insns.extend([
                    (fmt.CONST, dict(dst=fmt.STATUS_REGISTER, imm=fmt.SUCCESS_STATUS)),
                    (fmt.RETURN, {}),
                ])
            else:
                insns.append((fmt.RETURN, {}))
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
