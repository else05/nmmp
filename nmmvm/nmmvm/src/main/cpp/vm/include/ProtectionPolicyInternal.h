#ifndef NMMP_PROTECTION_POLICY_INTERNAL_H
#define NMMP_PROTECTION_POLICY_INTERNAL_H

#include "ProtectionPolicyTypes.h"
#include <stdbool.h>

#define NMMP_CHECK_TRACER UINT32_C(1)
#define NMMP_CHECK_JAVA_DEBUG UINT32_C(2)
#define NMMP_CHECK_APP_DEBUG UINT32_C(4)
#define NMMP_CHECK_MAPS UINT32_C(8)
#define NMMP_CHECK_THREADS UINT32_C(16)
#define NMMP_CHECK_PORT_PRIMARY UINT32_C(32)
#define NMMP_CHECK_PORT_SECONDARY UINT32_C(64)
#define NMMP_CHECK_FRAMEWORK_CLASSES UINT32_C(128)
#define NMMP_CHECK_PACKAGE_CREATOR UINT32_C(256)
#define NMMP_CHECK_MODULE_ORIGINS UINT32_C(512)
#define NMMP_CHECK_ENVIRONMENT_ALL (NMMP_CHECK_THREADS | NMMP_CHECK_PORT_PRIMARY | NMMP_CHECK_PORT_SECONDARY | NMMP_CHECK_FRAMEWORK_CLASSES | NMMP_CHECK_PACKAGE_CREATOR)
#define NMMP_CHECK_DEBUG_ALL (NMMP_CHECK_TRACER | NMMP_CHECK_JAVA_DEBUG | NMMP_CHECK_APP_DEBUG)

typedef struct {
    uint32_t valid;
    uint32_t signals;
    uint32_t notApplicable;
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
    const uint32_t required = ((policyFlags & NMMP_POLICY_CHECK_DEBUG) ? NMMP_CHECK_DEBUG_ALL : 0)
                              | ((policyFlags & NMMP_POLICY_CHECK_MAPS) ? (NMMP_CHECK_MAPS | NMMP_CHECK_MODULE_ORIGINS) : 0)
                              | ((policyFlags & NMMP_POLICY_CHECK_ENVIRONMENT) ? NMMP_CHECK_ENVIRONMENT_ALL : 0);
    const uint32_t applicable = required & ~evidence.notApplicable;
    const uint32_t signals = evidence.signals & evidence.valid & applicable;
    if (signals & NMMP_CHECK_DEBUG_ALL) result.reasons |= NMMP_REASON_DEBUG;
    if (signals & NMMP_CHECK_MAPS) result.reasons |= NMMP_REASON_INJECTION;
    if (signals & (NMMP_CHECK_ENVIRONMENT_ALL | NMMP_CHECK_MODULE_ORIGINS)) result.reasons |= NMMP_REASON_ENVIRONMENT;
    result.state = result.reasons ? NMMP_PROTECTION_SUSPICIOUS
                                  : (applicable && (evidence.valid & applicable) == applicable
                                             ? NMMP_PROTECTION_CLEAN : NMMP_PROTECTION_UNKNOWN);
    // Policy v5: enforce restricts explicit debugger evidence. Environment
    // clues alone only request integrity verification; they do not deny calls.
    result.allowed = !(policyFlags & NMMP_POLICY_ENFORCE) || !(result.reasons & NMMP_REASON_DEBUG);
    return result;
}

static inline bool nmmpProtectionAllowState(uint32_t policyFlags,
                                            NmmpProtectionState state,
                                            uint32_t reasons) {
    if (state == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    return !(policyFlags & NMMP_POLICY_ENFORCE)
           || state != NMMP_PROTECTION_SUSPICIOUS
           || !(reasons & NMMP_REASON_DEBUG);
}

#endif
