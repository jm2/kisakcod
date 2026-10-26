// Com_Memset byte-fill contract at every destination alignment and length.
//
// The decompiled body filled with 4-byte and 2-byte stores through cast
// pointers, which is undefined behaviour when the destination is not naturally
// aligned (UBSan reports the uint32_t store inside _copyDWord). This runs the
// production universal/com_shared.cpp and checks both the filled bytes and the
// guard bytes on either side, so a fill that changes extent or alignment
// assumptions fails here and trips UBSan on the sanitizer legs.

#include <universal/q_shared.h>

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

// com_shared.cpp's Com_Filter is the only caller of I_stristr and Com_Memset
// does not reach it, so the definition lives here instead of linking all of
// universal/q_shared.cpp. The declaration in q_shared.h is at global scope.
const char *__cdecl I_stristr(const char *s0, const char *substr)
{
    for (const char *start = s0; *start; ++start)
    {
        const char *lhs = start;
        const char *rhs = substr;
        while (*lhs && *rhs
            && std::tolower(static_cast<unsigned char>(*lhs))
                == std::tolower(static_cast<unsigned char>(*rhs)))
        {
            ++lhs;
            ++rhs;
        }
        if (!*rhs)
            return start;
    }
    return nullptr;
}

// The pre-fix Com_Memset called this helper from qcommon/common.cpp. Keeping a
// definition here lets the mutation check relink the original body without
// pulling the whole qcommon object into this focused test.
void _copyDWord(uint32_t *dest, const uint32_t constant, const uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        dest[i] = constant;
}

// One fill at one offset and length; returns the number of failed expectations.
int CheckFill(int offset, std::size_t count, int value, unsigned char *storage)
{
    int failures = 0;

    std::memset(storage, 0xA5, 64);
    Com_Memset(storage + offset, value, count);

    const unsigned char expected = static_cast<unsigned char>(value);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (storage[offset + i] != expected)
        {
            std::printf(
                "FAIL offset=%d count=%zu value=0x%x: byte %zu is 0x%02x\n",
                offset, count, value, i, storage[offset + i]);
            ++failures;
            break;
        }
    }

    if (storage[offset + count] != 0xA5)
    {
        std::printf(
            "FAIL offset=%d count=%zu value=0x%x: wrote past the end\n",
            offset, count, value);
        ++failures;
    }

    for (int i = 0; i < offset; ++i)
    {
        if (storage[i] != 0xA5)
        {
            std::printf(
                "FAIL offset=%d count=%zu value=0x%x: wrote before the start\n",
                offset, count, value);
            ++failures;
            break;
        }
    }
    return failures;
}

int main()
{
    static const int values[] = {0, 1, 0x7f, 0xff, 0x1ff};
    unsigned char storage[64];
    int failures = 0;

    for (int offset = 0; offset <= 7; ++offset)
        for (std::size_t count = 0; count <= 24; ++count)
            for (const int value : values)
                failures += CheckFill(offset, count, value, storage);

    // A zero-length call must not touch a null destination.
    Com_Memset(nullptr, 0, 0);

    if (failures == 0)
        std::printf("com shared memset: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
