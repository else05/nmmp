#include "NativeVm.h"
#include <stddef.h>

static void wipe(void *data, size_t size) {
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size--) *p++ = 0;
}
static uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
typedef struct { uint64_t imm; uint16_t target; uint8_t op, dst, a, b; } Instruction;
static bool fetch(const NmmpNativeProgram *p, uint32_t pc, uint32_t inputs, Instruction *i) {
    uint8_t bytes[NMMP_NATIVE_INSTRUCTION_BYTES];
    const uint32_t position = pc * NMMP_NATIVE_INSTRUCTION_BYTES;
    for (unsigned block = 0; block < 2; ++block) {
        uint64_t key = mix(p->key ^ (UINT64_C(0xd6e8feb86659fd93) * NMMP_NATIVE_DOMAIN)
                ^ ((position / 8) + block));
        for (unsigned b = 0; b < 8; ++b)
            bytes[block * 8 + b] = p->code[position + block * 8 + b] ^ (uint8_t)(key >> (b * 8));
        wipe(&key, sizeof(key));
    }
    i->op = NMMP_NATIVE_OP_COUNT;
    for (unsigned op = 0; op < NMMP_NATIVE_OP_COUNT; ++op)
        if (p->opcodes[op] == bytes[NMMP_NATIVE_OFFSET_OP]) i->op = op;
    i->dst = bytes[NMMP_NATIVE_OFFSET_DST]; i->a = bytes[NMMP_NATIVE_OFFSET_A]; i->b = bytes[NMMP_NATIVE_OFFSET_B]; i->imm = 0;
    for (unsigned b = 0; b < 8; ++b) i->imm |= (uint64_t)bytes[NMMP_NATIVE_OFFSET_IMM + b] << (b * 8);
    i->target = bytes[NMMP_NATIVE_OFFSET_TARGET] | (uint16_t)bytes[NMMP_NATIVE_OFFSET_TARGET + 1] << 8;
    bool valid = !bytes[NMMP_NATIVE_OFFSET_RESERVED] && !bytes[NMMP_NATIVE_OFFSET_RESERVED + 1] && i->dst < NMMP_NATIVE_REGISTERS
            && i->a < NMMP_NATIVE_REGISTERS && i->b < NMMP_NATIVE_REGISTERS;
    if (valid) switch (i->op) {
        case NMMP_NATIVE_LOAD_INPUT: valid = i->a < inputs && !i->b && !i->imm && !i->target; break;
        case NMMP_NATIVE_CONST: valid = !i->a && !i->b && !i->target; break;
        case NMMP_NATIVE_MOV: valid = !i->b && !i->imm && !i->target; break;
        case NMMP_NATIVE_XOR: case NMMP_NATIVE_AND: case NMMP_NATIVE_OR:
        case NMMP_NATIVE_ADD: case NMMP_NATIVE_SUB: case NMMP_NATIVE_MUL:
        case NMMP_NATIVE_SHL: case NMMP_NATIVE_SHR: valid = !i->imm && !i->target; break;
        case NMMP_NATIVE_JULT: valid = !i->dst && !i->imm && i->target < p->size / NMMP_NATIVE_INSTRUCTION_BYTES; break;
        case NMMP_NATIVE_JMP: valid = !i->dst && !i->a && !i->b && !i->imm
                && i->target < p->size / NMMP_NATIVE_INSTRUCTION_BYTES; break;
        case NMMP_NATIVE_RETURN: valid = !i->dst && !i->a && !i->b && !i->imm && !i->target; break;
        default: valid = false;
    }
    wipe(bytes, sizeof(bytes));
    return valid;
}

bool nmmpNativeRun(const NmmpNativeProgram *p, const uint64_t *inputs,
                   uint32_t inputCount, uint64_t *output) {
    if (!output) return false;
    *output = NMMP_NATIVE_FAILURE_VALUE;
    if (!p || !p->code || !p->size || p->size % NMMP_NATIVE_INSTRUCTION_BYTES
            || p->size / NMMP_NATIVE_INSTRUCTION_BYTES > NMMP_NATIVE_MAX_INSTRUCTIONS
            || inputCount > NMMP_NATIVE_REGISTERS || (inputCount && !inputs)) return false;
    for (unsigned i = 0; i < NMMP_NATIVE_OP_COUNT; ++i)
        for (unsigned j = 0; j < i; ++j) if (p->opcodes[i] == p->opcodes[j]) return false;
    uint32_t hash = UINT32_C(0x811c9dc5);
    for (uint32_t i = 0; i < p->size; ++i) { hash ^= p->code[i]; hash *= UINT32_C(0x01000193); }
    if (hash != p->hash) return false;
    uint64_t regs[NMMP_NATIVE_REGISTERS] = {0};
    Instruction instruction = {0};
    bool result = false;
    const uint32_t count = p->size / NMMP_NATIVE_INSTRUCTION_BYTES;
    for (uint32_t pc = 0; pc < count; ++pc)
        if (!fetch(p, pc, inputCount, &instruction)) goto cleanup;
    uint32_t pc = 0;
    for (unsigned step = 0; step < NMMP_NATIVE_MAX_STEPS; ++step) {
        if (pc >= count || !fetch(p, pc, inputCount, &instruction)) goto cleanup;
        ++pc;
        const uint8_t dst = instruction.dst, a = instruction.a, b = instruction.b;
        switch (instruction.op) {
            case NMMP_NATIVE_LOAD_INPUT: regs[dst] = inputs[a]; break;
            case NMMP_NATIVE_CONST: regs[dst] = instruction.imm; break;
            case NMMP_NATIVE_MOV: regs[dst] = regs[a]; break;
            case NMMP_NATIVE_XOR: regs[dst] = regs[a] ^ regs[b]; break;
            case NMMP_NATIVE_AND: regs[dst] = regs[a] & regs[b]; break;
            case NMMP_NATIVE_OR: regs[dst] = regs[a] | regs[b]; break;
            case NMMP_NATIVE_ADD: regs[dst] = regs[a] + regs[b]; break;
            case NMMP_NATIVE_SUB: regs[dst] = regs[a] - regs[b]; break;
            case NMMP_NATIVE_MUL: regs[dst] = regs[a] * regs[b]; break;
            case NMMP_NATIVE_SHL: case NMMP_NATIVE_SHR:
                if (regs[b] > 63) goto cleanup;
                regs[dst] = instruction.op == NMMP_NATIVE_SHL ? regs[a] << regs[b] : regs[a] >> regs[b]; break;
            case NMMP_NATIVE_JULT: if (regs[a] < regs[b]) pc = instruction.target; break;
            case NMMP_NATIVE_JMP: pc = instruction.target; break;
            case NMMP_NATIVE_RETURN:
                result = regs[NMMP_NATIVE_STATUS_REGISTER] == NMMP_NATIVE_SUCCESS_STATUS;
                if (result) *output = regs[NMMP_NATIVE_VALUE_REGISTER];
                goto cleanup;
            default: goto cleanup;
        }
    }
cleanup:
    wipe(regs, sizeof(regs)); wipe(&instruction, sizeof(instruction));
    return result;
}
