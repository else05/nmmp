#pragma once
#include <stddef.h>
#include <stdint.h>

enum class ArtIntegrityResult { NORMAL, SUSPICIOUS, MODIFIED, UNSUPPORTED };
struct ArtRuntimeReport {
    ArtIntegrityResult result;
    unsigned checked, modified, externalTargets;
};
// Recognizes a destination only when the code differs. A normal branch is not a hook.
bool nmmpArtRedirectTarget(const uint8_t original[32], const uint8_t runtime[32],
                           uintptr_t address, uintptr_t *target);
ArtRuntimeReport nmmpCheckArtRuntimeIntegrity();
