#include "ProtectionPolicyInternal.h"
#include <stdlib.h>

static void check(int condition) { if (!condition) abort(); }

int main(void) {
    NmmpProtectionState decodedState;
    uint32_t decodedReasons;
    const uint64_t encodedClean = nmmpProtectionEncodeDecision(NMMP_PROTECTION_CLEAN, 0);
    check(encodedClean != 0);
    check(nmmpProtectionDecodeDecision(encodedClean, &decodedState, &decodedReasons));
    check(decodedState == NMMP_PROTECTION_CLEAN && decodedReasons == 0);
    check(!nmmpProtectionDecodeDecision(0, &decodedState, &decodedReasons));
    check(!nmmpProtectionDecodeDecision(encodedClean ^ (UINT64_C(1) << 32U),
                                        &decodedState, &decodedReasons));
    check(!nmmpProtectionDecodeDecision(
            nmmpProtectionEncodeDecision(NMMP_PROTECTION_CLEAN, NMMP_REASON_DEBUG),
            &decodedState, &decodedReasons));

    const uint32_t observe = NMMP_POLICY_CHECK_DEBUG | NMMP_POLICY_CHECK_MAPS;
    const uint32_t enforce = observe | NMMP_POLICY_ENFORCE;
    NmmpProtectionEvidence evidence = {0, 0, 0};
    NmmpProtectionDecision decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && decision.allowed);

    evidence.valid = NMMP_CHECK_TRACER;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && decision.allowed);
    evidence.valid = NMMP_CHECK_DEBUG_ALL | NMMP_CHECK_MAPS | NMMP_CHECK_MODULE_ORIGINS;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_CLEAN && decision.allowed);

    evidence.signals = NMMP_CHECK_TRACER;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS
          && decision.reasons == NMMP_REASON_DEBUG && !decision.allowed);

    evidence.signals |= NMMP_CHECK_MAPS;
    decision = nmmpProtectionClassify(observe, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && decision.allowed);
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && !decision.allowed);

    evidence.valid = NMMP_CHECK_MAPS;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.reasons == NMMP_REASON_INJECTION && !decision.allowed);

    evidence.signals = NMMP_CHECK_TRACER; /* Invalid sources cannot assert a signal. */
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && decision.reasons == 0);
    evidence.notApplicable = NMMP_CHECK_DEBUG_ALL | NMMP_CHECK_MODULE_ORIGINS;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_CLEAN);

    decision = nmmpProtectionClassify(observe, true, evidence);
    check(decision.state == NMMP_PROTECTION_INTEGRITY_FAILURE
          && decision.reasons == NMMP_REASON_INTEGRITY && !decision.allowed);
    evidence = (NmmpProtectionEvidence){NMMP_CHECK_ENVIRONMENT_ALL, NMMP_CHECK_THREADS | NMMP_CHECK_PORT_PRIMARY, 0};
    decision = nmmpProtectionClassify(NMMP_POLICY_ENFORCE | NMMP_POLICY_CHECK_ENVIRONMENT, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && !decision.allowed && decision.reasons == NMMP_REASON_ENVIRONMENT);
    check(!nmmpProtectionAllowState(NMMP_POLICY_ENFORCE, decision.state, decision.reasons));
    check(!nmmpProtectionAllowState(NMMP_POLICY_ENFORCE, NMMP_PROTECTION_SUSPICIOUS,
                                    NMMP_REASON_ART_ENTRY));
    return 0;
}
