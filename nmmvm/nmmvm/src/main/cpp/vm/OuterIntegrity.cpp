#include "OuterIntegrity.h"
#include "ArtifactApk.h"
#include "Sha256.h"
#include <elf.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdlib>

namespace {
struct Baseline {
    size_t count;
    NmmpImageSegment segments[NMMP_PRIVATE_MAX_SEGMENTS];
    size_t importCount;
    NmmpImportSlot imports[NMMP_PRIVATE_MAX_IMPORT_SLOTS];
};
static const Baseline *baseline;
bool range(uint64_t offset, uint64_t length, size_t size) {
    return offset <= size && length <= size - offset;
}
}

bool nmmpBuildOuterCodeBaseline(const uint8_t *image, size_t size,
        const NmmpLoadedModule *module, NmmpImageSegment *segments, size_t *count) {
    if (!count) return false;
    *count = 0;
    if (!image || size < sizeof(Elf64_Ehdr) || size > NMMP_PRIVATE_MAX_IMAGE_BYTES
            || !module || !module->count || module->count > 16 || !segments) return false;
    Elf64_Ehdr header;
    std::memcpy(&header, image, sizeof(header));
    if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) || header.e_ident[EI_CLASS] != ELFCLASS64
            || header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_ident[EI_VERSION] != EV_CURRENT
            || header.e_version != EV_CURRENT || header.e_machine != EM_AARCH64 || header.e_type != ET_DYN
            || header.e_ehsize != sizeof(header) || header.e_phentsize != sizeof(Elf64_Phdr)
            || !header.e_phnum || header.e_phnum > 128
            || !range(header.e_phoff, size_t(header.e_phnum) * sizeof(Elf64_Phdr), size)) return false;
    size_t loads = 0, executable = 0;
    for (size_t i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr ph;
        std::memcpy(&ph, image + header.e_phoff + i * sizeof(ph), sizeof(ph));
        if (ph.p_type == PT_DYNAMIC) {
            if (!range(ph.p_offset, ph.p_filesz, size) || ph.p_filesz % sizeof(Elf64_Dyn)) return false;
            bool ended = false;
            for (size_t j = 0; j < ph.p_filesz; j += sizeof(Elf64_Dyn)) {
                Elf64_Dyn entry;
                std::memcpy(&entry, image + ph.p_offset + j, sizeof(entry));
                if (entry.d_tag == DT_NULL) { ended = true; break; }
                if (entry.d_tag == DT_TEXTREL || (entry.d_tag == DT_FLAGS && (entry.d_un.d_val & DF_TEXTREL))) return false;
            }
            if (!ended) return false;
        }
        if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
        if (loads == module->count || ph.p_filesz > ph.p_memsz || (ph.p_flags & ~7u)
                || !range(ph.p_offset, ph.p_filesz, size) || ph.p_vaddr > UINTPTR_MAX - module->bias
                || ph.p_memsz > UINTPTR_MAX - (module->bias + ph.p_vaddr)
                || (ph.p_align > 1 && ((ph.p_align & (ph.p_align - 1))
                    || ph.p_offset % ph.p_align != ph.p_vaddr % ph.p_align))) return false;
        const uintptr_t start = module->bias + ph.p_vaddr;
        if (module->loads[loads].start != start || module->loads[loads].end != start + ph.p_memsz) return false;
        for (size_t previous = 0; previous < loads; ++previous)
            if (start < module->loads[previous].end && module->loads[previous].start < start + ph.p_memsz) return false;
        ++loads;
        if (!(ph.p_flags & PF_X)) continue;
        if ((ph.p_flags & PF_W) || !(ph.p_flags & PF_R) || !ph.p_filesz || executable == NMMP_PRIVATE_MAX_SEGMENTS) return false;
        NmmpImageSegment &segment = segments[executable++];
        segment = {start, static_cast<size_t>(ph.p_filesz), static_cast<size_t>(ph.p_memsz), ph.p_flags, 0, {0}};
        for (uint32_t shard = 0; shard < NMMP_EXECUTABLE_SHARD_COUNT; ++shard) {
            size_t shardOffset, shardLength;
            if (!nmmpExecutableShardRange(segment.file_size, shard, &shardOffset, &shardLength)) return false;
            nmmpSha256(image + ph.p_offset + shardOffset, shardLength, segment.executable_digests[shard]);
        }
    }
    if (loads != module->count || !executable) return false;
    *count = executable;
    return true;
}

bool nmmpPrepareOuterImageFromApk(int fd, const NmmpArtifactEntry *entries,
        size_t count, uintptr_t anchor) {
    if (fd < 0 || !entries || !count || count > NMMP_ARTIFACT_MAX_ENTRIES || !anchor
            || __atomic_load_n(&baseline, __ATOMIC_ACQUIRE)) return false;
    auto *modules = static_cast<NmmpLoadedModules *>(std::malloc(sizeof(NmmpLoadedModules)));
    if (!modules) return false;
    nmmpReadLoadedModules(modules, anchor);
    const NmmpLoadedModule *outer = nullptr;
    NmmpLoadedModule libcModule = {};
    if (modules->complete) for (size_t i = 0; i < modules->count; ++i) {
        const auto &candidate = modules->modules[i];
        if (!std::strcmp(candidate.name, "/system/lib64/libc.so")
                || !std::strcmp(candidate.name, "/apex/com.android.runtime/lib64/bionic/libc.so")) {
            if (libcModule.count) { std::free(modules); return false; }
            libcModule = candidate;
        }
        for (size_t j = 0; j < candidate.count; ++j) {
            if (anchor >= candidate.loads[j].start && anchor < candidate.loads[j].end) {
                if (outer) { std::free(modules); return false; }
                outer = &candidate;
            }
        }
    }
    if (!outer || !libcModule.count) { std::free(modules); return false; }
    const NmmpLoadedModule outerModule = *outer;
    std::free(modules);
    outer = &outerModule;
    const char *name = std::strrchr(outer->name, '/');
    name = name ? name + 1 : outer->name;
    const char prefix[] = "lib/arm64-v8a/";
    const NmmpArtifactEntry *entry = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].name_size == sizeof(prefix) - 1 + std::strlen(name)
                && !std::memcmp(entries[i].name, prefix, sizeof(prefix) - 1)
                && !std::memcmp(entries[i].name + sizeof(prefix) - 1, name, std::strlen(name))) {
            if (entry) return false;
            entry = entries + i;
        }
    }
    uint8_t *image = nullptr;
    uint8_t *libcImage = nullptr;
    size_t libcSize = 0;
    Baseline pending = {};
    const bool valid = entry && nmmpReadArtifactImage(fd, entry, &image)
            && nmmpBuildOuterCodeBaseline(image, static_cast<size_t>(entry->size), outer, pending.segments, &pending.count)
            && nmmpVerifyExecutableSegments(pending.segments, pending.count) == NMMP_NATIVE_MATCH
            && nmmpReadSystemLibc(&libcModule, &libcImage, &libcSize)
            && nmmpBuildOuterImportBaseline(image, static_cast<size_t>(entry->size), outer,
                    libcImage, libcSize, &libcModule, pending.imports, &pending.importCount)
            && (pending.importCount == 0 || nmmpVerifyImportSlots(pending.imports, pending.importCount) == NMMP_NATIVE_MATCH);
    std::free(image);
    std::free(libcImage);
    if (!valid) return false;
    void *memory = mmap(nullptr, sizeof(Baseline), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return false;
    std::memcpy(memory, &pending, sizeof(pending));
    if (mprotect(memory, sizeof(Baseline), PROT_READ)) { munmap(memory, sizeof(Baseline)); return false; }
    const Baseline *expected = nullptr;
    if (!__atomic_compare_exchange_n(&baseline, &expected, static_cast<const Baseline *>(memory), false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        munmap(memory, sizeof(Baseline));
        return false;
    }
    return true;
}

NmmpNativeIntegrityResult nmmpVerifyOuterImage() {
    const Baseline *current = __atomic_load_n(&baseline, __ATOMIC_ACQUIRE);
    if (!current) return NMMP_NATIVE_UNAVAILABLE;
    const auto imports = nmmpVerifyImportSlots(current->imports, current->importCount);
    if (imports == NMMP_NATIVE_UNAVAILABLE || imports == NMMP_NATIVE_MISMATCH) return imports;
    return nmmpVerifyExecutableSegments(current->segments, current->count);
}

NmmpNativeIntegrityResult nmmpVerifyOuterImageShard(uint32_t shard) {
    const Baseline *current = __atomic_load_n(&baseline, __ATOMIC_ACQUIRE);
    if (!current) return NMMP_NATIVE_UNAVAILABLE;
    const auto imports = nmmpVerifyImportSlots(current->imports, current->importCount);
    if (imports == NMMP_NATIVE_UNAVAILABLE || imports == NMMP_NATIVE_MISMATCH) return imports;
    return nmmpVerifyExecutableSegmentsShard(current->segments, current->count, shard);
}
