#ifndef NMMP_STAGE0_H
#define NMMP_STAGE0_H
#include "NativeVm.h"
bool nmmpRecoverStage0(const NmmpNativeProgram programs[4], const uint8_t build_id[16], uint8_t key[32]);
#endif
