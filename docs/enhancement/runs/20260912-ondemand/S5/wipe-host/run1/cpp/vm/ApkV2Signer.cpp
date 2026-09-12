#include "ApkV2Signer.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "Arm64Syscall.h"

namespace {

const uint32_t kEocdSignature = UINT32_C(0x06054b50);
const uint32_t kV2BlockId = UINT32_C(0x7109871a);
const size_t kEocdMinimumSize = 22;
const size_t kEocdMaximumSize = kEocdMinimumSize + UINT16_MAX;
const uint64_t kMaximumV2BlockSize = UINT64_C(16) * 1024U * 1024U;
const size_t kMaximumCertificateSize = 1024U * 1024U;
const size_t kMaximumAlgorithmCount = 32;
const uint8_t kSigningBlockMagic[16] = {
        'A', 'P', 'K', ' ', 'S', 'i', 'g', ' ', 'B', 'l', 'o', 'c', 'k', ' ', '4', '2'
};

static uint16_t readUint16(const uint8_t *data) {
    return static_cast<uint16_t>(
            static_cast<uint16_t>(data[0])
            | static_cast<uint16_t>(data[1]) << 8U);
}

static uint32_t readUint32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0])
           | static_cast<uint32_t>(data[1]) << 8U
           | static_cast<uint32_t>(data[2]) << 16U
           | static_cast<uint32_t>(data[3]) << 24U;
}

static uint64_t readUint64(const uint8_t *data) {
    return static_cast<uint64_t>(readUint32(data))
           | static_cast<uint64_t>(readUint32(data + 4)) << 32U;
}

static bool preadFully(int fd, void *buffer, size_t size, uint64_t offset) {
    uint8_t *output = static_cast<uint8_t *>(buffer);
    size_t completed = 0;
    while (completed < size) {
        const long count = nmmpRawPread64(
                fd, output + completed, size - completed, offset + completed);
        if (count == -EINTR) continue;
        if (count <= 0 || static_cast<size_t>(count) > size - completed) return false;
        completed += static_cast<size_t>(count);
    }
    return true;
}

static void secureFree(uint8_t *data, size_t size) {
    if (data == nullptr) return;
    volatile uint8_t *wipe = data;
    for (size_t i = 0; i < size; ++i) {
        wipe[i] = 0;
    }
    std::free(data);
}

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : data_(data), size_(size), offset_(0) {
    }

    bool readUint32Value(uint32_t *value) {
        if (remaining() < 4) return false;
        *value = readUint32(data_ + offset_);
        offset_ += 4;
        return true;
    }

    bool readLengthPrefixed(Reader *value) {
        uint32_t length;
        if (!readUint32Value(&length) || length > remaining()) return false;
        *value = Reader(data_ + offset_, length);
        offset_ += length;
        return true;
    }

    const uint8_t *data() const {
        return data_ + offset_;
    }

    size_t remaining() const {
        return size_ - offset_;
    }

    bool empty() const {
        return offset_ == size_;
    }

private:
    const uint8_t *data_;
    size_t size_;
    size_t offset_;
};

static bool findCentralDirectoryOffset(int fd,
                                       uint64_t fileSize,
                                       uint64_t *centralDirectoryOffset) {
    if (fileSize < kEocdMinimumSize) return false;
    const size_t tailSize = static_cast<size_t>(
            fileSize < kEocdMaximumSize ? fileSize : kEocdMaximumSize);
    uint8_t *tail = static_cast<uint8_t *>(std::malloc(tailSize));
    if (tail == nullptr) return false;
    if (!preadFully(fd, tail, tailSize, fileSize - tailSize)) {
        std::free(tail);
        return false;
    }

    size_t candidateCount = 0;
    uint64_t candidateOffset = 0;
    for (size_t position = tailSize - kEocdMinimumSize;; --position) {
        if (readUint32(tail + position) == kEocdSignature) {
            const uint16_t commentSize = readUint16(tail + position + 20);
            if (position + kEocdMinimumSize + commentSize == tailSize) {
                const uint16_t diskNumber = readUint16(tail + position + 4);
                const uint16_t centralDirectoryDisk = readUint16(tail + position + 6);
                const uint16_t diskEntries = readUint16(tail + position + 8);
                const uint16_t totalEntries = readUint16(tail + position + 10);
                const uint32_t directorySize = readUint32(tail + position + 12);
                const uint32_t directoryOffset = readUint32(tail + position + 16);
                const uint64_t absoluteEocdOffset = fileSize - tailSize + position;
                if (diskNumber == 0
                    && centralDirectoryDisk == 0
                    && diskEntries == totalEntries
                    && totalEntries != UINT16_MAX
                    && directorySize != UINT32_MAX
                    && directoryOffset != UINT32_MAX
                    && static_cast<uint64_t>(directoryOffset) + directorySize
                       == absoluteEocdOffset) {
                    candidateOffset = directoryOffset;
                    ++candidateCount;
                }
            }
        }
        if (position == 0) break;
    }
    std::free(tail);
    if (candidateCount != 1) return false;
    *centralDirectoryOffset = candidateOffset;
    return true;
}

static bool parseAlgorithmRecords(Reader records,
                                  uint32_t algorithms[kMaximumAlgorithmCount],
                                  size_t *algorithmCount) {
    size_t count = 0;
    while (!records.empty()) {
        Reader record(nullptr, 0);
        Reader value(nullptr, 0);
        uint32_t algorithm;
        if (!records.readLengthPrefixed(&record)
            || !record.readUint32Value(&algorithm)
            || !record.readLengthPrefixed(&value)
            || value.empty()
            || !record.empty()
            || count == kMaximumAlgorithmCount) {
            return false;
        }
        algorithms[count++] = algorithm;
    }
    if (count == 0) return false;
    *algorithmCount = count;
    return true;
}

static bool parseCertificates(Reader certificates,
                              const uint8_t **firstCertificate,
                              size_t *firstCertificateSize) {
    size_t count = 0;
    while (!certificates.empty()) {
        Reader certificate(nullptr, 0);
        if (!certificates.readLengthPrefixed(&certificate)
            || certificate.empty()
            || certificate.remaining() > kMaximumCertificateSize) {
            return false;
        }
        if (count == 0) {
            *firstCertificate = certificate.data();
            *firstCertificateSize = certificate.remaining();
        }
        ++count;
    }
    return count != 0;
}

static bool parseAttributes(Reader attributes) {
    while (!attributes.empty()) {
        Reader attribute(nullptr, 0);
        uint32_t id;
        if (!attributes.readLengthPrefixed(&attribute)
            || !attribute.readUint32Value(&id)) {
            return false;
        }
        (void) id;
    }
    return true;
}

static bool parseSigner(Reader signer,
                        const uint8_t **certificate,
                        size_t *certificateSize) {
    Reader signedData(nullptr, 0);
    Reader signatures(nullptr, 0);
    Reader publicKey(nullptr, 0);
    if (!signer.readLengthPrefixed(&signedData)
        || !signer.readLengthPrefixed(&signatures)
        || !signer.readLengthPrefixed(&publicKey)
        || publicKey.empty()
        || !signer.empty()) {
        return false;
    }

    Reader digests(nullptr, 0);
    Reader certificates(nullptr, 0);
    Reader attributes(nullptr, 0);
    if (!signedData.readLengthPrefixed(&digests)
        || !signedData.readLengthPrefixed(&certificates)
        || !signedData.readLengthPrefixed(&attributes)) {
        return false;
    }
    if (!signedData.empty()) {
        Reader reserved(nullptr, 0);
        if (!signedData.readLengthPrefixed(&reserved)
            || !reserved.empty()
            || !signedData.empty()) {
            return false;
        }
    }

    uint32_t digestAlgorithms[kMaximumAlgorithmCount];
    uint32_t signatureAlgorithms[kMaximumAlgorithmCount];
    size_t digestCount;
    size_t signatureCount;
    if (!parseAlgorithmRecords(digests, digestAlgorithms, &digestCount)
        || !parseAlgorithmRecords(signatures, signatureAlgorithms, &signatureCount)
        || digestCount != signatureCount) {
        return false;
    }
    for (size_t i = 0; i < digestCount; ++i) {
        if (digestAlgorithms[i] != signatureAlgorithms[i]) return false;
    }
    return parseCertificates(certificates, certificate, certificateSize)
           && parseAttributes(attributes);
}

static bool parseV2Block(const uint8_t *block,
                         size_t blockSize,
                         const uint8_t **certificate,
                         size_t *certificateSize) {
    Reader root(block, blockSize);
    Reader signers(nullptr, 0);
    Reader signer(nullptr, 0);
    if (!root.readLengthPrefixed(&signers)
        || !root.empty()
        || !signers.readLengthPrefixed(&signer)
        || !signers.empty()) {
        return false;
    }
    return parseSigner(signer, certificate, certificateSize);
}

static bool readV2Block(int fd,
                        uint64_t centralDirectoryOffset,
                        uint8_t **v2Block,
                        size_t *v2BlockSize) {
    if (centralDirectoryOffset < 32) return false;
    uint8_t footer[24];
    const uint64_t footerOffset = centralDirectoryOffset - sizeof(footer);
    if (!preadFully(fd, footer, sizeof(footer), footerOffset)
        || std::memcmp(footer + 8, kSigningBlockMagic, sizeof(kSigningBlockMagic)) != 0) {
        return false;
    }

    const uint64_t blockSize = readUint64(footer);
    if (blockSize < sizeof(footer)
        || blockSize > UINT64_MAX - 8
        || blockSize + 8 > centralDirectoryOffset) {
        return false;
    }
    const uint64_t blockOffset = centralDirectoryOffset - blockSize - 8;
    uint8_t leadingSize[8];
    if (!preadFully(fd, leadingSize, sizeof(leadingSize), blockOffset)
        || readUint64(leadingSize) != blockSize) {
        return false;
    }

    uint64_t position = blockOffset + 8;
    bool found = false;
    while (position < footerOffset) {
        if (footerOffset - position < 12) return false;
        uint8_t pairHeader[12];
        if (!preadFully(fd, pairHeader, sizeof(pairHeader), position)) return false;
        const uint64_t pairSize = readUint64(pairHeader);
        const uint32_t pairId = readUint32(pairHeader + 8);
        if (pairSize < 4 || pairSize > footerOffset - position - 8) return false;

        if (pairId == kV2BlockId) {
            if (found || pairSize - 4 > kMaximumV2BlockSize) return false;
            const size_t valueSize = static_cast<size_t>(pairSize - 4);
            uint8_t *value = static_cast<uint8_t *>(std::malloc(valueSize));
            if (value == nullptr
                || !preadFully(fd, value, valueSize, position + sizeof(pairHeader))) {
                secureFree(value, valueSize);
                return false;
            }
            *v2Block = value;
            *v2BlockSize = valueSize;
            found = true;
        }
        position += 8 + pairSize;
    }
    return found && position == footerOffset;
}

}  // namespace

bool nmmpReadApkV2SignerCertificate(int fd,
                                    uint64_t fileSize,
                                    NmmpSignerCertificate *certificate) {
    if (fd < 0 || certificate == nullptr) return false;
    certificate->data = nullptr;
    certificate->size = 0;

    uint64_t centralDirectoryOffset;
    uint8_t *v2Block = nullptr;
    size_t v2BlockSize = 0;
    if (!findCentralDirectoryOffset(fd, fileSize, &centralDirectoryOffset)) {
        return false;
    }
    if (!readV2Block(fd, centralDirectoryOffset, &v2Block, &v2BlockSize)) {
        secureFree(v2Block, v2BlockSize);
        return false;
    }

    const uint8_t *certificateData = nullptr;
    size_t certificateSize = 0;
    const bool valid = parseV2Block(
            v2Block, v2BlockSize, &certificateData, &certificateSize);
    if (valid) {
        certificate->data = static_cast<uint8_t *>(std::malloc(certificateSize));
        if (certificate->data != nullptr) {
            std::memcpy(certificate->data, certificateData, certificateSize);
            certificate->size = certificateSize;
        }
    }
    secureFree(v2Block, v2BlockSize);
    return valid && certificate->data != nullptr;
}

void nmmpFreeSignerCertificate(NmmpSignerCertificate *certificate) {
    if (certificate == nullptr) return;
    if (certificate->data != nullptr) {
        volatile uint8_t *data = certificate->data;
        for (size_t i = 0; i < certificate->size; ++i) {
            data[i] = 0;
        }
        std::free(certificate->data);
    }
    certificate->data = nullptr;
    certificate->size = 0;
}
