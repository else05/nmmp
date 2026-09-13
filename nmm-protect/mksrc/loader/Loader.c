#define _GNU_SOURCE
#include "Loader.h"
#include "Envelope.h"
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LOADS 16
#define MAX_BLOCKS 4
#define MAX_SYMBOLS 65536
#define MAX_RELOCS 1000000

typedef struct {
    uint64_t addr, filesz, memsz, align, flags, offset;
} Segment;
typedef struct { uint32_t kind; uint64_t addr, size, offset; } Block;
typedef struct {
    uint64_t hash, strings, strsz, symbols, rela, relasz, jmprel, pltsz;
    uint64_t init, fini, init_array, init_size, fini_array, fini_size;
    uint64_t versym, verneed, vercount;
    uint32_t needed[5], needed_count;
} Dynamic;
typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t nseg, nblock, nimport, nsym, entry;
    Segment segments[MAX_LOADS];
    Block blocks[MAX_BLOCKS];
    const uint8_t *imports;
    Dynamic dynamic;
    uint64_t relro_addr, relro_size;
    size_t page;
    uint64_t low, high;
    uint32_t version_names[256], version_providers[256];
} Content;

struct NmmpModule {
    void *mapping;
    size_t size;
    uintptr_t bias;
    void *dependencies[5];
    uint32_t dependency_count;
    void *bootstrap;
    void (*init)(void);
    uintptr_t *init_array;
    size_t init_count;
    int constructing;
};

static int page_flags(const Content *c, uint64_t addr);

static int range(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}
static int overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m) {
    return n && m && a < b + m && b < a + n;
}
static uint64_t down(uint64_t a, size_t page) { return a & ~((uint64_t)page - 1); }
static uint64_t up(uint64_t a, size_t page) { return (a + page - 1) & ~((uint64_t)page - 1); }

static const Segment *segment(const Content *c, uint64_t addr, uint64_t size, int file) {
    for (uint32_t i = 0; i < c->nseg; ++i) {
        const Segment *s = &c->segments[i];
        if (addr >= s->addr && range(addr - s->addr, size, file ? s->filesz : s->memsz)) return s;
    }
    return NULL;
}
static const uint8_t *at(const Content *c, uint64_t addr, uint64_t size) {
    const Segment *s = segment(c, addr, size, 1);
    return s ? c->data + s->offset + addr - s->addr : NULL;
}
static const char *string_at(const Content *c, uint32_t offset) {
    const Dynamic *d = &c->dynamic;
    if (offset >= d->strsz) return NULL;
    const uint8_t *p = at(c, d->strings + offset, d->strsz - offset);
    return p && memchr(p, 0, (size_t)d->strsz - offset) ? (const char *)p : NULL;
}
static int public_library(const char *s) {
    return s && (!strcmp(s, "libc.so") || !strcmp(s, "libm.so") || !strcmp(s, "libdl.so") ||
                 !strcmp(s, "liblog.so") || !strcmp(s, "libz.so"));
}
static int executable(const Content *c, uint64_t addr) {
    const Segment *s = segment(c, addr, 4, 0);
    return s && (s->flags & 1) && !(addr & 3);
}

static int tables(Content *c) {
    if (c->size < 64 || c->size > NMMP_LOADER_LIMIT || memcmp(c->data, "NMMPIM01", 8) ||
        nmmp_u32(c->data + 8) != 1 || nmmp_u32(c->data + 12) != (3U << 16 | 183U) ||
        nmmp_u32(c->data + 16)) return 0;
    c->nseg = nmmp_u32(c->data + 20);
    c->nblock = nmmp_u32(c->data + 24);
    c->nimport = nmmp_u32(c->data + 28);
    c->nsym = nmmp_u32(c->data + 32);
    c->entry = nmmp_u32(c->data + 36);
    if (!c->nseg || c->nseg > MAX_LOADS || c->nblock < 2 || c->nblock > MAX_BLOCKS ||
        !c->nsym || c->nsym > MAX_SYMBOLS || c->entry >= c->nsym || c->nimport >= c->nsym) return 0;
    uint64_t boff = 64 + c->nseg * 56, ioff = boff + c->nblock * 32;
    uint64_t cursor = ioff + c->nimport * 20;
    if (nmmp_u64(c->data + 40) != 64 || nmmp_u64(c->data + 48) != boff ||
        nmmp_u64(c->data + 56) != ioff || !range(0, cursor, c->size)) return 0;
    c->imports = c->data + ioff;
    for (uint32_t i = 0; i < c->nblock; ++i) {
        const uint8_t *p = c->data + boff + i * 32;
        Block *b = &c->blocks[i];
        b->kind = nmmp_u32(p); b->addr = nmmp_u64(p + 8);
        b->size = nmmp_u64(p + 16); b->offset = nmmp_u64(p + 24);
        if ((i < 2 ? b->kind != i + 1 : (b->kind < 3 || b->kind > 4 || b->kind <= c->blocks[i - 1].kind)) ||
            nmmp_u32(p + 4) || b->offset != cursor || !b->size ||
            !range(cursor, b->size, c->size) || !range(b->addr, b->size, NMMP_LOADER_LIMIT)) return 0;
        if ((i == 0 && (b->size % 56 || b->size / 56 > 32)) ||
            (i == 1 && b->size % 16) || (i >= 2 && (b->size % 24 || b->size / 24 > MAX_RELOCS))) return 0;
        cursor += b->size;
        for (uint32_t j = 0; j < i; ++j)
            if (overlap(b->addr, b->size, c->blocks[j].addr, c->blocks[j].size)) return 0;
    }
    c->low = NMMP_LOADER_LIMIT;
    for (uint32_t i = 0; i < c->nseg; ++i) {
        const uint8_t *p = c->data + 64 + i * 56;
        Segment *s = &c->segments[i];
        s->addr = nmmp_u64(p); s->filesz = nmmp_u64(p + 8); s->memsz = nmmp_u64(p + 16);
        s->align = nmmp_u64(p + 24); s->flags = nmmp_u64(p + 32); s->offset = nmmp_u64(p + 40);
        if (!s->memsz || s->filesz > s->memsz || !range(s->addr, s->memsz, NMMP_LOADER_LIMIT) ||
            s->offset != cursor || nmmp_u64(p + 48) != s->filesz || !range(cursor, s->filesz, c->size) ||
            !s->align || s->align > c->page || (s->align & (s->align - 1)) ||
            (s->flags & ~7) || !(s->flags & 4) || (s->flags & 3) == 3) return 0;
        cursor += s->filesz;
        uint64_t start = down(s->addr, c->page), end = up(s->addr + s->memsz, c->page);
        if (start < c->low) c->low = start;
        if (end > c->high) c->high = end;
        for (uint32_t j = 0; j < i; ++j) {
            const Segment *t = &c->segments[j];
            if (overlap(s->addr, s->memsz, t->addr, t->memsz)) return 0;
            if (overlap(start, end - start, down(t->addr, c->page),
                        up(t->addr + t->memsz, c->page) - down(t->addr, c->page)) &&
                ((s->flags | t->flags) & 3) == 3) return 0;
        }
    }
    if (cursor != c->size || c->high - c->low > NMMP_LOADER_LIMIT) return 0;
    for (uint32_t i = 0; i < c->nblock; ++i) {
        Block *b = &c->blocks[i];
        uint8_t *p = (uint8_t *)at(c, b->addr, b->size);
        if (!p) return 0;
        for (uint64_t j = 0; j < b->size; ++j) if (p[j]) return 0;
        memcpy(p, c->data + b->offset, b->size);
    }
    return 1;
}

static int program_headers(Content *c) {
    const Block *b = &c->blocks[0];
    const uint8_t *p = c->data + b->offset;
    unsigned loads = 0, dynamics = 0, relros = 0, phdrs = 0;
    for (uint64_t i = 0; i < b->size; i += 56) {
        uint32_t type = nmmp_u32(p + i), flags = nmmp_u32(p + i + 4);
        uint64_t offset = nmmp_u64(p + i + 8), addr = nmmp_u64(p + i + 16);
        uint64_t filesz = nmmp_u64(p + i + 32), memsz = nmmp_u64(p + i + 40), align = nmmp_u64(p + i + 48);
        if (filesz > memsz || !range(addr, memsz, NMMP_LOADER_LIMIT)) return 0;
        switch (type) {
            case 0: break;
            case 1: {
                if (loads >= c->nseg) return 0;
                const Segment *s = &c->segments[loads++];
                if (s->addr != addr || s->filesz != filesz || s->memsz != memsz ||
                    s->align != align || s->flags != flags || offset % align != addr % align) return 0;
                break;
            }
            case 2:
                if (++dynamics != 1 || addr != c->blocks[1].addr || filesz != c->blocks[1].size) return 0;
                break;
            case 6:
                if (++phdrs != 1 || addr != b->addr || filesz != b->size) return 0;
                break;
            case 4: case 0x6474e550:
                if (filesz && !at(c, addr, filesz)) return 0;
                break;
            case 0x6474e551: if (flags & 1) return 0; break;
            case 0x6474e552:
                if (++relros != 1 || !memsz) return 0;
                c->relro_addr = down(addr, c->page);
                c->relro_size = up(addr + memsz, c->page) - c->relro_addr;
                break;
            default: return 0;
        }
    }
    if (c->relro_size) {
        if (c->relro_addr < c->low || !range(c->relro_addr - c->low, c->relro_size, c->high - c->low)) return 0;
        for (uint64_t addr = c->relro_addr; addr < c->relro_addr + c->relro_size; addr += c->page) {
            int flags = page_flags(c, addr);
            if (!flags || (flags & 1)) return 0;
        }
    }
    return loads == c->nseg && dynamics == 1;
}

static int dynamic_table(Content *c) {
    const Block *b = &c->blocks[1];
    const uint8_t *p = c->data + b->offset;
    Dynamic *d = &c->dynamic;
    uint64_t seen[40] = {0};
    unsigned count = 0;
    int ended = 0;
    for (uint64_t i = 0; i < b->size; i += 16) {
        uint64_t tag = nmmp_u64(p + i), v = nmmp_u64(p + i + 8);
        if (!tag) { ended = 1; break; }
        if (tag != 1) {
            for (unsigned j = 0; j < count; ++j) if (seen[j] == tag) return 0;
            if (count == 40) return 0;
            seen[count++] = tag;
        }
        switch (tag) {
            case 1: if (d->needed_count == 5 || v > UINT32_MAX) return 0;
                d->needed[d->needed_count++] = (uint32_t)v; break;
            case 2: d->pltsz = v; break;
            case 3: if (!segment(c, v, 24, 0)) return 0; break;
            case 4: d->hash = v; break;
            case 5: d->strings = v; break;
            case 6: d->symbols = v; break;
            case 7: d->rela = v; break;
            case 8: d->relasz = v; break;
            case 9: case 11: if (v != 24) return 0; break;
            case 10: d->strsz = v; break;
            case 12: d->init = v; break;
            case 13: d->fini = v; break;
            case 14: if (v > UINT32_MAX) return 0; break;
            case 16: case 24: if (v) return 0; break;
            case 20: if (v != 7) return 0; break;
            case 21: if (v) return 0; break;
            case 23: d->jmprel = v; break;
            case 25: d->init_array = v; break;
            case 26: d->fini_array = v; break;
            case 27: d->init_size = v; break;
            case 28: d->fini_size = v; break;
            case 30: if (v & ~UINT64_C(10)) return 0; break;
            case 0x6ffffff9: if (v > MAX_RELOCS) return 0; break;
            case 0x6ffffffb: if (v & ~UINT64_C(1)) return 0; break;
            case 0x6ffffff0: d->versym = v; break;
            case 0x6ffffffe: d->verneed = v; break;
            case 0x6fffffff: d->vercount = v; break;
            default: return 0;
        }
    }
    if (!ended || !d->strsz || d->strsz > NMMP_LOADER_LIMIT || !at(c, d->strings, d->strsz) ||
        *at(c, d->strings, 1) || !at(c, d->symbols, c->nsym * 24)) return 0;
    const uint8_t *hash = at(c, d->hash, 8);
    if (!hash || !nmmp_u32(hash) || nmmp_u32(hash) > MAX_SYMBOLS || nmmp_u32(hash + 4) != c->nsym) return 0;
    uint32_t words = nmmp_u32(hash) + c->nsym;
    hash = at(c, d->hash + 8, (uint64_t)words * 4);
    if (!hash) return 0;
    for (uint32_t i = 0; i < words; ++i) if (nmmp_u32(hash + i * 4) >= c->nsym) return 0;
    /* No hash-chain traversal is needed: entry and import indices are explicit. */
    uint64_t addresses[2] = {d->rela, d->jmprel}, sizes[2] = {d->relasz, d->pltsz};
    for (unsigned i = 0; i < 2; ++i) {
        if (!!addresses[i] != !!sizes[i] || sizes[i] % 24 || sizes[i] / 24 > MAX_RELOCS) return 0;
        const Block *found = NULL;
        for (uint32_t j = 0; j < c->nblock; ++j) if (c->blocks[j].kind == i + 3) found = &c->blocks[j];
        if (!!found != !!sizes[i] || (found && (found->addr != addresses[i] || found->size != sizes[i]))) return 0;
    }
    uint64_t arrays[2] = {d->init_array, d->fini_array}, lengths[2] = {d->init_size, d->fini_size};
    for (unsigned i = 0; i < 2; ++i)
        if (!!arrays[i] != !!lengths[i] || arrays[i] % 8 || lengths[i] % 8 || lengths[i] > 8192 ||
            (lengths[i] && !at(c, arrays[i], lengths[i]))) return 0;
    if ((d->init && !executable(c, d->init)) || (d->fini && !executable(c, d->fini))) return 0;
    for (uint32_t i = 0; i < d->needed_count; ++i) {
        if (!public_library(string_at(c, d->needed[i]))) return 0;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(string_at(c, d->needed[i]), string_at(c, d->needed[j]))) return 0;
    }
    return 1;
}

static uint32_t version_hash(const char *name) {
    uint32_t hash = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        hash = (hash << 4) + *p;
        uint32_t high = hash & 0xf0000000U;
        hash ^= high >> 24;
        hash &= ~high;
    }
    return hash;
}

static int versions(Content *c) {
    Dynamic *d = &c->dynamic;
    if (!d->versym && !d->verneed && !d->vercount) return 1;
    if (!d->versym || !d->verneed || !d->vercount || d->vercount > 5 ||
        !at(c, d->versym, c->nsym * 2)) return 0;
    uint64_t cursor = d->verneed, ranges[326][2];
    unsigned nranges = 0;
    ranges[nranges][0] = d->versym; ranges[nranges++][1] = c->nsym * 2;
    for (uint64_t i = 0; i < d->vercount; ++i) {
        const uint8_t *p = at(c, cursor, 16);
        if (!p) return 0;
        uint32_t count = nmmp_u32(p) >> 16, provider = nmmp_u32(p + 4);
        uint32_t aux = nmmp_u32(p + 8), next = nmmp_u32(p + 12);
        if ((nmmp_u32(p) & 65535) != 1 || !count || count > 64 || aux < 16 || aux % 4) return 0;
        int found = 0;
        for (uint32_t k = 0; k < d->needed_count; ++k)
            if (string_at(c, provider) && !strcmp(string_at(c, provider), string_at(c, d->needed[k]))) found = 1;
        if (!found) return 0;
        ranges[nranges][0] = cursor; ranges[nranges++][1] = 16;
        uint64_t a = cursor + aux;
        for (uint32_t j = 0; j < count; ++j) {
            p = at(c, a, 16);
            if (!p) return 0;
            uint32_t value = nmmp_u32(p), index = nmmp_u32(p + 4) >> 16;
            uint32_t name = nmmp_u32(p + 8), link = nmmp_u32(p + 12);
            const char *text = string_at(c, name);
            if ((nmmp_u32(p + 4) & 65535) || index < 2 || index >= 256 ||
                c->version_names[index] || !name || !text || version_hash(text) != value) return 0;
            c->version_names[index] = name;
            c->version_providers[index] = provider;
            ranges[nranges][0] = a; ranges[nranges++][1] = 16;
            if (j + 1 == count ? link != 0 : link < 16 || link % 4) return 0;
            a += link;
        }
        if (i + 1 == d->vercount ? next != 0 : next < 16 || next % 4) return 0;
        cursor += next;
    }
    for (unsigned i = 0; i < nranges; ++i) for (unsigned j = 0; j < i; ++j)
        if (overlap(ranges[i][0], ranges[i][1], ranges[j][0], ranges[j][1])) return 0;
    return 1;
}

static int symbols(Content *c) {
    const uint8_t *syms = at(c, c->dynamic.symbols, c->nsym * 24);
    for (unsigned j = 0; j < 24; ++j) if (syms[j]) return 0;
    const uint8_t *version = c->dynamic.versym ? at(c, c->dynamic.versym, c->nsym * 2) : NULL;
    if (version && (version[0] || version[1])) return 0;
    uint32_t imports = 0;
    for (uint32_t i = 1; i < c->nsym; ++i) {
        const uint8_t *s = syms + i * 24;
        unsigned bind = s[4] >> 4, type = s[4] & 15, shndx = s[6] | (unsigned)s[7] << 8;
        uint64_t addr = nmmp_u64(s + 8), size = nmmp_u64(s + 16);
        unsigned ver = version ? (unsigned)version[i * 2] | (unsigned)version[i * 2 + 1] << 8 : 1;
        if (bind > 2 || type > 2 || (s[5] & ~3) || shndx >= 0xff00 || !string_at(c, nmmp_u32(s))) return 0;
        if (!ver || ver >= 256 || (ver > 1 && !c->version_names[ver])) return 0;
        if (shndx) {
            if (ver != 1 || !segment(c, addr, size ? size : 1, 0)) return 0;
        } else {
            if (imports >= c->nimport || !bind || addr || size) return 0;
            const uint8_t *m = c->imports + imports++ * 20;
            if (nmmp_u32(m) != i || nmmp_u32(m + 8) != c->version_names[ver] ||
                nmmp_u32(m + 12) != bind || nmmp_u32(m + 16) != type) return 0;
            int found = 0;
            for (uint32_t k = 0; k < c->dynamic.needed_count; ++k)
                if (nmmp_u32(m + 4) == c->dynamic.needed[k]) found = 1;
            if (!found) return 0;
            if (ver > 1 && strcmp(string_at(c, nmmp_u32(m + 4)), string_at(c, c->version_providers[ver]))) return 0;
        }
    }
    const uint8_t *entry = syms + c->entry * 24;
    return imports == c->nimport && entry[4] == 18 && entry[5] == 0 && (entry[6] || entry[7]) &&
           !strcmp(string_at(c, nmmp_u32(entry)), "nmmp_inner_bootstrap_v1") &&
           executable(c, nmmp_u64(entry + 8));
}

static int relocation_table(Content *c, uint64_t addr, uint64_t size, NmmpModule *m, const uintptr_t *resolved) {
    const uint8_t *p = size ? at(c, addr, size) : NULL;
    if (size && !p) return 0;
    for (uint64_t i = 0; i < size; i += 24) {
        uint64_t target = nmmp_u64(p + i), info = nmmp_u64(p + i + 8), a = nmmp_u64(p + i + 16);
        uint32_t type = (uint32_t)info, sym = (uint32_t)(info >> 32);
        if (sym >= c->nsym || (type != 0 && type != 257 && type != 1025 && type != 1026 && type != 1027)) return 0;
        if (!type) continue;
        const Segment *s = segment(c, target, 8, 0);
        if (!s || !(s->flags & 2) || (s->flags & 1) || (target & 7)) return 0;
        if (type == 1027 && (sym || a >= NMMP_LOADER_LIMIT || !segment(c, a, 1, 0))) return 0;
        if (m) {
            uintptr_t base = type == 1027 ? m->bias : resolved[sym], value;
            if (a <= INT64_MAX) {
                if (base > UINTPTR_MAX - a) return 0;
                value = base + a;
            } else {
                uint64_t magnitude = (~a) + 1;
                if (base < magnitude) return 0;
                value = base - magnitude;
            }
            memcpy((void *)(m->bias + target), &value, sizeof(value));
        }
    }
    return 1;
}

static int page_flags(const Content *c, uint64_t addr) {
    int flags = 0;
    for (uint32_t i = 0; i < c->nseg; ++i) {
        const Segment *s = &c->segments[i];
        if (overlap(addr, c->page, s->addr, s->memsz)) flags |= (int)s->flags;
    }
    return flags;
}

void nmmp_discard_image(NmmpModule *m) {
    if (!m || m->constructing) return;
    if (m->mapping) munmap(m->mapping, m->size);
    for (uint32_t i = m->dependency_count; i; --i) dlclose(m->dependencies[i - 1]);
    free(m);
}

int nmmp_map_image(uint8_t *decoded, size_t size, NmmpModule **out) {
    *out = NULL;
    Content c = {0};
    c.data = decoded; c.size = size;
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || page > NMMP_LOADER_LIMIT || (page & (page - 1))) return -1;
    c.page = (size_t)page;
    if (!decoded || !tables(&c) || !program_headers(&c) || !dynamic_table(&c) || !versions(&c) || !symbols(&c) ||
        !relocation_table(&c, c.dynamic.rela, c.dynamic.relasz, NULL, NULL) ||
        !relocation_table(&c, c.dynamic.jmprel, c.dynamic.pltsz, NULL, NULL)) return -1;
    /* Reject duplicate relocation targets in linear space before writing any GOT. */
    uint8_t *targets = calloc((size_t)((c.high - c.low) / 8 + 1), 1);
    if (!targets) return -2;
    uint64_t ra[2] = {c.dynamic.rela, c.dynamic.jmprel}, rz[2] = {c.dynamic.relasz, c.dynamic.pltsz};
    for (unsigned t = 0; t < 2; ++t) for (uint64_t i = 0; i < rz[t]; i += 24) {
        const uint8_t *r = at(&c, ra[t] + i, 24);
        if (!nmmp_u32(r + 8)) continue;
        size_t index = (size_t)((nmmp_u64(r) - c.low) / 8);
        if (targets[index]) { free(targets); return -1; }
        targets[index] = 1;
    }
    free(targets);
    NmmpModule *m = calloc(1, sizeof(*m));
    uintptr_t *resolved = calloc(c.nsym, sizeof(*resolved));
    if (!m || !resolved) { free(m); free(resolved); return -2; }
    m->size = (size_t)(c.high - c.low);
    void *mapping = mmap(NULL, m->size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) goto failed;
    m->mapping = mapping;
    if ((uintptr_t)mapping < c.low) goto failed;
    m->bias = (uintptr_t)mapping - c.low;
    /* Only LOAD-owned pages become writable; no executable page exists yet. */
    for (uint64_t addr = c.low; addr < c.high; addr += c.page)
        if (page_flags(&c, addr) && mprotect((void *)(m->bias + addr), c.page, PROT_READ | PROT_WRITE)) goto failed;
    for (uint32_t i = 0; i < c.nseg; ++i) {
        const Segment *s = &c.segments[i];
        memcpy((void *)(m->bias + s->addr), decoded + s->offset, (size_t)s->filesz);
        /* Anonymous pages already zero BSS, including tails sharing LOAD pages. */
    }
    for (uint32_t i = 0; i < c.dynamic.needed_count; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/system/lib64/%s", string_at(&c, c.dynamic.needed[i]));
        void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!handle) goto failed;
        m->dependencies[m->dependency_count++] = handle;
    }
    const uint8_t *syms = at(&c, c.dynamic.symbols, c.nsym * 24);
    for (uint32_t i = 1; i < c.nsym; ++i) {
        const uint8_t *s = syms + i * 24;
        if (s[6] || s[7]) resolved[i] = m->bias + nmmp_u64(s + 8);
    }
    for (uint32_t i = 0; i < c.nimport; ++i) {
        const uint8_t *p = c.imports + i * 20;
        uint32_t sym = nmmp_u32(p), dep = nmmp_u32(p + 4);
        void *handle = NULL;
        for (uint32_t j = 0; j < c.dynamic.needed_count; ++j)
            if (c.dynamic.needed[j] == dep) handle = m->dependencies[j];
        const char *name = string_at(&c, nmmp_u32(syms + sym * 24));
        dlerror();
        void *value = dlsym(handle, name);
        const char *error = dlerror();
        if ((error || !value) && nmmp_u32(p + 12) != 2) goto failed;
        int missing = error || !value;
        if (nmmp_u32(p + 8)) {
            dlerror();
            void *versioned = dlvsym(handle, name, string_at(&c, nmmp_u32(p + 8)));
            const char *version_error = dlerror();
            if (missing ? (!version_error || versioned) : (version_error || versioned != value)) goto failed;
        }
        if (!missing) {
            Dl_info identity;
            if (!dladdr(value, &identity) || !identity.dli_fname) goto failed;
            char expected[64], actual_path[PATH_MAX], expected_path[PATH_MAX];
            struct stat actual_stat, expected_stat;
            snprintf(expected, sizeof(expected), "/system/lib64/%s", string_at(&c, dep));
            if (!realpath(identity.dli_fname, actual_path) || !realpath(expected, expected_path) ||
                strcmp(actual_path, expected_path) || stat(actual_path, &actual_stat) ||
                stat(expected, &expected_stat) || !S_ISREG(actual_stat.st_mode) ||
                actual_stat.st_dev != expected_stat.st_dev || actual_stat.st_ino != expected_stat.st_ino) goto failed;
        }
        /* Unresolved weak S=0; addend checks are identical for all supported S+A relocations. */
        resolved[sym] = missing ? 0 : (uintptr_t)value;
    }
    if (!relocation_table(&c, c.dynamic.rela, c.dynamic.relasz, m, resolved) ||
        !relocation_table(&c, c.dynamic.jmprel, c.dynamic.pltsz, m, resolved)) goto failed;
    m->bootstrap = (void *)resolved[c.entry];
    m->init = c.dynamic.init ? (void (*)(void))(m->bias + c.dynamic.init) : NULL;
    m->init_array = c.dynamic.init_size ? (uintptr_t *)(m->bias + c.dynamic.init_array) : NULL;
    m->init_count = (size_t)(c.dynamic.init_size / 8);
    for (size_t i = 0; i < m->init_count; ++i) {
        uintptr_t f = m->init_array[i];
        if (f && f != UINTPTR_MAX && (f < m->bias || !executable(&c, f - m->bias))) goto failed;
    }
    for (uint32_t i = 0; i < c.nseg; ++i) {
        const Segment *s = &c.segments[i];
        if (s->flags & 1) __builtin___clear_cache((char *)(m->bias + s->addr), (char *)(m->bias + s->addr + s->memsz));
    }
    for (uint64_t addr = c.low; addr < c.high; addr += c.page) {
        int flags = page_flags(&c, addr);
        int prot = (flags & 4 ? PROT_READ : 0) | (flags & 2 ? PROT_WRITE : 0) | (flags & 1 ? PROT_EXEC : 0);
        if (overlap(addr, c.page, c.relro_addr, c.relro_size)) {
            if (!flags || (flags & 1)) goto failed;
            prot &= ~PROT_WRITE;
        }
        if (mprotect((void *)(m->bias + addr), c.page, prot)) goto failed;
    }
    free(resolved);
    *out = m;
    return 0;
failed:
    free(resolved);
    nmmp_discard_image(m);
    return -3;
}

int nmmp_run_constructors(NmmpModule *m) {
    if (!m || m->constructing) return -1;
    m->constructing = 1;
    if (m->init) m->init();
    for (size_t i = 0; i < m->init_count; ++i)
        if (m->init_array[i] && m->init_array[i] != UINTPTR_MAX) ((void (*)(void))m->init_array[i])();
    return 0;
}
void *nmmp_bootstrap_address(const NmmpModule *m) { return m ? m->bootstrap : NULL; }
uintptr_t nmmp_image_bias(const NmmpModule *m) { return m ? m->bias : 0; }
const void *nmmp_image_start(const NmmpModule *m) { return m ? m->mapping : NULL; }
size_t nmmp_image_size(const NmmpModule *m) { return m ? m->size : 0; }
