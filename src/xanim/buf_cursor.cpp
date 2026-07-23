#include "buf_cursor.h"

#include <stdint.h>

namespace buf_cursor
{
namespace
{
// Thread-local active cursor. Loaders call Activate at entry and
// Deactivate at exit so the thread-local state is well-scoped.
thread_local BufCursor g_active{};
thread_local bool g_activeValid = false;
}  // namespace

BufCursor *Activate(const unsigned char *buf, size_t size)
{
    g_active.begin = buf;
    g_active.current = buf;
    g_active.end = buf + size;
    g_active.txnCheckpoint = nullptr;
    g_active.maxBoneIdx = 0xFFFFFFFFu;
    g_active.maxWeightIdx = 0xFFFFFFFFu;
    g_active.maxTriIdx = 0xFFFFFFFFu;
    g_active.maxStringLen = 0xFFFFFFFFu;
    g_active.failed = false;
    g_activeValid = true;
    return &g_active;
}

void Deactivate()
{
    g_activeValid = false;
    g_active = BufCursor{};
}

bool Failed()
{
    return g_activeValid && g_active.failed;
}

void Fail()
{
    if (g_activeValid)
    {
        g_active.failed = true;
    }
}

void Begin()
{
    if (g_activeValid)
    {
        g_active.txnCheckpoint = g_active.current;
    }
}

bool Commit()
{
    if (!g_activeValid)
    {
        return false;
    }
    bool ok = !g_active.failed;
    g_active.txnCheckpoint = nullptr;
    return ok;
}

void Rollback()
{
    if (!g_activeValid)
    {
        return;
    }
    if (g_active.txnCheckpoint != nullptr)
    {
        g_active.current = g_active.txnCheckpoint;
        g_active.failed = false;
        g_active.txnCheckpoint = nullptr;
    }
}

void SetBoneLimit(uint32_t maxBoneIdx)
{
    if (g_activeValid)
    {
        g_active.maxBoneIdx = maxBoneIdx;
    }
}

void SetWeightLimit(uint32_t maxWeightIdx)
{
    if (g_activeValid)
    {
        g_active.maxWeightIdx = maxWeightIdx;
    }
}

void SetTriLimit(uint32_t maxTriIdx)
{
    if (g_activeValid)
    {
        g_active.maxTriIdx = maxTriIdx;
    }
}

void SetStringLimit(uint32_t maxStringLen)
{
    if (g_activeValid)
    {
        g_active.maxStringLen = maxStringLen;
    }
}

void Advance(ptrdiff_t delta)
{
    if (!g_activeValid)
    {
        return;
    }
    const unsigned char *next = g_active.current + delta;
    if (next > g_active.end)
    {
        g_active.failed = true;
        return;
    }
    if (next < g_active.begin)
    {
        g_active.failed = true;
        return;
    }
    g_active.current = next;
}

bool ReadString(char *out, size_t outSize)
{
    if (!g_activeValid || g_active.failed)
    {
        return false;
    }
    if (outSize == 0)
    {
        return false;
    }
    const unsigned char *p = g_active.current;
    size_t len = 0;
    while (p + len < g_active.end && p[len] != 0)
    {
        ++len;
    }
    if (p + len >= g_active.end)
    {
        g_active.failed = true;
        return false;
    }
    if (len >= outSize)
    {
        g_active.failed = true;
        return false;
    }
    if (len > g_active.maxStringLen)
    {
        g_active.failed = true;
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        out[i] = static_cast<char>(p[i]);
    }
    out[len] = '\0';
    g_active.current = p + len + 1;
    return true;
}

uint16_t ReadBone()
{
    if (!g_activeValid || g_active.failed)
    {
        return 0;
    }
    if (g_active.current + sizeof(uint16_t) > g_active.end)
    {
        g_active.failed = true;
        return 0;
    }
    uint16_t value = *(reinterpret_cast<const uint16_t *>(g_active.current));
    g_active.current += sizeof(uint16_t);
    if (value >= g_active.maxBoneIdx)
    {
        g_active.failed = true;
        return value;
    }
    return value;
}

uint8_t ReadWeight()
{
    if (!g_activeValid || g_active.failed)
    {
        return 0;
    }
    if (g_active.current + sizeof(uint8_t) > g_active.end)
    {
        g_active.failed = true;
        return 0;
    }
    uint8_t value = *(g_active.current);
    g_active.current += sizeof(uint8_t);
    if (value >= g_active.maxWeightIdx)
    {
        g_active.failed = true;
        return value;
    }
    return value;
}

uint16_t ReadTri(uint16_t vertCount)
{
    if (!g_activeValid || g_active.failed)
    {
        return 0;
    }
    if (g_active.current + sizeof(uint16_t) > g_active.end)
    {
        g_active.failed = true;
        return 0;
    }
    uint16_t value = *(reinterpret_cast<const uint16_t *>(g_active.current));
    g_active.current += sizeof(uint16_t);
    if (vertCount > 0 && value >= vertCount)
    {
        g_active.failed = true;
        return value;
    }
    if (value >= g_active.maxTriIdx)
    {
        g_active.failed = true;
        return value;
    }
    return value;
}

const BufCursor *Current()
{
    if (!g_activeValid)
    {
        return nullptr;
    }
    return &g_active;
}
}  // namespace buf_cursor
