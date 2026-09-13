#include "ProtectionPolicyInternal.h"
#include <stdlib.h>

static void check(int condition) { if (!condition) abort(); }

int main(void) {
    const uint32_t observe = NMMP_POLICY_CHECK_DEBUG | NMMP_POLICY_CHECK_MAPS;
    const uint32_t enforce = observe | NMMP_POLICY_ENFORCE;
    NmmpProtectionEvidence evidence = {false, false, false, false};
    NmmpProtectionDecision decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_UNKNOWN && decision.allowed);

    evidence.debugValid = true;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_CLEAN && decision.allowed);

    evidence.debugSignal = true;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS
          && decision.reasons == NMMP_REASON_DEBUG && decision.allowed);

    evidence.mapsValid = true;
    evidence.injectionSignal = true;
    decision = nmmpProtectionClassify(observe, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && decision.allowed);
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.state == NMMP_PROTECTION_SUSPICIOUS && !decision.allowed);

    evidence.debugValid = false;
    decision = nmmpProtectionClassify(enforce, false, evidence);
    check(decision.reasons == NMMP_REASON_INJECTION && decision.allowed);

    decision = nmmpProtectionClassify(observe, true, evidence);
    check(decision.state == NMMP_PROTECTION_INTEGRITY_FAILURE
          && decision.reasons == NMMP_REASON_INTEGRITY && !decision.allowed);
    return 0;
}
