#ifndef NMMP_VM_READER_H
#define NMMP_VM_READER_H

#include <stdint.h>
#include <stddef.h>

// All positions are offsets, never pointers into executable instructions.
class VmReader {
public:
    VmReader(const uint8_t *code, uint32_t codeBytes,
             const uint8_t *tries = nullptr, uint32_t triesBytes = 0)
            : code_(code), codeBytes_(codeBytes), tries_(tries),
              triesBytes_(triesBytes), failed_(false) {}

    bool failed() const { return failed_; }
    void fail() { failed_ = true; }
    uint32_t codeUnits() const { return codeBytes_ / 2; }
    uint32_t triesBytes() const { return triesBytes_; }
    bool range(uint64_t offset, uint64_t size, uint64_t length) {
        if (offset > length || size > length - offset) { fail(); return false; }
        return true;
    }
    bool target(int64_t pc) {
        if (pc < 0 || uint64_t(pc) >= codeUnits()) { fail(); return false; }
        return true;
    }
    uint16_t instruction(int64_t pc) { return word(pc); }
    uint16_t operand(uint32_t pc, uint32_t delta) { return word(uint64_t(pc) + delta); }
    uint16_t payload16(int64_t pc) { return word(pc); }
    uint32_t payload32(int64_t pc) {
        uint32_t low = payload16(pc);
        return low | (uint32_t(payload16(pc + 1)) << 16);
    }
    uint8_t payloadByte(uint64_t offset) {
        if (!code_ || !range(offset, 1, codeBytes_)) { fail(); return 0; }
        return code_[offset];
    }
    bool payloadRange(int64_t pc, uint64_t bytes) {
        if (pc < 0) { fail(); return false; }
        return range(uint64_t(pc) * 2, bytes, codeBytes_);
    }
    uint8_t tryByte(uint64_t offset) {
        if (!tries_ || !range(offset, 1, triesBytes_)) { fail(); return 0; }
        return tries_[offset];
    }
    uint16_t try16(uint64_t offset) {
        uint16_t low = tryByte(offset);
        return low | (uint16_t(tryByte(offset + 1)) << 8);
    }
    uint32_t try32(uint64_t offset) {
        uint32_t low = try16(offset);
        return low | (uint32_t(try16(offset + 2)) << 16);
    }
    uint32_t uleb(uint32_t &cursor) { return leb(cursor, false); }
    int32_t sleb(uint32_t &cursor) { return int32_t(leb(cursor, true)); }

private:
    uint16_t word(int64_t pc) {
        if (pc < 0 || !code_ || !range(uint64_t(pc) * 2, 2, codeBytes_)) {
            fail(); return 0;
        }
        uint64_t pos = uint64_t(pc) * 2;
        return uint16_t(code_[pos]) | (uint16_t(code_[pos + 1]) << 8);
    }
    uint32_t leb(uint32_t &cursor, bool isSigned) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 5; ++i) {
            uint8_t byte = tryByte(cursor);
            if (failed_) return 0;
            ++cursor;
            if (i == 4 && (isSigned ? ((byte & 0xf8) != 0 && (byte & 0xf8) != 0x78)
                                   : (byte & 0xf0) != 0)) { fail(); return 0; }
            value |= uint32_t(byte & 0x7f) << (7 * i);
            if (!(byte & 0x80)) {
                if (isSigned && i < 4 && (byte & 0x40)) value |= ~uint32_t(0) << (7 * (i + 1));
                return value;
            }
        }
        fail(); return 0;
    }
    const uint8_t *code_;
    uint32_t codeBytes_;
    const uint8_t *tries_;
    uint32_t triesBytes_;
    bool failed_;
};

#endif
