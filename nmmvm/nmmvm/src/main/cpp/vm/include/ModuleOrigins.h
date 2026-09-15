#ifndef NMMP_MODULE_ORIGINS_H
#define NMMP_MODULE_ORIGINS_H
#include "ProcessMaps.h"
#include "PrivateImageLayout.h"
#include "EnvironmentChecks.h"

enum NmmpAddressOwner {
    NMMP_OWNER_UNKNOWN, NMMP_OWNER_OUTER, NMMP_OWNER_PRIVATE,
    NMMP_OWNER_LIBC, NMMP_OWNER_ART, NMMP_OWNER_LINKER,
    NMMP_OWNER_SYSTEM, NMMP_OWNER_APP, NMMP_OWNER_OAT,
    NMMP_OWNER_JIT, NMMP_OWNER_ANONYMOUS, NMMP_OWNER_OTHER
};
struct NmmpLoadedModule {
    uintptr_t bias;
    size_t count;
    struct { uintptr_t start, end; } loads[16];
    char name[1024];
};
struct NmmpLoadedModules {
    bool complete;
    size_t count;
    uintptr_t outerAnchor;
    NmmpLoadedModule modules[256];
};
struct NmmpAddressInfo {
    NmmpAddressOwner owner;
    uint32_t permissions;
    bool deleted, memfd, temporaryPath;
};
// Labels describe a single observation, never authenticate a file or authorize
// dereferencing an address. The caller must discard both snapshots after use.
void nmmpReadLoadedModules(NmmpLoadedModules *modules, uintptr_t outerAnchor);
NmmpAddressInfo nmmpInspectAddress(const NmmpMapsSnapshot *maps,
        const NmmpLoadedModules *modules, const NmmpImageSegment *privateSegments,
        size_t privateCount, uintptr_t address);
NmmpCheckStatus nmmpCheckModuleOrigins(const NmmpMapsSnapshot *maps);
#endif
