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

// The public state values remain stable. Only the in-memory decision word is
// encoded so no valid decision is all-zero and either 32-bit half validates
// the other before it is trusted.
#define NMMP_DECISION_VALUE_COOKIE UINT32_C(0x6d4d4d50)
#define NMMP_DECISION_CHECK_COOKIE UINT32_C(0xa3c59ac3)
#define NMMP_DECISION_PAYLOAD(state, reasons) \
    ((((uint32_t)(reasons)) << 2U) | (uint32_t)(state))
#define NMMP_ENCODED_DECISION(state, reasons) \
    (((uint64_t)((~NMMP_DECISION_PAYLOAD((state), (reasons))) \
                 ^ NMMP_DECISION_CHECK_COOKIE) << 32U) \
     | (uint64_t)(NMMP_DECISION_PAYLOAD((state), (reasons)) \
                  ^ NMMP_DECISION_VALUE_COOKIE))

static inline uint64_t nmmpProtectionEncodeDecision(NmmpProtectionState state,
                                                     uint32_t reasons) {
    return NMMP_ENCODED_DECISION(state, reasons);
}

static inline bool nmmpProtectionDecodeDecision(uint64_t encoded,
                                                 NmmpProtectionState *state,
                                                 uint32_t *reasons) {
    const uint32_t payload = (uint32_t)encoded ^ NMMP_DECISION_VALUE_COOKIE;
    const uint32_t inverse = (uint32_t)(encoded >> 32U) ^ NMMP_DECISION_CHECK_COOKIE;
    const uint32_t decodedState = payload & 3U;
    const uint32_t decodedReasons = payload >> 2U;
    if (inverse != ~payload
            || (decodedReasons & ~NMMP_REASON_ALL)
            || (decodedState == NMMP_PROTECTION_CLEAN && decodedReasons)
            || (decodedState == NMMP_PROTECTION_SUSPICIOUS && !decodedReasons)
            || (decodedState == NMMP_PROTECTION_INTEGRITY_FAILURE
                && !(decodedReasons & NMMP_REASON_INTEGRITY))) return false;
    *state = (NmmpProtectionState)decodedState;
    *reasons = decodedReasons;
    return true;
}

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
    // ENFORCE is fail-closed for every confirmed suspicious signal. UNKNOWN
    // remains distinct so an unavailable check is not treated as a detection.
    result.allowed = !(policyFlags & NMMP_POLICY_ENFORCE)
                     || result.state != NMMP_PROTECTION_SUSPICIOUS;
    return result;
}

static inline bool nmmpProtectionAllowState(uint32_t policyFlags,
                                            NmmpProtectionState state,
                                            uint32_t reasons) {
    (void)reasons;
    if (state == NMMP_PROTECTION_INTEGRITY_FAILURE) return false;
    return !(policyFlags & NMMP_POLICY_ENFORCE)
           || state != NMMP_PROTECTION_SUSPICIOUS;
}

#endif
