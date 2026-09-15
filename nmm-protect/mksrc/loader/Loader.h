#ifndef NMMP_PRIVATE_LOADER_H
#define NMMP_PRIVATE_LOADER_H
#include <stddef.h>
#include <stdint.h>
#include "PrivateImageLayout.h"

typedef struct NmmpModule NmmpModule;
/* decoded is private writable scratch; moved metadata is restored there first. */
int nmmp_map_image(uint8_t *decoded, size_t size, NmmpModule **out);
int nmmp_run_constructors(NmmpModule *module);
void *nmmp_bootstrap_address(const NmmpModule *module);
uintptr_t nmmp_image_bias(const NmmpModule *module);
const void *nmmp_image_start(const NmmpModule *module);
size_t nmmp_image_size(const NmmpModule *module);
const NmmpImageSegment *nmmp_image_segments(const NmmpModule *module, size_t *count);
const NmmpImportSlot *nmmp_import_slots(const NmmpModule *module, size_t *count);
/* Only valid before constructors; afterwards the module is process-resident. */
void nmmp_discard_image(NmmpModule *module);
#endif
