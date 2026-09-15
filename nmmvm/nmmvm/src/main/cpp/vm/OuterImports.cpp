#include "OuterIntegrity.h"
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace {
// The controlled outer build and supported system libc retain dynamic sections.
// Missing section metadata is unavailable, never an empty successful inventory.
struct Elf {
    const uint8_t *bytes;
    size_t size;
    Elf64_Ehdr header;
    Elf64_Shdr symbols, strings, versions, definitions, needs;
    uint64_t rela, relasz, plt, pltsz;
    bool range(uint64_t offset, uint64_t length) const {
        return offset <= size && length <= size - offset;
    }
    bool section(size_t index, Elf64_Shdr &out) const {
        if (index >= header.e_shnum) return false;
        std::memcpy(&out, bytes + header.e_shoff + index * sizeof(out), sizeof(out));
        return out.sh_type == SHT_NOBITS || range(out.sh_offset, out.sh_size);
    }
    const char *string(uint32_t offset) const {
        if (offset >= strings.sh_size) return nullptr;
        const char *value = reinterpret_cast<const char *>(bytes + strings.sh_offset + offset);
        return std::memchr(value, 0, strings.sh_size - offset) ? value : nullptr;
    }
    template<class T> bool item(const Elf64_Shdr &section, uint64_t offset, T &out) const {
        if (offset > section.sh_size || sizeof(T) > section.sh_size - offset) return false;
        std::memcpy(&out, bytes + section.sh_offset + offset, sizeof(T));
        return true;
    }
    bool init(const uint8_t *data, size_t length, bool imports) {
        *this = {};
        bytes = data; size = length;
        if (!bytes || size < sizeof(header) || size > NMMP_PRIVATE_MAX_IMAGE_BYTES) return false;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.e_ident, ELFMAG, SELFMAG) || header.e_ident[EI_CLASS] != ELFCLASS64
                || header.e_ident[EI_DATA] != ELFDATA2LSB || header.e_machine != EM_AARCH64
                || header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT
                || header.e_type != ET_DYN || header.e_ehsize != sizeof(Elf64_Ehdr)
                || header.e_phentsize != sizeof(Elf64_Phdr) || !header.e_phnum || header.e_phnum > 128
                || !range(header.e_phoff, size_t(header.e_phnum) * sizeof(Elf64_Phdr))
                || header.e_shentsize != sizeof(Elf64_Shdr)
                || !header.e_shnum || header.e_shnum > 4096
                || !range(header.e_shoff, size_t(header.e_shnum) * sizeof(Elf64_Shdr))) return false;
        Elf64_Shdr dynamic = {};
        size_t symbolIndex = 0;
        for (size_t i = 0; i < header.e_shnum; ++i) {
            Elf64_Shdr s;
            if (!section(i, s)) return false;
            if (s.sh_type == SHT_DYNSYM) {
                if (symbols.sh_size || !s.sh_size || s.sh_entsize != sizeof(Elf64_Sym)
                        || s.sh_size % sizeof(Elf64_Sym) || s.sh_size / sizeof(Elf64_Sym) > 65536) return false;
                symbols = s; symbolIndex = i;
            } else if (s.sh_type == SHT_DYNAMIC) {
                if (dynamic.sh_size) return false;
                dynamic = s;
            } else if (s.sh_type == SHT_GNU_versym) {
                if (versions.sh_size) return false;
                versions = s;
            } else if (s.sh_type == SHT_GNU_verdef) {
                if (definitions.sh_size) return false;
                definitions = s;
            } else if (s.sh_type == SHT_GNU_verneed) {
                if (needs.sh_size) return false;
                needs = s;
            }
        }
        if (!symbols.sh_size || !section(symbols.sh_link, strings) || strings.sh_type != SHT_STRTAB
                || !dynamic.sh_size || dynamic.sh_size % sizeof(Elf64_Dyn)
                || (versions.sh_size && (versions.sh_link != symbolIndex
                    || versions.sh_size != symbols.sh_size / sizeof(Elf64_Sym) * sizeof(Elf64_Half)))
                || (definitions.sh_size && definitions.sh_link != symbols.sh_link)
                || (needs.sh_size && needs.sh_link != symbols.sh_link)) return false;
        bool ended = false, sym = false, str = false;
        for (size_t i = 0; i < dynamic.sh_size; i += sizeof(Elf64_Dyn)) {
            Elf64_Dyn d;
            if (!item(dynamic, i, d)) return false;
            if (d.d_tag == DT_NULL) { ended = true; break; }
            switch (d.d_tag) {
                case DT_SYMTAB: sym = d.d_un.d_ptr == symbols.sh_addr; if (!sym) return false; break;
                case DT_STRTAB: str = d.d_un.d_ptr == strings.sh_addr; if (!str) return false; break;
                case DT_STRSZ: if (d.d_un.d_val != strings.sh_size) return false; break;
                case DT_SYMENT: if (d.d_un.d_val != sizeof(Elf64_Sym)) return false; break;
                case DT_RELA: rela = d.d_un.d_ptr; break;
                case DT_RELASZ: relasz = d.d_un.d_val; break;
                case DT_RELAENT: if (d.d_un.d_val != sizeof(Elf64_Rela)) return false; break;
                case DT_JMPREL: plt = d.d_un.d_ptr; break;
                case DT_PLTRELSZ: pltsz = d.d_un.d_val; break;
                case DT_PLTREL: if (d.d_un.d_val != DT_RELA) return false; break;
                case DT_VERSYM: if (d.d_un.d_ptr != versions.sh_addr || !versions.sh_size) return false; break;
                case DT_VERDEF: if (d.d_un.d_ptr != definitions.sh_addr || !definitions.sh_size) return false; break;
                case DT_VERNEED: if (d.d_un.d_ptr != needs.sh_addr || !needs.sh_size) return false; break;
                // Packed relocation encodings are not emitted by our outer build.
                case DT_REL: case DT_RELSZ: case 0x6000000f: case 0x60000010:
                case 0x60000011: case 0x60000012: if (imports) return false; break;
                default: break;
            }
        }
        return ended && sym && str;
    }
    bool libcVersion(size_t index, bool definition) const {
        if (!versions.sh_size) return false;
        Elf64_Half version;
        if (!item(versions, index * sizeof(version), version) || (version & 0x8000)) return false;
        version &= 0x7fff;
        if (version <= 1) return false;
        uint64_t offset = 0;
        for (unsigned i = 0; i < 256; ++i) {
            if (definition) {
                Elf64_Verdef v; Elf64_Verdaux a;
                if (!item(definitions, offset, v) || v.vd_version != VER_DEF_CURRENT) return false;
                if (v.vd_ndx == version) {
                    if (!v.vd_aux || !item(definitions, offset + v.vd_aux, a)) return false;
                    const char *name = string(a.vda_name);
                    return name && !std::strcmp(name, "LIBC");
                }
                if (!v.vd_next) return false;
                offset += v.vd_next;
            } else {
                Elf64_Verneed v;
                if (!item(needs, offset, v) || v.vn_version != VER_NEED_CURRENT || v.vn_cnt > 256) return false;
                const char *provider = string(v.vn_file);
                uint64_t aux = offset + v.vn_aux;
                for (unsigned j = 0; j < v.vn_cnt; ++j) {
                    Elf64_Vernaux a;
                    if (!item(needs, aux, a)) return false;
                    if ((a.vna_other & 0x7fff) == version) {
                        const char *name = string(a.vna_name);
                        return provider && name && !std::strcmp(provider, "libc.so") && !std::strcmp(name, "LIBC");
                    }
                    if (j + 1 < v.vn_cnt && !a.vna_next) return false;
                    aux += a.vna_next;
                }
                if (!v.vn_next) return false;
                offset += v.vn_next;
            }
        }
        return false;
    }
};
const char *const names[] = {"open", "open64", "openat", "openat64", "mmap", "mprotect"};
bool resolve(const Elf &libc, const char *name, const NmmpLoadedModule &module,
        const NmmpImageSegment *segments, size_t count, uintptr_t &address) {
    address = 0;
    for (size_t i = 0; i < libc.symbols.sh_size / sizeof(Elf64_Sym); ++i) {
        Elf64_Sym symbol;
        if (!libc.item(libc.symbols, i * sizeof(symbol), symbol)) return false;
        const char *candidate = libc.string(symbol.st_name);
        if (!candidate) return false;
        if (std::strcmp(candidate, name) || symbol.st_shndx == SHN_UNDEF) continue;
        if (ELF64_ST_TYPE(symbol.st_info) != STT_FUNC
                || (ELF64_ST_BIND(symbol.st_info) != STB_GLOBAL && ELF64_ST_BIND(symbol.st_info) != STB_WEAK)
                || (symbol.st_other & 3) != STV_DEFAULT || !libc.libcVersion(i, true)
                || symbol.st_value > UINTPTR_MAX - module.bias || address) return false;
        address = module.bias + symbol.st_value;
        if (!nmmpImageExecutable(segments, count, address)) return false;
    }
    return address != 0;
}
}

bool nmmpBuildOuterImportBaseline(const uint8_t *image, size_t size, const NmmpLoadedModule *outer,
        const uint8_t *libcImage, size_t libcSize, const NmmpLoadedModule *libcModule,
        NmmpImportSlot *slots, size_t *count) {
    if (!count) return false;
    *count = 0;
    if (!outer || !libcModule || !slots) return false;
    Elf source, provider;
    NmmpImageSegment executable[NMMP_PRIVATE_MAX_SEGMENTS]; size_t executableCount = 0;
    if (!source.init(image, size, true) || !provider.init(libcImage, libcSize, false)
            || !nmmpBuildOuterCodeBaseline(image, size, outer, executable, &executableCount)
            || !nmmpBuildOuterCodeBaseline(libcImage, libcSize, libcModule, executable, &executableCount)) return false;
    size_t used = 0;
    for (unsigned table = 0; table < 2; ++table) {
        const uint64_t address = table ? source.plt : source.rela;
        const uint64_t length = table ? source.pltsz : source.relasz;
        if (!length && !address) continue;
        if (!length || !address || length % sizeof(Elf64_Rela) || length / sizeof(Elf64_Rela) > 65536) return false;
        Elf64_Shdr relocations = {};
        for (size_t i = 0; i < source.header.e_shnum; ++i) {
            Elf64_Shdr section;
            if (!source.section(i, section)) return false;
            if (section.sh_type == SHT_RELA && section.sh_addr == address && section.sh_size == length) {
                if (relocations.sh_size) return false;
                relocations = section;
            }
        }
        if (!relocations.sh_size) return false;
        for (size_t i = 0; i < length; i += sizeof(Elf64_Rela)) {
            Elf64_Rela relocation; Elf64_Sym symbol;
            if (!source.item(relocations, i, relocation)) return false;
            const uint64_t index = ELF64_R_SYM(relocation.r_info);
            if (!index) continue;
            if (!source.item(source.symbols, index * sizeof(symbol), symbol)) return false;
            const char *name = source.string(symbol.st_name);
            if (!name) return false;
            unsigned id = 0;
            for (unsigned n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
                if (!std::strcmp(name, names[n])) id = n + 1;
            if (!id || symbol.st_shndx != SHN_UNDEF) continue;
            const uint32_t type = ELF64_R_TYPE(relocation.r_info);
            if ((type != R_AARCH64_JUMP_SLOT && type != R_AARCH64_GLOB_DAT) || relocation.r_addend
                    || ELF64_ST_BIND(symbol.st_info) != STB_GLOBAL || !source.libcVersion(index, false)
                    || relocation.r_offset > UINTPTR_MAX - outer->bias
                    || used == NMMP_PRIVATE_MAX_IMPORT_SLOTS) return false;
            const uintptr_t slot = outer->bias + relocation.r_offset;
            bool writable = false;
            for (size_t p = 0; p < source.header.e_phnum; ++p) {
                Elf64_Phdr ph;
                const uint64_t offset = source.header.e_phoff + p * sizeof(ph);
                if (!source.range(offset, sizeof(ph))) return false;
                std::memcpy(&ph, image + offset, sizeof(ph));
                if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) && !(ph.p_flags & PF_X)
                        && relocation.r_offset >= ph.p_vaddr && ph.p_memsz >= sizeof(uintptr_t)
                        && relocation.r_offset - ph.p_vaddr <= ph.p_memsz - sizeof(uintptr_t)) writable = true;
            }
            if (!writable || (slot & 7) || slot > UINTPTR_MAX - sizeof(uintptr_t)) return false;
            for (size_t j = 0; j < used; ++j) if (slots[j].address == slot) return false;
            uintptr_t expected;
            if (!resolve(provider, name, *libcModule, executable, executableCount, expected)) return false;
            slots[used++] = {slot, expected, id, 0};
        }
    }
    *count = used;
    return true;
}

bool nmmpReadSystemLibc(const NmmpLoadedModule *module, uint8_t **image, size_t *size) {
    if (!image || !size) return false;
    *image = nullptr; *size = 0;
    if (!module || (std::strcmp(module->name, "/system/lib64/libc.so")
            && std::strcmp(module->name, "/apex/com.android.runtime/lib64/bionic/libc.so"))) return false;
    int fd = open(module->name, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat before, after;
    bool valid = !fstat(fd, &before) && S_ISREG(before.st_mode) && before.st_size > 0
            && before.st_size <= NMMP_PRIVATE_MAX_IMAGE_BYTES;
    uint8_t *bytes = valid ? static_cast<uint8_t *>(std::malloc(static_cast<size_t>(before.st_size))) : nullptr;
    if (!bytes) valid = false;
    size_t done = 0; unsigned interruptions = 0;
    while (valid && done < static_cast<size_t>(before.st_size)) {
        ssize_t n = pread(fd, bytes + done, static_cast<size_t>(before.st_size) - done, done);
        if (n < 0 && errno == EINTR && ++interruptions < 64) continue;
        if (n <= 0) { valid = false; break; }
        done += static_cast<size_t>(n);
    }
    valid = valid && !fstat(fd, &after) && before.st_dev == after.st_dev && before.st_ino == after.st_ino
            && before.st_size == after.st_size && before.st_mtime == after.st_mtime && before.st_ctime == after.st_ctime;
    close(fd);
    if (!valid) { std::free(bytes); return false; }
    *image = bytes; *size = done;
    return true;
}
