#ifndef NMMP_PROTECTION_POLICY_INTERNAL_H
#define NMMP_PROTECTION_POLICY_INTERNAL_H

#include "ProtectionPolicyTypes.h"
#include <stdbool.h>

typedef struct {
    bool debugValid;
    bool debugSignal;
    bool mapsValid;
    bool injectionSignal;
} NmmpProtectionEvidence;

typedef struct {
    NmmpProtectionState state;
    uint32_t reasons;
    bool allowed;
} NmmpProtectionDecision;

static inline NmmpProtectionDecision nmmpProtectionClassify(
        uint32_t policyFlags,
        bool integrityFailure,
        NmmpProtectionEvidence evidence) {
    NmmpProtectionDecision result = {NMMP_PROTECTION_UNKNOWN, 0, true};
    if (integrityFailure) {
        result.state = NMMP_PROTECTION_INTEGRITY_FAILURE;
        result.reasons = NMMP_REASON_INTEGRITY;
        result.allowed = false;
        return result;
    }
    const bool debugValid = (policyFlags & NMMP_POLICY_CHECK_DEBUG) && evidence.debugValid;
    const bool mapsValid = (policyFlags & NMMP_POLICY_CHECK_MAPS) && evidence.mapsValid;
    if (debugValid && evidence.debugSignal) result.reasons |= NMMP_REASON_DEBUG;
    if (mapsValid && evidence.injectionSignal) result.reasons |= NMMP_REASON_INJECTION;
    result.state = result.reasons ? NMMP_PROTECTION_SUSPICIOUS
                                  : (debugValid || mapsValid ? NMMP_PROTECTION_CLEAN
                                                             : NMMP_PROTECTION_UNKNOWN);
    result.allowed = !(policyFlags & NMMP_POLICY_ENFORCE)
                     || (result.reasons & (NMMP_REASON_DEBUG | NMMP_REASON_INJECTION))
                        != (NMMP_REASON_DEBUG | NMMP_REASON_INJECTION);
    return result;
}

static inline bool nmmpProtectionAllowState(uint32_t policyFlags,
                                            NmmpProtectionState state,
                                            uint32_t reasons) {
    if (state == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    return !(policyFlags & NMMP_POLICY_ENFORCE)
           || state != NMMP_PROTECTION_SUSPICIOUS
           || (reasons & (NMMP_REASON_DEBUG | NMMP_REASON_INJECTION))
              != (NMMP_REASON_DEBUG | NMMP_REASON_INJECTION);
}

#endif
