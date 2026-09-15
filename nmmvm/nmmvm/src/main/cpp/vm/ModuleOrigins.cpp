#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ModuleOrigins.h"
#include "PrivateLoaderState.h"
#include <link.h>
#include <cstring>
#include <cstdlib>

namespace {
bool prefix(const char *value, const char *start) {
    return !std::strncmp(value, start, std::strlen(start));
}
bool contains(const NmmpLoadedModule &module, uintptr_t address) {
    for (size_t i = 0; i < module.count; ++i)
        if (address >= module.loads[i].start && address < module.loads[i].end) return true;
    return false;
}
int collect(dl_phdr_info *info, size_t size, void *opaque) {
    auto *result = static_cast<NmmpLoadedModules *>(opaque);
    if (size < offsetof(dl_phdr_info, dlpi_phnum) + sizeof(info->dlpi_phnum)
            || !info->dlpi_name || info->dlpi_phnum > 128) {
        result->complete = false;
        return 1;
    }
    // API27 can enumerate a linker placeholder with no program headers.
    // It contributes no ranges; maps can still label the linker diagnostically.
    if (!info->dlpi_phnum) return 0;
    if (!info->dlpi_phdr || result->count == 256) { result->complete = false; return 1; }
    NmmpLoadedModule &module = result->modules[result->count];
    module.count = 0;
    module.bias = info->dlpi_addr;
    const size_t length = strnlen(info->dlpi_name, sizeof(module.name));
    if (length == sizeof(module.name)) { result->complete = false; return 1; }
    std::memcpy(module.name, info->dlpi_name, length + 1);
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
        if (module.count == 16 || ph.p_vaddr > UINTPTR_MAX - module.bias
                || ph.p_memsz > UINTPTR_MAX - (module.bias + ph.p_vaddr)) {
            result->complete = false;
            return 1;
        }
        module.loads[module.count++] = {module.bias + ph.p_vaddr, module.bias + ph.p_vaddr + ph.p_memsz};
    }
    if (module.count) ++result->count;
    return 0;
}
bool systemPath(const char *name) {
    return prefix(name, "/system/") || prefix(name, "/vendor/") || prefix(name, "/apex/");
}
bool oatPath(const char *name) {
    const size_t length = std::strlen(name);
    return (length >= 4 && !std::strcmp(name + length - 4, ".oat"))
            || (length >= 5 && !std::strcmp(name + length - 5, ".odex"));
}
}

void nmmpReadLoadedModules(NmmpLoadedModules *modules, uintptr_t outerAnchor) {
    if (!modules) return;
    modules->count = 0;
    modules->outerAnchor = outerAnchor;
    modules->complete = true;
    dl_iterate_phdr(collect, modules);
    if (!modules->count) modules->complete = false;
}

NmmpAddressInfo nmmpInspectAddress(const NmmpMapsSnapshot *maps,
        const NmmpLoadedModules *modules, const NmmpImageSegment *privateSegments,
        size_t privateCount, uintptr_t address) {
    NmmpAddressInfo result = {NMMP_OWNER_UNKNOWN, 0, false, false, false};
    const NmmpMapEntry *mapping = nmmpFindMapping(maps, address);
    if (!mapping) return result;
    result.permissions = mapping->permissions;
    const char *name = nmmpMappingName(maps, mapping);
    if (!name) return result;
    result.deleted = std::strstr(name, " (deleted)") != nullptr;
    result.memfd = prefix(name, "memfd:") || prefix(name, "/memfd:");
    result.temporaryPath = prefix(name, "/data/local/tmp/") || prefix(name, "/sdcard/")
            || prefix(name, "/storage/emulated/");
    if (!modules || !modules->complete || modules->count > 256 || privateCount > NMMP_PRIVATE_MAX_SEGMENTS
            || (privateCount && !privateSegments)) return result;
    for (size_t i = 0; i < privateCount; ++i) {
        const auto &segment = privateSegments[i];
        if (address >= segment.start && address - segment.start < segment.memory_size) {
            result.owner = NMMP_OWNER_PRIVATE;
            return result;
        }
    }
    const NmmpLoadedModule *owner = nullptr, *outer = nullptr;
    for (size_t i = 0; i < modules->count; ++i) {
        const auto &module = modules->modules[i];
        if (module.count > 16) return result;
        if (contains(module, address)) {
            if (owner) return result; // Conflicting module observations.
            owner = &module;
        }
        if (modules->outerAnchor && contains(module, modules->outerAnchor)) outer = &module;
    }
    if (owner) {
        // A module can unload between linker enumeration and maps capture.
        // A name disagreement is unknown, never a confident source label.
        const char *apkSeparator = std::strstr(owner->name, "!/");
        const bool same = !std::strcmp(owner->name, name)
                || (apkSeparator && std::strlen(name) == static_cast<size_t>(apkSeparator - owner->name)
                    && !std::memcmp(owner->name, name, apkSeparator - owner->name));
        if (!same || result.deleted) return result;
        if (owner == outer) { result.owner = NMMP_OWNER_OUTER; return result; }
        // ART can expose an OAT/ODEX image through dl_iterate_phdr as well as
        // maps. Preserve its diagnostic type in either observation path.
        if (oatPath(owner->name)) { result.owner = NMMP_OWNER_OAT; return result; }
        const char *base = std::strrchr(owner->name, '/');
        base = base ? base + 1 : owner->name;
        if (systemPath(owner->name)) {
            result.owner = !std::strcmp(base, "libc.so") ? NMMP_OWNER_LIBC
                    : !std::strcmp(base, "libart.so") ? NMMP_OWNER_ART
                    : (!std::strcmp(base, "linker64") || !std::strcmp(base, "linker")) ? NMMP_OWNER_LINKER
                    : NMMP_OWNER_SYSTEM;
        } else if (outer && prefix(outer->name, "/data/app/")) {
            const char *end = std::strchr(outer->name + 10, '/');
            if (end && !std::strncmp(owner->name, outer->name, end - outer->name + 1)) result.owner = NMMP_OWNER_APP;
            else result.owner = NMMP_OWNER_OTHER;
        } else result.owner = NMMP_OWNER_OTHER;
        return result;
    }
    // ART/JIT labels are diagnostic names; they do not confer trust on code.
    if (!std::strcmp(name, "/system/bin/linker64") || !std::strcmp(name, "/system/bin/linker")) result.owner = NMMP_OWNER_LINKER;
    else if (!std::strcmp(name, "[anon:dalvik-jit-code-cache]")
            || !std::strcmp(name, "/memfd:jit-cache (deleted)")) result.owner = NMMP_OWNER_JIT;
    else {
        if (oatPath(name)) result.owner = NMMP_OWNER_OAT;
        else if (!*name || result.memfd || prefix(name, "[anon:")) result.owner = NMMP_OWNER_ANONYMOUS;
        else result.owner = NMMP_OWNER_OTHER;
    }
    return result;
}

NmmpCheckStatus nmmpCheckModuleOrigins(const NmmpMapsSnapshot *maps) {
    if (!maps || maps->status != NMMP_MAPS_COMPLETE) return NMMP_CHECK_UNKNOWN;
    auto *modules = static_cast<NmmpLoadedModules *>(std::malloc(sizeof(NmmpLoadedModules)));
    if (!modules) return NMMP_CHECK_UNKNOWN;
    uintptr_t anchor = 0;
    const NmmpImageSegment *segments = nullptr;
    size_t count = 0;
#if defined(NMMP_PRIVATE_LINKER)
    anchor = reinterpret_cast<uintptr_t>(nmmp_private_failure_state);
    segments = nmmp_private_segments;
    count = nmmp_private_segment_count;
#endif
    nmmpReadLoadedModules(modules, anchor);
    NmmpCheckStatus status = modules->complete ? NMMP_CHECK_PASS : NMMP_CHECK_UNKNOWN;
    if (modules->complete) for (size_t i = 0; i < maps->count; ++i) {
        const auto &entry = maps->entries[i];
        if (!(entry.permissions & NMMP_MAP_EXEC)) continue;
        const auto source = nmmpInspectAddress(maps, modules, segments, count, entry.start);
        if (source.owner == NMMP_OWNER_UNKNOWN && status == NMMP_CHECK_PASS) status = NMMP_CHECK_UNKNOWN;
        if (source.temporaryPath) { status = NMMP_CHECK_SIGNAL; break; }
        const char *name = nmmpMappingName(maps, &entry);
        if (source.owner == NMMP_OWNER_OTHER && name
                && (prefix(name, "/data/user/") || prefix(name, "/data/data/"))) {
            status = NMMP_CHECK_SIGNAL;
            break;
        }
    }
    std::free(modules);
    return status;
}
