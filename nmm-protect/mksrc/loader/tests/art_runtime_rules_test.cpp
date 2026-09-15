#include "ArtRuntimeIntegrity.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
static void verify(bool v) { if (!v) std::abort(); }
int main(int argc, char **) {
    if (argc > 1) {
        auto r=nmmpCheckArtRuntimeIntegrity();
        printf("result=%d checked=%u modified=%u external=%u\n",int(r.result),r.checked,r.modified,r.externalTargets);
        return 0;
    }
    uint8_t original[32]={}, runtime[32]={}; uintptr_t target=0;
    verify(!nmmpArtRedirectTarget(original,runtime,0x1000,&target));
    uint32_t b=0x14000004; memcpy(runtime,&b,4);
    verify(nmmpArtRedirectTarget(original,runtime,0x1000,&target)&&target==0x1010);
    memcpy(original,runtime,32);
    verify(!nmmpArtRedirectTarget(original,runtime,0x1000,&target));
    memset(original,0,32); b=0x17ffffff; memcpy(runtime,&b,4);
    verify(nmmpArtRedirectTarget(original,runtime,0x1000,&target)&&target==0xffc);
    uint32_t ldr=0x58000050,br=0xd61f0200; uint64_t destination=0x12345678;
    memcpy(runtime,&ldr,4); memcpy(runtime+4,&br,4); memcpy(runtime+8,&destination,8);
    verify(nmmpArtRedirectTarget(original,runtime,0x1000,&target)&&target==destination);
    br=0xd61f0220; memcpy(runtime+4,&br,4);
    verify(!nmmpArtRedirectTarget(original,runtime,0x1000,&target));
    uint32_t adrp=0x90000010,add=0x91048210; br=0xd61f0200;
    memcpy(runtime,&adrp,4); memcpy(runtime+4,&add,4); memcpy(runtime+8,&br,4);
    verify(nmmpArtRedirectTarget(original,runtime,0x1000,&target)&&target==0x1120);
    puts("redirect_rules=PASS"); return 0;
}
