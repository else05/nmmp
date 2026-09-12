#ifndef NMMP_VM_READER_H
#define NMMP_VM_READER_H

#include <stdint.h>
#include <stddef.h>
#include "VmReaderFormats.h"

// All positions are offsets, never pointers into executable instructions.
class VmReader {
public:
    VmReader(const uint8_t *code, uint32_t codeBytes,
             const uint8_t *tries = nullptr, uint32_t triesBytes = 0)
            : code_(code), codeBytes_(codeBytes), tries_(tries),
              triesBytes_(triesBytes), failed_(false) {}

    VmReader(const uint8_t *code, uint32_t codeBytes, const uint8_t *tries,
             uint32_t triesBytes, const uint8_t *boundaries, uint64_t seed,
             const uint8_t *row = nullptr)
            : VmReader(code, codeBytes, tries, triesBytes) {
        boundaries_ = boundaries; seed_ = seed; row_ = row;
    }
    VmReader(const VmReader &) = delete;
    VmReader &operator=(const VmReader &) = delete;
    ~VmReader() {
        volatile uint64_t *wipe = cache_;
        for (unsigned i = 0; i < 8; ++i) wipe[i] = 0;
        volatile uint64_t *seed = &seed_; *seed = 0;
    }

    bool failed() const { return failed_; }
    void fail() { failed_ = true; }
    uint32_t codeUnits() const { return codeBytes_ / 2; }
    uint32_t triesBytes() const { return triesBytes_; }
    bool range(uint64_t offset, uint64_t size, uint64_t length) {
        if (offset > length || size > length - offset) { fail(); return false; }
        return true;
    }
    bool target(int64_t pc) {
        if (pc < 0 || uint64_t(pc) >= codeUnits()
                || (boundaries_ && kind(uint32_t(pc)) != NMMP_READER_FETCH)) { fail(); return false; }
        return true;
    }
    uint16_t instruction(int64_t pc) {
        if (!target(pc)) return 0;
        uint16_t result = word(pc, NMMP_READER_FETCH);
        return row_ ? (result & 0xff00) | row_[result & 255] : result;
    }
    uint16_t operand(uint32_t pc, uint32_t delta) {
        if (boundaries_) {
            if (!delta || delta > 4 || uint64_t(pc) + delta >= codeUnits()) { fail(); return 0; }
            for (uint32_t i = 1; i <= delta; ++i) {
                unsigned domain = kind(pc + i);
                if (domain != NMMP_READER_OPERAND && domain != NMMP_READER_WIDE) { fail(); return 0; }
            }
            return word(uint64_t(pc) + delta, kind(pc + delta));
        }
        return word(uint64_t(pc) + delta, NMMP_READER_OPERAND);
    }
    uint16_t payload16(int64_t pc) {
        if (boundaries_ && (pc < 0 || uint64_t(pc) >= codeUnits()
                || kind(uint32_t(pc)) != NMMP_READER_PAYLOAD)) { fail(); return 0; }
        return word(pc, NMMP_READER_PAYLOAD);
    }
    uint32_t payload32(int64_t pc) {
        if (!payloadRange(pc, 4)) return 0;
        uint32_t low = payload16(pc);
        return low | (uint32_t(payload16(pc + 1)) << 16);
    }
    uint8_t payloadByte(uint64_t offset) {
        if (!code_ || !range(offset, 1, codeBytes_)) { fail(); return 0; }
        if (boundaries_ && kind(uint32_t(offset / 2)) != NMMP_READER_PAYLOAD) { fail(); return 0; }
        return decode(code_[offset], uint32_t(offset), NMMP_READER_PAYLOAD);
    }
    bool payloadRange(int64_t pc, uint64_t bytes) {
        if (pc < 0) { fail(); return false; }
        return range(uint64_t(pc) * 2, bytes, codeBytes_);
    }
    uint8_t tryByte(uint64_t offset) {
        if (!tries_ || !range(offset, 1, triesBytes_)) { fail(); return 0; }
        return decode(tries_[offset], uint32_t(offset), NMMP_READER_TRIES);
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
    unsigned kind(uint32_t pc) const { return (boundaries_[pc / 2] >> ((pc & 1) * 4)) & 15; }
    static uint64_t mix(uint64_t x) {
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        return x ^ (x >> 31);
    }
    uint8_t decode(uint8_t byte, uint32_t pos, uint32_t domain) {
        if (!boundaries_) return byte;
        uint32_t window = pos & ~uint32_t(63);
        if (cacheDomain_ != domain || cacheWindow_ != window) {
            cacheDomain_ = domain; cacheWindow_ = window;
            cacheValid_ = 0;
        }
        unsigned slot = (pos & 63) / 8;
        if (!(cacheValid_ & (1u << slot))) {
            uint64_t input = seed_ ^ (uint64_t(domain) * UINT64_C(0xd6e8feb86659fd93));
            cache_[slot] = mix(input ^ (pos / 8));
            cacheValid_ |= 1u << slot;
        }
        return byte ^ uint8_t(cache_[slot] >> ((pos & 7) * 8));
    }
    uint16_t word(int64_t pc, uint32_t domain) {
        if (pc < 0 || !code_ || !range(uint64_t(pc) * 2, 2, codeBytes_)) {
            fail(); return 0;
        }
        uint64_t pos = uint64_t(pc) * 2;
        uint16_t low = decode(code_[pos], uint32_t(pos), domain);
        return low | (uint16_t(decode(code_[pos + 1], uint32_t(pos + 1), domain)) << 8);
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
    const uint8_t *boundaries_ = nullptr;
    const uint8_t *row_ = nullptr;
    uint64_t seed_ = 0;
    uint64_t cache_[8] = {};
    uint32_t cacheWindow_ = 0;
    uint32_t cacheDomain_ = 0;
    uint8_t cacheValid_ = 0;
};

#endif
