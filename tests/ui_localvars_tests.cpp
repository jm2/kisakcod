// Menu local variables (ui/ui_localvars.cpp): UILocalVar_FindOrCreate stores a
// variable in the context's 256-slot hash table and UILocalVar_Find returns it
// again. The decompiled lookups returned (char *)context + 12 * hash: the x86
// sizeof(UILocalVar). On 64-bit the variable is 24 bytes, so every slot but
// hash 0 resolved into the middle of another variable, and
// UILocalVar_SetString would then FreeString a garbage pointer.

#include <cstdio>
#include <cstring>
#include <string_view>

#include <ui/ui_shared.h>

namespace
{
int g_failures = 0;
int g_asserts = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

UILocalVar *AsVar(UILocalVarContext *found)
{
    // The engine's callers use the returned "context" as the variable:
    // found->table is the UILocalVar the lookup located.
    return found ? found->table : nullptr;
}
}

// The engine boundary ui_localvars.cpp links against.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_asserts;
}

const char *CopyString(const char *in)
{
    const std::string_view text(in);
    char *out = new char[text.size() + 1];
    text.copy(out, text.size());
    out[text.size()] = '\0';
    return out;
}

void __cdecl FreeString(const char *str)
{
    delete[] str;
}

// Only UILocalVar_GetString formats, and these checks never call it.
int Com_sprintf(char *dest, uint32_t size, const char *, ...)
{
    if (size)
        dest[0] = '\0';
    return 0;
}

int main()
{
    static UILocalVarContext context{};
    UILocalVar_Init(&context);

    // Names in different, non-zero hash slots.
    const char *names[] = { "ui_lv_alpha", "ui_lv_beta", "ui_lv_gamma" };
    Check(UILocalVar_HashName(names[0]) != UILocalVar_HashName(names[1]), "test names hash to different slots");
    Check(UILocalVar_HashName(names[1]) != 0 && UILocalVar_HashName(names[2]) != 0, "test names avoid slot 0");

    for (int i = 0; i < 3; ++i)
    {
        UILocalVar *created = AsVar(UILocalVar_FindOrCreate(&context, const_cast<char *>(names[i])));
        Check(created == &context.table[UILocalVar_HashName(names[i])], "FindOrCreate places the variable in its slot");
        if (created)
            UILocalVar_SetInt(created, 100 + i);
    }

    for (int i = 0; i < 3; ++i)
    {
        UILocalVar *found = AsVar(UILocalVar_Find(&context, names[i]));
        Check(found == &context.table[UILocalVar_HashName(names[i])], "Find returns the variable's slot");
        Check(found && found->name && std::strcmp(found->name, names[i]) == 0, "Find returns the named variable");
        Check(found && UILocalVar_GetInt(found).integer == 100 + i, "the stored value reads back");

        UILocalVar *again = AsVar(UILocalVar_FindOrCreate(&context, const_cast<char *>(names[i])));
        Check(again == found, "FindOrCreate finds an existing variable in its slot");
    }

    // A string value is freed when replaced, so the slot must be the real one.
    UILocalVar_SetString(AsVar(UILocalVar_Find(&context, names[1])), const_cast<char *>("first"));
    UILocalVar_SetString(AsVar(UILocalVar_Find(&context, names[1])), const_cast<char *>("second"));
    UILocalVar *stringVar = AsVar(UILocalVar_Find(&context, names[1]));
    Check(stringVar && stringVar->type == UILOCALVAR_STRING && std::strcmp(stringVar->u.string, "second") == 0,
          "a replaced string value reads back");

    UILocalVar_Shutdown(&context);
    Check(g_asserts == 0, "no asserts");
    if (g_failures)
        return 1;
    std::printf("ui-localvars: all checks passed\n");
    return 0;
}
