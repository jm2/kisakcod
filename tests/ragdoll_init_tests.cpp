// ragdoll_init_tests.cpp: Ragdoll_Init (production ragdoll/ragdoll.cpp) clears
// every ragdoll body. The decompiled clear was the x86 0x13A80 bytes (32 x
// 0x9D4); RagdollBody holds pointers, so at 64-bit that left the last bodies'
// DObj, pose and rigid-body pointers alive across a map change.

#include <ragdoll/ragdoll.h>

#include <cstdio>
#include <cstring>

extern bool ragdollFirstInit;
extern const dvar_t *ragdoll_enable;
extern const dvar_t *ragdoll_max_simulating;

// Reached only with ragdoll_enable set and fewer than 8 simulating.
void __cdecl Dvar_SetInt(dvar_s *, int)
{
}

int __cdecl Sys_IsMainThread()
{
    return 1;
}

namespace
{
dvar_t g_enable;
dvar_t g_maxSimulating;
}

int main()
{
    g_maxSimulating.current.integer = 8;
    ragdoll_enable = &g_enable;
    ragdoll_max_simulating = &g_maxSimulating;
    ragdollFirstInit = true;
    ragdollInited = 0;
    std::memset(&ragdollBodies[31], 0xAB, sizeof(ragdollBodies[31]));
    Ragdoll_Init();

    const auto *bytes = reinterpret_cast<const unsigned char *>(&ragdollBodies[31]);
    for (size_t i = 0; i < sizeof(ragdollBodies[31]); ++i)
    {
        if (bytes[i] != 0)
        {
            std::fprintf(stderr, "FAIL ragdoll body 31 byte %zu survives Ragdoll_Init\n", i);
            return 1;
        }
    }
    std::puts("ragdoll init: ok");
    return 0;
}
