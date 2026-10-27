// Issue #847: the console packs HUD icons into its text stream and the render
// thread unpacks them.  The reader used to load a full Material* from a 4-byte
// field at offset 3, so on 64-bit it read past the end of the 7-byte record and
// the respawn/death-message path crashed in IsValidMaterialHandle().
#include <qcommon/hudicon.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

static int failures;

static void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    static_assert(hudicon::kTypeOffset == 0);
    static_assert(hudicon::kWidthOffset == 1);
    static_assert(hudicon::kHeightOffset == 2);
    static_assert(hudicon::kHandleOffset == 3);
    static_assert(hudicon::kRecordBytes == 3 + static_cast<int>(sizeof(void *)));

    // The 32-bit layout is the original one; nothing on x86 moves.
    Check(hudicon::kRecordBytes == (sizeof(void *) == 4 ? 7 : 11),
          "record length follows pointer width");
    Check(hudicon::kEncodedBytes == hudicon::kRecordBytes + 1,
          "the '^' introducer is counted once");

    // Deliberately misaligned storage, the way the console and death-message
    // buffers lay these out.
    alignas(1) char first[hudicon::kEncodedBytes + 1];
    alignas(1) char second[hudicon::kEncodedBytes + 1];
    std::memset(first, 0x5C, sizeof(first));
    std::memset(second, 0x5C, sizeof(second));

    std::uintptr_t raw = static_cast<std::uintptr_t>(0x12345678u);
#if UINTPTR_MAX > 0xFFFFFFFFu
    raw |= static_cast<std::uintptr_t>(0x9ABCDEF0u) << 32;
#endif
    const void *handle = reinterpret_cast<const void *>(raw);

    char *record = first + hudicon::kIntroducerBytes;
    record[hudicon::kTypeOffset] = 2;
    record[hudicon::kWidthOffset] = 0x2A;
    record[hudicon::kHeightOffset] = 0x33;
    hudicon::WriteHandle(record + hudicon::kHandleOffset, handle);

    Check(record[hudicon::kTypeOffset] == 2, "type byte survives the handle write");
    Check(record[hudicon::kWidthOffset] == 0x2A, "width byte survives the handle write");
    Check(record[hudicon::kHeightOffset] == 0x33, "height byte survives the handle write");
    Check(std::memcmp(record + hudicon::kHandleOffset, &handle, sizeof(handle)) == 0,
          "handle is stored verbatim");
    Check(hudicon::ReadHandle(record + hudicon::kHandleOffset) == handle, "handle round-trips");

    // A short record must not be read: the full handle lives inside the record
    // and nothing is written past it.
    Check(static_cast<unsigned char>(record[hudicon::kRecordBytes]) == 0x5C,
          "handle write stays inside the record");

    // Two records back to back, advanced by kRecordBytes exactly as the
    // renderer walks the text stream.
    char *record2 = second + hudicon::kIntroducerBytes;
    record2[hudicon::kTypeOffset] = 1;
    record2[hudicon::kWidthOffset] = 0x11;
    record2[hudicon::kHeightOffset] = 0x22;
    hudicon::WriteHandle(record2 + hudicon::kHandleOffset, reinterpret_cast<const void *>(static_cast<std::uintptr_t>(1)));
    Check(hudicon::ReadHandle(record2 + hudicon::kHandleOffset) == reinterpret_cast<const void *>(static_cast<std::uintptr_t>(1)),
          "second record round-trips at the next stride");
    Check(hudicon::ReadHandle(record + hudicon::kHandleOffset) == handle, "first record is unchanged by the second");

    if (failures)
    {
        std::printf("hud icon codec: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("hud icon codec: all checks passed\n");
    return 0;
}
