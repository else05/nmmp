#ifndef NMMP_APK_V2_SIGNER_H
#define NMMP_APK_V2_SIGNER_H

#include <stddef.h>
#include <stdint.h>

struct NmmpSignerCertificate {
    uint8_t *data;
    size_t size;
};

bool nmmpReadApkV2SignerCertificate(int fd,
                                    uint64_t fileSize,
                                    NmmpSignerCertificate *certificate);
void nmmpFreeSignerCertificate(NmmpSignerCertificate *certificate);

#endif
