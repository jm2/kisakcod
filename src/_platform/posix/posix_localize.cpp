// posix_localize.cpp: language selection for the POSIX headless dedicated
// server. This is the POSIX counterpart of win32/win_localize.cpp; the engine
// asks for a language and for localized string references from shared code
// (qcommon/db_registry.cpp, qcommon/com_playerprofile.cpp), so the headless
// build needs the same entry points even though a dedicated server has no
// locale resources to load (docs/design/PLATFORM_POSIX.md, NOW row 13).

#include <cstdlib>
#include <cstring>

// The engine's localization entry points. They are declared in
// win32/win_localize.h for the Win32 build; the POSIX headless composition has
// no win32 source group, so the signatures are repeated here rather than
// reached through a platform header.
char *__cdecl Win_CopyLocalizationString(const char *string);
char *__cdecl Win_GetLanguage();
int __cdecl Win_InitLocalization();
char *__cdecl Win_LocalizeRef(const char *ref);
void __cdecl Win_ShutdownLocalization();

namespace
{
// The language a headless server reports. Retail resolves this from the
// install's localization files; a dedicated server has none, so it reports the
// plain-English default and resolves every reference to itself.
const char *const kDefaultLanguage = "english";
} // namespace

char *__cdecl Win_CopyLocalizationString(const char *string)
{
    if (!string)
        return nullptr;
    const size_t length = std::strlen(string) + 1;
    char *copy = static_cast<char *>(malloc(length));
    if (copy)
        std::memcpy(copy, string, length);
    return copy;
}

char *__cdecl Win_GetLanguage()
{
    return const_cast<char *>(kDefaultLanguage);
}

int __cdecl Win_InitLocalization()
{
    return 1;
}

void __cdecl Win_ShutdownLocalization()
{
}

char *__cdecl Win_LocalizeRef(const char *ref)
{
    // No localization table is loaded, so the reference itself is the string
    // the caller will show. Returning the ref (rather than null) keeps the
    // error dialogs and profile prompts readable instead of crashing a server
    // that never had a locale.
    return const_cast<char *>(ref ? ref : "");
}
