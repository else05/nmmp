#ifndef NMMP_PROTECTION_MARKER_H
#define NMMP_PROTECTION_MARKER_H

#include <cstddef>

bool nmmpProtectionMarkerPresent();
bool nmmpProtectionMarkerPersist();

#if defined(NMMP_PROTECTION_MARKER_TEST)
bool nmmpProtectionMarkerBuildPathsForTest(const char *root,
                                           const char *packageName,
                                           char *primary,
                                           size_t primaryCapacity,
                                           char *fallback,
                                           size_t fallbackCapacity);
bool nmmpProtectionMarkerPresentForTest(const char *root, const char *packageName);
bool nmmpProtectionMarkerPersistForTest(const char *root, const char *packageName);
#endif

#endif
