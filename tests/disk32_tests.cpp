#include <database/db_disk32.h>

#include <cstdio>

namespace
{
int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}
}

int main()
{
    static_assert(sizeof(disk32::PointerToken) == 4);

    const uint32_t sizes[] = {64, 128, 256};
    disk32::DecodedOffset decoded{};

    // Encoded offsets are one-based: high nibble is the block and low 28 bits
    // are the byte offset after subtracting one.
    if (!disk32::DecodeOffset({(1u << 28) + 17u}, sizes, 3, 4, &decoded))
        return fail("valid disk32 offset was rejected");
    if (decoded.block != 1 || decoded.offset != 16)
        return fail("disk32 offset decoded incorrectly");

    if (disk32::DecodeOffset({0}, sizes, 3, 1, &decoded))
        return fail("null token was accepted as an offset");
    if (disk32::DecodeOffset({disk32::kInline}, sizes, 3, 1, &decoded))
        return fail("inline sentinel was accepted as an offset");
    if (disk32::DecodeOffset({(3u << 28) + 1u}, sizes, 3, 1, &decoded))
        return fail("out-of-range block was accepted");
    if (disk32::DecodeOffset({(1u << 28) + 127u}, sizes, 3, 4, &decoded))
        return fail("out-of-range byte span was accepted");

    return 0;
}
