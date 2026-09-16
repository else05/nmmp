#include "ProtectionPolicyInternal.h"
#include <stdlib.h>

static void check(int condition) { if (!condition) abort(); }
static bool allowed(uint32_t flags, NmmpProtectionDecision decision) {
    return nmmpProtectionAllowState(flags, decision.state);
}

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
    NmmpProtectionDecision decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && allowed(enforce, decision));

    evidence.valid = NMMP_CHECK_TRACER;
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && allowed(enforce, decision));
    evidence.valid = NMMP_CHECK_DEBUG_ALL | NMMP_CHECK_MAPS | NMMP_CHECK_MODULE_ORIGINS;
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_CLEAN && allowed(enforce, decision));

    evidence.signals = NMMP_CHECK_TRACER;
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS
          && decision.reasons == NMMP_REASON_DEBUG && !allowed(enforce, decision));

    evidence.signals |= NMMP_CHECK_MAPS;
    decision = nmmpProtectionClassify(observe, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && allowed(observe, decision));
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && !allowed(enforce, decision));

    evidence.valid = NMMP_CHECK_MAPS;
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.reasons == NMMP_REASON_INJECTION && !allowed(enforce, decision));

    evidence.signals = NMMP_CHECK_TRACER; /* Invalid sources cannot assert a signal. */
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && decision.reasons == 0);
    evidence.notApplicable = NMMP_CHECK_DEBUG_ALL | NMMP_CHECK_MODULE_ORIGINS;
    decision = nmmpProtectionClassify(enforce, evidence);
    check(decision.state == NMMP_PROTECTION_CLEAN);

    check(!nmmpProtectionAllowState(observe, NMMP_PROTECTION_INTEGRITY_FAILURE));
    evidence = (NmmpProtectionEvidence){NMMP_CHECK_ENVIRONMENT_ALL, NMMP_CHECK_THREADS | NMMP_CHECK_PORT_PRIMARY, 0};
    decision = nmmpProtectionClassify(NMMP_POLICY_ENFORCE | NMMP_POLICY_CHECK_ENVIRONMENT, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && !allowed(NMMP_POLICY_ENFORCE, decision)
          && decision.reasons == NMMP_REASON_ENVIRONMENT);
    check(!nmmpProtectionAllowState(NMMP_POLICY_ENFORCE, decision.state));
    check(!nmmpProtectionAllowState(NMMP_POLICY_ENFORCE, NMMP_PROTECTION_SUSPICIOUS));
    return 0;
}
