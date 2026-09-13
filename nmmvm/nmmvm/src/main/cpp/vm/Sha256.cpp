#include "Sha256.h"

#include <cstring>

namespace {

const uint32_t kRoundConstants[64] = {
        UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
        UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
        UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
        UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
        UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
        UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
        UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
        UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
        UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
        UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
        UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
        UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
        UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
        UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
        UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
        UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
        UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
        UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
        UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
        UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
        UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
        UINT32_C(0xc67178f2)
};

static uint32_t rotateRight(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

static uint32_t loadBigEndian(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24U)
           | (static_cast<uint32_t>(data[1]) << 16U)
           | (static_cast<uint32_t>(data[2]) << 8U)
           | static_cast<uint32_t>(data[3]);
}

static void transform(NmmpSha256Context *context, const uint8_t block[64]) {
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) {
        words[i] = loadBigEndian(block + i * 4U);
    }
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotateRight(words[i - 15], 7)
                            ^ rotateRight(words[i - 15], 18)
                            ^ (words[i - 15] >> 3U);
        const uint32_t s1 = rotateRight(words[i - 2], 17)
                            ^ rotateRight(words[i - 2], 19)
                            ^ (words[i - 2] >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];

    for (size_t i = 0; i < 64; ++i) {
        const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
        const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void finish(NmmpSha256Context *context, uint8_t digest[32]) {
    const uint64_t bitLength = context->totalSize * 8U;
    context->buffer[context->bufferSize++] = 0x80;
    if (context->bufferSize > 56) {
        std::memset(context->buffer + context->bufferSize, 0,
                    sizeof(context->buffer) - context->bufferSize);
        transform(context, context->buffer);
        context->bufferSize = 0;
    }
    std::memset(context->buffer + context->bufferSize, 0, 56 - context->bufferSize);
    for (size_t i = 0; i < 8; ++i) {
        context->buffer[63 - i] = static_cast<uint8_t>(bitLength >> (i * 8U));
    }
    transform(context, context->buffer);

    for (size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>(context->state[i] >> 24U);
        digest[i * 4 + 1] = static_cast<uint8_t>(context->state[i] >> 16U);
        digest[i * 4 + 2] = static_cast<uint8_t>(context->state[i] >> 8U);
        digest[i * 4 + 3] = static_cast<uint8_t>(context->state[i]);
    }
    std::memset(context, 0, sizeof(*context));
}

}  // namespace

void nmmpSha256Init(NmmpSha256Context *context) {
    const NmmpSha256Context initial = {
            {
                    UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
                    UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
                    UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
                    UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
            },
            0,
            {0},
            0
    };
    *context = initial;
}

void nmmpSha256Update(NmmpSha256Context *context, const uint8_t *data, size_t size) {
    if (size == 0) return;
    context->totalSize += size;
    while (size > 0) {
        const size_t available = sizeof(context->buffer) - context->bufferSize;
        const size_t count = size < available ? size : available;
        std::memcpy(context->buffer + context->bufferSize, data, count);
        context->bufferSize += count;
        data += count;
        size -= count;
        if (context->bufferSize == sizeof(context->buffer)) {
            transform(context, context->buffer);
            context->bufferSize = 0;
        }
    }
}

void nmmpSha256UpdateU32(NmmpSha256Context *context, uint32_t value) {
    uint8_t bytes[4] = {
            static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8U),
            static_cast<uint8_t>(value >> 16U), static_cast<uint8_t>(value >> 24U)
    };
    nmmpSha256Update(context, bytes, sizeof(bytes));
    std::memset(bytes, 0, sizeof(bytes));
}

void nmmpSha256Final(NmmpSha256Context *context, uint8_t digest[32]) {
    finish(context, digest);
}

void nmmpSha256(const uint8_t *data, size_t size, uint8_t digest[32]) {
    NmmpSha256Context context;
    nmmpSha256Init(&context);
    nmmpSha256Update(&context, data, size);
    nmmpSha256Final(&context, digest);
}
