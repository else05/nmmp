#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ArtRuntimeIntegrity.h"
#include "ProcessMaps.h"
#include "ProcessMemory.h"
#include "CheckLog.h"
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#if defined(__ANDROID__)
#include <android/api-level.h>
#endif

namespace {
static bool range(uint64_t offset, uint64_t count, size_t size) {
    return offset <= size && count <= size - offset;
}
static int64_t signedBits(uint32_t value, unsigned bits) {
    const int64_t sign = INT64_C(1) << (bits - 1);
    return (static_cast<int64_t>(value) ^ sign) - sign;
}
static bool addOffset(uintptr_t address, int64_t offset, uintptr_t *out) {
    if (offset >= 0) {
        if (static_cast<uint64_t>(offset) > UINTPTR_MAX - address) return false;
        *out = address + static_cast<uintptr_t>(offset);
    } else {
        const uint64_t magnitude = static_cast<uint64_t>(-offset);
        if (magnitude > address) return false;
        *out = address - magnitude;
    }
    return true;
}
static bool symbol(const uint8_t *file, size_t size, const Elf64_Ehdr &header,
                   const char *name, Elf64_Sym *out) {
    if (header.e_shentsize != sizeof(Elf64_Shdr) || !header.e_shnum
            || !range(header.e_shoff, uint64_t(header.e_shnum) * sizeof(Elf64_Shdr), size)) return false;
    for (unsigned i = 0; i < header.e_shnum; ++i) {
        Elf64_Shdr section;
        memcpy(&section, file + header.e_shoff + i * sizeof(section), sizeof(section));
        if (section.sh_type != SHT_DYNSYM && section.sh_type != SHT_SYMTAB) continue;
        if (section.sh_entsize != sizeof(Elf64_Sym) || section.sh_link >= header.e_shnum
                || section.sh_size % sizeof(Elf64_Sym) || !range(section.sh_offset, section.sh_size, size)) continue;
        Elf64_Shdr strings;
        memcpy(&strings, file + header.e_shoff + section.sh_link * sizeof(strings), sizeof(strings));
        if (strings.sh_type != SHT_STRTAB || !range(strings.sh_offset, strings.sh_size, size)) continue;
        for (uint64_t offset = 0; offset < section.sh_size; offset += sizeof(Elf64_Sym)) {
            Elf64_Sym entry;
            memcpy(&entry, file + section.sh_offset + offset, sizeof(entry));
            if (entry.st_shndx == SHN_UNDEF || entry.st_name >= strings.sh_size) continue;
            const char *text = reinterpret_cast<const char *>(file + strings.sh_offset + entry.st_name);
            if (!memchr(text, 0, strings.sh_size - entry.st_name)) continue;
            if (!strcmp(text, name) && entry.st_size >= 32) { *out = entry; return true; }
        }
    }
    return false;
}
}

bool nmmpArtRedirectTarget(const uint8_t original[32], const uint8_t runtime[32],
                           uintptr_t address, uintptr_t *target) {
    if (!original || !runtime || !target || !memcmp(original, runtime, 32)) return false;
    uint32_t code[8]; memcpy(code, runtime, sizeof(code));
    if ((code[0] & 0xfc000000U) == 0x14000000U)
        return addOffset(address, signedBits(code[0] & 0x03ffffffU, 26) * 4, target);
    // LDR Xt, literal; BR Xt. The literal must be inside the captured window.
    if ((code[0] & 0xff000000U) == 0x58000000U
            && (code[1] & 0xfffffc1fU) == 0xd61f0000U
            && (code[0] & 31U) == ((code[1] >> 5) & 31U)) {
        const int64_t offset = signedBits((code[0] >> 5) & 0x7ffffU, 19) * 4;
        if (offset >= 8 && offset <= 24) {
            uint64_t destination; memcpy(&destination, runtime + offset, sizeof(destination));
            if (destination > UINTPTR_MAX) return false;
            *target = static_cast<uintptr_t>(destination); return true;
        }
    }
    // ADRP Xn; ADD Xn, Xn, #imm; BR Xn.
    const unsigned reg = code[0] & 31U;
    if ((code[0] & 0x9f000000U) == 0x90000000U
            && (code[1] & 0xff800000U) == 0x91000000U
            && (code[1] & 31U) == reg && ((code[1] >> 5) & 31U) == reg
            && (code[2] & 0xfffffc1fU) == 0xd61f0000U && ((code[2] >> 5) & 31U) == reg) {
        const uint32_t immediate = ((code[0] >> 5) & 0x7ffffU) << 2 | ((code[0] >> 29) & 3U);
        const int64_t offset = signedBits(immediate, 21) * 4096
                + static_cast<int64_t>((code[1] >> 10) & 0xfffU) * ((code[1] & (1U << 22)) ? 4096 : 1);
        return addOffset(address & ~uintptr_t(4095), offset, target);
    }
    return false;
}

ArtRuntimeReport nmmpCheckArtRuntimeIntegrity() {
    ArtRuntimeReport report = {ArtIntegrityResult::UNSUPPORTED, 0, 0, 0};
#if !defined(__ANDROID__) || !defined(__aarch64__)
    return report;
#else
    if (android_get_device_api_level() != 27) return report;
    const char *path = "/system/lib64/libart.so";
    const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return report;
    struct stat statbuf;
    if (fstat(fd, &statbuf) || !S_ISREG(statbuf.st_mode) || statbuf.st_size < sizeof(Elf64_Ehdr)
            || statbuf.st_size > 64 * 1024 * 1024) { close(fd); return report; }
    const size_t size = static_cast<size_t>(statbuf.st_size);
    const auto *file = static_cast<const uint8_t *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (file == MAP_FAILED) return report;
    auto *maps = static_cast<NmmpMapsSnapshot *>(malloc(sizeof(NmmpMapsSnapshot)));
    if (!maps) { munmap(const_cast<uint8_t *>(file), size); return report; }
    nmmpReadProcessMaps(maps);
    Elf64_Ehdr header; memcpy(&header, file, sizeof(header));
    bool valid = maps->status == NMMP_MAPS_COMPLETE && !memcmp(header.e_ident, ELFMAG, SELFMAG)
            && header.e_ident[EI_CLASS] == ELFCLASS64 && header.e_ident[EI_DATA] == ELFDATA2LSB
            && header.e_machine == EM_AARCH64 && header.e_type == ET_DYN
            && header.e_phentsize == sizeof(Elf64_Phdr) && header.e_phnum && header.e_phnum <= 128
            && range(header.e_phoff, uint64_t(header.e_phnum) * sizeof(Elf64_Phdr), size);
    Elf64_Phdr loads[128]; unsigned count = 0;
    if (valid) for (unsigned i = 0; i < header.e_phnum; ++i) {
        Elf64_Phdr entry; memcpy(&entry, file + header.e_phoff + i * sizeof(entry), sizeof(entry));
        if (!range(entry.p_offset, entry.p_filesz, size)) { valid = false; break; }
        if (entry.p_type == PT_LOAD) loads[count++] = entry;
        if (entry.p_type == PT_DYNAMIC) {
            for (uint64_t off = 0; off + sizeof(Elf64_Dyn) <= entry.p_filesz; off += sizeof(Elf64_Dyn)) {
                Elf64_Dyn item; memcpy(&item, file + entry.p_offset + off, sizeof(item));
                if (item.d_tag == DT_NULL) break;
                if (item.d_tag == DT_TEXTREL || (item.d_tag == DT_FLAGS && (item.d_un.d_val & DF_TEXTREL))) valid = false;
            }
        }
    }
    static const char *names[] = {"art_quick_generic_jni_trampoline", "art_quick_to_interpreter_bridge",
                                  "art_quick_resolution_trampoline"};
    if (valid) for (const char *name : names) {
        Elf64_Sym entry;
        if (!symbol(file, size, header, name, &entry)) {
            NMMP_CHECK_LOG("point=%s status=UNSUPPORTED reason=symbol_unavailable", name);
            continue;
        }
        bool compared = false;
        for (unsigned i = 0; i < count; ++i) {
            const auto &load = loads[i];
            if (!(load.p_flags & PF_X) || entry.st_value < load.p_vaddr
                    || !range(entry.st_value - load.p_vaddr, 32, load.p_filesz)) continue;
            const uint64_t offset = load.p_offset + entry.st_value - load.p_vaddr;
            const uint8_t *original = file + offset;
            for (size_t m = 0; m < maps->count; ++m) {
                const auto &mapping = maps->entries[m];
                if (!(mapping.permissions & NMMP_MAP_EXEC) || strcmp(nmmpMappingName(maps, &mapping), path)
                        || mapping.inode != static_cast<uint64_t>(statbuf.st_ino)
                        || mapping.deviceMajor != major(statbuf.st_dev) || mapping.deviceMinor != minor(statbuf.st_dev)
                        || offset < mapping.offset || !range(offset - mapping.offset, 32, mapping.end - mapping.start)) continue;
                const uintptr_t address = mapping.start + static_cast<uintptr_t>(offset - mapping.offset);
                uint8_t runtime[32];
                struct iovec local = {runtime, sizeof(runtime)}, remote = {reinterpret_cast<void *>(address), sizeof(runtime)};
                ssize_t bytes;
                unsigned interruptions = 0;
                do { bytes = nmmpReadSelfMemory(&local, &remote); }
                while (bytes < 0 && errno == EINTR && ++interruptions <= 64);
                if (bytes != sizeof(runtime)) break;
                ++report.checked;
                compared = true;
                NMMP_CHECK_LOG("point=%s status=%s bytes=32", name,
                               memcmp(original, runtime, sizeof(runtime)) ? "MODIFIED" : "PASS");
                if (memcmp(original, runtime, sizeof(runtime))) {
                    ++report.modified;
                    uintptr_t target;
                    if (nmmpArtRedirectTarget(original, runtime, address, &target)) {
                        const auto *owner = nmmpFindMapping(maps, target);
                        if (!owner || !(owner->permissions & NMMP_MAP_EXEC)
                                || strcmp(nmmpMappingName(maps, owner), path)) ++report.externalTargets;
                    }
                    NMMP_CHECK_LOG("ART_ENTRY_MODIFIED name=%s redirect_external=%u", name, report.externalTargets);
                }
                break;
            }
            break;
        }
        if (!compared) NMMP_CHECK_LOG("point=%s status=UNSUPPORTED reason=baseline_or_read_unavailable", name);
    }
    report.result = report.modified ? ArtIntegrityResult::MODIFIED
            : report.checked == 3 ? ArtIntegrityResult::NORMAL : ArtIntegrityResult::UNSUPPORTED;
    free(maps); munmap(const_cast<uint8_t *>(file), size);
    return report;
#endif
}
