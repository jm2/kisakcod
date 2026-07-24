#include "buf_cursor.h"

#include <stdint.h>
#include <string.h>

namespace buf_cursor
{
namespace
{
// Thread-local active cursor. Loaders call Activate at entry and
// Deactivate at exit so the thread-local state is well-scoped.
thread_local BufCursor g_active{};
thread_local bool g_activeValid = false;

// The caller's *pos pointer that the cursor keeps in sync. Anchored
// by AnchorPos immediately after Activate so future Buf_Read<T> can
// write *pos back to the cursor's current without the caller having
// to remember to do it. Rolling back a transaction walks both back
// together so the cursor and the caller's *pos cannot disagree.
thread_local unsigned char **g_anchoredPos = nullptr;

// Internal: scan from current for a NUL terminator, bounded by end.
// Returns the string length (excluding NUL) on success, or SIZE_MAX
// on overrun. The bounded scan prevents an unbounded strlen-before-check
// loop from running the cursor past the end of the buffer.
size_t ScanBoundedStringLength(const unsigned char *current, const unsigned char *end, uint32_t maxLen)
{
    size_t len = 0;
    while (current + len < end && current[len] != 0)
    {
        if (len >= maxLen)
            return SIZE_MAX;
        ++len;
    }
    if (current + len >= end)
        return SIZE_MAX;
    return len;
}

// Sync the anchored *pos pointer to the cursor's current position so
// the two views cannot drift out of sync.
inline void SyncAnchoredPos()
{
    if (g_anchoredPos != nullptr)
    {
        *g_anchoredPos = const_cast<unsigned char *>(g_active.current);
    }
}
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
    g_anchoredPos = nullptr;
    return &g_active;
}

void Deactivate()
{
    g_activeValid = false;
    g_anchoredPos = nullptr;
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
        SyncAnchoredPos();
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
    if (!g_activeValid || g_active.failed)
    {
        return;
    }
    ptrdiff_t target = static_cast<ptrdiff_t>(g_active.current - g_active.begin) + delta;
    if (target < 0 || static_cast<size_t>(target) > static_cast<size_t>(g_active.end - g_active.begin))
    {
        g_active.failed = true;
        return;
    }
    g_active.current = g_active.begin + target;
    SyncAnchoredPos();
}

void AnchorPos(unsigned char **pos)
{
    if (!g_activeValid)
    {
        return;
    }
    g_anchoredPos = pos;
    if (pos != nullptr)
    {
        *pos = const_cast<unsigned char *>(g_active.current);
    }
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
    size_t len = ScanBoundedStringLength(g_active.current, g_active.end, g_active.maxStringLen);
    if (len == SIZE_MAX)
    {
        g_active.failed = true;
        return false;
    }
    if (len + 1 > outSize)
    {
        g_active.failed = true;
        return false;
    }
    std::memcpy(out, g_active.current, len);
    out[len] = '\0';
    g_active.current += len + 1;
    SyncAnchoredPos();
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
    uint16_t value;
    std::memcpy(&value, g_active.current, sizeof(uint16_t));
    g_active.current += sizeof(uint16_t);
    SyncAnchoredPos();
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
    uint8_t value = *g_active.current;
    g_active.current += sizeof(uint8_t);
    SyncAnchoredPos();
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
    uint16_t value;
    std::memcpy(&value, g_active.current, sizeof(uint16_t));
    g_active.current += sizeof(uint16_t);
    SyncAnchoredPos();
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
