#ifndef NMMP_ENVIRONMENT_CHECKS_H
#define NMMP_ENVIRONMENT_CHECKS_H
#include <stdint.h>

enum NmmpCheckStatus { NMMP_CHECK_PASS, NMMP_CHECK_SIGNAL, NMMP_CHECK_UNKNOWN, NMMP_CHECK_NOT_APPLICABLE };
NmmpCheckStatus nmmpCheckThreadNames();
// Runtime callers probe only the two configured default loopback ports.
NmmpCheckStatus nmmpProbeLoopbackPort(uint16_t port);
#endif
