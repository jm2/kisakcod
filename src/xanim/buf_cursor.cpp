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

// Nested-activation save stack. Production loaders nest (XModelLoadFile
// -> XModelPartsPrecache / XModelSurfsPrecache, XModelPiecesLoadFile ->
// nested model registration), and each nesting level Activates its own
// cursor. Activate pushes the live scope so the nested parse cannot
// destroy the parent's position, anchor, limits or failure state, and
// Deactivate pops it back. The depth bound covers any real call graph
// many times over (deepest audited production nesting is 3); on
// overflow the nested cursor is installed pre-failed so the nested
// parse rejects through the ordinary malformed-input path instead of
// silently corrupting the parent scope. When that scope unwinds, the
// overflowed caller's own cursor state is gone, so its Deactivate
// leaves a pre-failed active cursor: the caller stays fail-closed
// (Failed() latched, bounded zero reads) until its own Deactivate
// pops the nearest pushed ancestor.
//
// Only scopes that pushed a parent save occupy a stack slot, so the
// slots are a strict prefix of the open-scope chain: while an overflow
// scope is open its 16 save-pushing ancestors are open too, which pins
// the depth at kMaxSavedScopes and forces every deeper activation to
// overflow as well. Deactivate exploits that shape to identify the
// innermost scope's kind exactly — a scope pops its own frame or
// touches nothing, so LIFO push/pop accounting is exact by
// construction.
struct SavedCursorScope
{
    BufCursor state;
    unsigned char **anchoredPos;
};

constexpr size_t kMaxSavedScopes = 16;
thread_local SavedCursorScope g_scopeStack[kMaxSavedScopes] = {};
thread_local size_t g_scopeDepth = 0;

// Open scopes that could NOT push a parent save because the stack was
// full at their Activate (all other open scopes either pushed a save or
// are the top-level scope). The innermost scope is one of these exactly
// when the count is nonzero, so its Deactivate pops nothing. The
// overwritten parent state is unrecoverable, but the parent must not
// fall back to an unbounded cursor either: Deactivate leaves a
// pre-failed cursor active so the parent's remaining reads return
// zeros, Failed() stays true, and the parent unwinds through the
// ordinary malformed-input path while every pushed ancestor still pops
// exactly its own frame.
thread_local size_t g_deepOverflow = 0;

// Degenerate non-null buffer domain for the pre-failed cursor left
// active after an overflow scope unwinds. The overflowed caller's real
// cursor state is unrecoverable, but keeping the replacement on a
// valid, empty, addressable window keeps every remaining bounds check
// and anchored *pos write-back on a real (never null) pointer — no
// code path can end up doing pointer arithmetic on null, and no
// caller's *pos is ever handed nullptr while it keeps parsing.
constexpr unsigned char kOverflowParentDomain[1] = {0};

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
    if (g_activeValid)
    {
        // Nested activation: explicitly preserve the parent scope so the
        // nested parse cannot destroy the outer cursor's anchored *pos,
        // position, domain limits or failure state. The matching
        // Deactivate restores exactly this state (LIFO — every loader
        // Activates at entry and Deactivates on every exit).
        if (g_scopeDepth >= kMaxSavedScopes)
        {
            // Fail closed on pathological nesting depth: install the new
            // cursor but pre-fail it so every read returns zeros and the
            // nested parse unwinds through its ordinary malformed-input
            // path. The scope does not push a parent save (tracked in
            // g_deepOverflow so the matching Deactivate keeps LIFO
            // accounting exact and leaves the overflowed caller a
            // pre-failed ACTIVE cursor — see Deactivate).
            ++g_deepOverflow;
            g_active.begin = buf;
            g_active.current = buf;
            g_active.end = buf + size;
            g_active.txnCheckpoint = nullptr;
            g_active.maxBoneIdx = 0xFFFFFFFFu;
            g_active.maxWeightIdx = 0xFFFFFFFFu;
            g_active.maxTriIdx = 0xFFFFFFFFu;
            g_active.maxStringLen = 0xFFFFFFFFu;
            g_active.failed = true;
            g_activeValid = true;
            g_anchoredPos = nullptr;
            return &g_active;
        }
        g_scopeStack[g_scopeDepth].state = g_active;
        g_scopeStack[g_scopeDepth].anchoredPos = g_anchoredPos;
        ++g_scopeDepth;
    }
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
    if (g_deepOverflow > 0)
    {
        // This scope never pushed a parent save (the save stack was
        // full at its Activate), so there is nothing to pop — and the
        // overflowed caller's live cursor state was destroyed by the
        // overflow install, so it cannot be restored either. Dropping
        // the thread-local to inactive here would hand the still-
        // running caller back to the unbounded legacy read with
        // Failed() reporting false, defeating the overflow path's
        // fail-closed purpose. Instead leave a pre-failed, empty-but-
        // ACTIVE cursor: the caller's remaining reads return zeros,
        // Failed() stays latched, and the caller unwinds through its
        // ordinary malformed-input path; every pushed ancestor still
        // pops exactly its own frame below.
        --g_deepOverflow;
        g_active.begin = kOverflowParentDomain;
        g_active.current = kOverflowParentDomain;
        g_active.end = kOverflowParentDomain;
        g_active.txnCheckpoint = nullptr;
        g_active.maxBoneIdx = 0xFFFFFFFFu;
        g_active.maxWeightIdx = 0xFFFFFFFFu;
        g_active.maxTriIdx = 0xFFFFFFFFu;
        g_active.maxStringLen = 0xFFFFFFFFu;
        g_active.failed = true;
        g_activeValid = true;
        g_anchoredPos = nullptr;
        return;
    }
    if (g_scopeDepth > 0)
    {
        // Restore the parent scope exactly: position, anchor, limits and
        // failure flag. Re-asserting the anchor writes the restored
        // position back through the parent's *pos so the two views
        // continue in lockstep (and heals any transient raw-pointer
        // desync the parent picked up before the nested activation).
        --g_scopeDepth;
        g_active = g_scopeStack[g_scopeDepth].state;
        g_anchoredPos = g_scopeStack[g_scopeDepth].anchoredPos;
        g_activeValid = true;
        SyncAnchoredPos();
        return;
    }
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

const unsigned char *Tell()
{
    if (!g_activeValid)
    {
        return nullptr;
    }
    return g_active.current;
}

bool SeekTo(const unsigned char *target)
{
    if (!g_activeValid || g_active.failed)
    {
        return false;
    }
    if (target < g_active.begin || target > g_active.end)
    {
        // A checkpoint outside the active buffer means the caller's
        // saved position is stale or corrupt. Latch failed so the
        // caller's ordinary malformed-input cleanup runs; the position
        // does not move.
        g_active.failed = true;
        SyncAnchoredPos();
        return false;
    }
    g_active.current = target;
    SyncAnchoredPos();
    return true;
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

bool ReadBytes(void *const out, const size_t outCapacity, const size_t byteCount)
{
    if (out == nullptr || outCapacity == 0 || !g_activeValid)
        return false;

    if (g_active.failed
        || byteCount > outCapacity
        || g_active.current + byteCount > g_active.end)
    {
        // Deterministic zero state: a failed bulk read must leave the
        // destination free of uninitialized bytes because load-object
        // callers keep parsing (with zero-valued reads) instead of
        // unwinding.
        const size_t zeroBytes = byteCount < outCapacity ? byteCount : outCapacity;
        std::memset(out, 0, zeroBytes);
        g_active.failed = true;
        SyncAnchoredPos();
        return false;
    }

    std::memcpy(out, g_active.current, byteCount);
    g_active.current += byteCount;
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
