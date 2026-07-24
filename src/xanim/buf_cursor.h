#pragma once

#include <cstring>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// buf_cursor: transactional current/end cursor that bounds every
// Buf_Read<T> in src/xanim/{xanim,xmodel}_load_obj.cpp and friends.
//
// The original Buf_Read<T>(unsigned char **pos) in src/qcommon/qcommon.h
// is an unbounded T copy — it advances the caller's pointer with no
// notion of the buffer's end. The cursor replaces that with a stateful
// reader that knows the buffer's end, the active bone / weight /
// triangle indices, and a transactional checkpoint. The signature of
// Buf_Read<T> is preserved so the 114 call sites still compile, but the
// implementation routes through the cursor when one is active and falls
// back to the original unbounded read otherwise (so legacy callers and
// tests that never activate a cursor keep working).
//
// The cursor is intentionally thread-local: the existing Buf_Read<T>(T**)
// API is single-threaded per parse, and the loaders always run on the
// main thread. Activate + Deactivate bracket each top-level parse so the
// thread-local state is well-scoped.
//
// Cursor-vs-raw-pointer desynchronization guard.
//
// Buf_Read<T> writes *pos in addition to the cursor's internal current.
// Activate initializes the cursor's current to the buffer's start and
// assumes the caller passes a pos that points at the same byte. Loaders
// that want to fast-forward past a header (e.g. XModelParts classification
// or the useBones flag) must do so through Cursor_Advance, which updates
// both the cursor and the *pos pointer that the cursor owns. Rollback
// also walks both back together so any subsequent Buf_Read<T> cannot
// read past the checkpoint.
//
// UBSan alignment hazard.
//
// The original Buf_Read<T> uses *reinterpret_cast<const T *>(*pos) which
// is a misaligned load for T=float on pointers that are not 4-byte
// aligned. The production loader buffers are file-backed and may not
// have natural alignment for any T larger than 1 byte. The cursor's
// Buf_Read<T> dispatch reads through std::memcpy so the access is
// well-defined without changing the ABI.
namespace buf_cursor
{
struct BufCursor
{
    const unsigned char *begin;
    const unsigned char *current;
    const unsigned char *end;
    const unsigned char *txnCheckpoint;
    uint32_t maxBoneIdx;
    uint32_t maxWeightIdx;
    uint32_t maxTriIdx;
    uint32_t maxStringLen;
    bool failed;
};

// Activate a fresh cursor over [buf, buf + size) and make it the active
// thread-local cursor. Returns a non-owning pointer to the cursor so the
// caller can inspect failure state, set domain limits, or perform
// transactional reads. The pointer is valid until Deactivate is called
// or another activation replaces it.
BufCursor *Activate(const unsigned char *buf, size_t size);

// Tear down the active cursor and clear the thread-local. After this
// returns Buf_Read<T> falls back to the original unbounded read until
// another Activate call re-establishes the cursor.
void Deactivate();

// True when the current active cursor has failed a bounds check. Loaders
// polling this between header fields get the same answer whether the
// cursor saw the failure or an upstream helper marked it intentionally.
bool Failed();

// Mark the cursor as failed even if no bounds check tripped. Use this
// when an upstream loader validates domain semantics (e.g. "bone count
// cannot be zero") and wants to abort the rest of the read.
void Fail();

// Transactional control. Begin records the current position. Commit
// releases the checkpoint (the read sequence is now permanent).
// Rollback rewinds both the cursor and the *pos pointer that the cursor
// owns back to the checkpoint and clears the failed flag if the failure
// was recorded during the in-flight transaction.
void Begin();
bool Commit();
void Rollback();

// Domain limit setters. The limits are checked by the typed ReadBone /
// ReadWeight / ReadTri helpers below. Set them once after Activate and
// before any reads issue; tightening mid-stream is allowed but the
// cursor does not retroactively re-validate prior reads.
void SetBoneLimit(uint32_t maxBoneIdx);
void SetWeightLimit(uint32_t maxWeightIdx);
void SetTriLimit(uint32_t maxTriIdx);
void SetStringLimit(uint32_t maxStringLen);

// Direct cursor advance. Mirrors the legacy `pos += count` pattern but
// routes through the active cursor so an overrun marks the cursor as
// failed instead of silently walking off the end. Compares against end
// before updating so the cursor cannot Advance past the buffer.
void Advance(ptrdiff_t delta);

// String read: scan from current until a NUL is observed, copy
// (including NUL) into out, and advance. Returns false and marks the
// cursor failed if no NUL is found before end, the string is longer
// than maxStringLen (when set), or out is too small. The scan is
// bounded by end so an attacker-controlled buffer cannot cause an
// unbounded strlen-before-check loop.
bool ReadString(char *out, size_t outSize);

// Typed helpers that read through the cursor and apply a domain bound.
// A failed read leaves the cursor at its current position (it does NOT
// rewind — the caller's transaction rollback is the canonical
// recovery). Each helper forwards the count-style bounds check from
// Buf_Read<T> first.
uint16_t ReadBone();
uint8_t ReadWeight();
uint16_t ReadTri(uint16_t vertCount);

// Anchor the cursor to a caller's *pos pointer. The cursor "owns" the
// pointer in the sense that future Buf_Read<T> calls will write through
// it: that way the cursor's current and the caller's *pos can never
// drift out of sync. Anchoring must happen immediately after Activate
// and before any reads.
void AnchorPos(unsigned char **pos);

// Active cursor accessor for callers that want to introspect begin /
// current / end. Returns nullptr if no cursor is active.
const BufCursor *Current();
}  // namespace buf_cursor

// Buf_Read<T> overloaded — when a buf_cursor::BufCursor is active, the
// read enforces the count-style bounds check; otherwise it falls back
// to the original unbounded behavior so legacy callers and tests that
// never activate a cursor continue to work. Reads go through
// std::memcpy so the load is well-defined regardless of the underlying
// pointer's natural alignment, which prevents UBSan from flagging a
// misaligned float load on file-backed buffers.
template <typename T>
inline T Buf_Read(unsigned char **pos)
{
    const buf_cursor::BufCursor *cursor = buf_cursor::Current();
    if (cursor != nullptr)
    {
        if (cursor->failed)
        {
            if (pos != nullptr)
                *pos = const_cast<unsigned char *>(cursor->current);
            return static_cast<T>(0);
        }
        if (cursor->current + sizeof(T) > cursor->end)
        {
            const_cast<buf_cursor::BufCursor *>(cursor)->failed = true;
            if (pos != nullptr)
                *pos = const_cast<unsigned char *>(cursor->current);
            return static_cast<T>(0);
        }
        T value;
        std::memcpy(&value, cursor->current, sizeof(T));
        const_cast<buf_cursor::BufCursor *>(cursor)->current += sizeof(T);
        if (pos != nullptr)
            *pos = const_cast<unsigned char *>(cursor->current);
        return value;
    }
    T value;
    std::memcpy(&value, *pos, sizeof(T));
    *pos += sizeof(T);
    return value;
}
