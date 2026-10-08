// q_shared_field_pointer_tests.cpp: the pointer field types of the production
// config-string parser (universal/q_shared.cpp, ParseConfigStringToStructCustomSize),
// compiled as the headless server compiles it. FX (8), XModel (9) and Material
// (0xA) store null there and the sound alias (0xB) stores what
// Com_FindSoundAlias returns; each must fill the whole pointer member. The old
// 32-bit stores truncated the alias and left half of every null as it was.

#include <universal/q_shared.h>
#include <universal/com_sndalias.h>
#include <qcommon/threads.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

struct PointerFields
{
    const void *effect;
    const void *model;
    const void *material;
    snd_alias_list_t *sound;
};

const cspField_t kFields[] = {
    {"effect", static_cast<int>(offsetof(PointerFields, effect)), 8},
    {"model", static_cast<int>(offsetof(PointerFields, model)), 9},
    {"material", static_cast<int>(offsetof(PointerFields, material)), 10},
    {"sound", static_cast<int>(offsetof(PointerFields, sound)), 11},
};

// A sound alias list the stub hands back; any address the test can compare.
snd_alias_list_t g_alias{};

void NoStrcpy(uint8_t *, const char *)
{
    std::abort();
}
} // namespace

snd_alias_list_t *__cdecl Com_FindSoundAlias(const char *name)
{
    return std::strcmp(name, "weap_test_fire") == 0 ? &g_alias : nullptr;
}

void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "engine assert %s:%d %s\n", file, line, fmt);
    std::abort();
}

void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error %s\n", fmt);
    std::abort();
}

bool Sys_IsMainThread()
{
    return true;
}

void *__cdecl Sys_GetValue(int valueIndex)
{
    static va_info_t vaInfo;
    if (valueIndex != 1)
        std::abort();
    return &vaInfo;
}

int main()
{
    PointerFields fields;
    std::memset(static_cast<void *>(&fields), 0xFF, sizeof(fields));
    char config[] = "\\effect\\fx/test\\model\\viewmodel_test\\material\\mtl_test\\sound\\weap_test_fire";
    Check(ParseConfigStringToStructCustomSize(reinterpret_cast<uint8_t *>(&fields), kFields, 4, config, 12,
              nullptr, NoStrcpy),
        "parse succeeds");
    Check(fields.effect == nullptr, "FX field (8) is a whole null pointer");
    Check(fields.model == nullptr, "XModel field (9) is a whole null pointer");
    Check(fields.material == nullptr, "Material field (0xA) is a whole null pointer");
    Check(fields.sound == &g_alias, "sound-alias field (0xB) holds the whole pointer");

    if (g_failures == 0)
        std::puts("config-string pointer field contracts passed");
    return g_failures == 0 ? 0 : 1;
}
