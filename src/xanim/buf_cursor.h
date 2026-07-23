#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// BufCursor: transactional current/end cursor that bounds every Buf_Read<T>.
//
// The cursor replaces the original unbounded Buf_Read<T>(unsigned char **pos)
// with a stateful reader that knows the buffer's end, the active bone /
// weight / triangle indices, and a transactional checkpoint. Loaders
// activate a cursor at the buffer's start; subsequent Buf_Read<T> calls
// advance the cursor and enforce a count-style "have enough bytes left"
// check. Loaders can also wrap speculative reads in a transaction so a
// failed bound rolls the cursor back to the checkpoint instead of
// leaving the stream half-consumed.
//
// The cursor is thread-local so existing call sites that take
// `unsigned char **pos` continue to compile. The cursor is intentionally
// not thread-affine; Activate + Deactivate bracket each top-level loader.

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

// True when the current active cursor has failed a bounds check.
bool Failed();

// Mark the cursor as failed even if no bounds check tripped. Use this
// when an upstream loader validates domain semantics (e.g. "bone count
// cannot be zero") and wants to abort the rest of the read.
void Fail();

// Transactional control: Begin records the current position. Commit
// releases the checkpoint (the read sequence is now permanent).
// Rollback rewinds to the checkpoint and clears the failed flag if the
// failure was recorded during the in-flight transaction.
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
// failed instead of silently walking off the end.
void Advance(ptrdiff_t delta);

// String read: scan from current until a NUL is observed, copy
// (including NUL) into out, and advance. Returns false and marks the
// cursor failed if no NUL is found before end, or the string is longer
// than maxStringLen (when set), or out is too small.
bool ReadString(char *out, size_t outSize);

// Typed helpers that read through the cursor and apply a domain bound.
// A failed read leaves the cursor at its current position (it does NOT
// rewind — the caller's transaction rollback is the canonical
// recovery). Each helper forwards the count-style bounds check from
// Buf_Read<T> first.
uint16_t ReadBone();
uint8_t ReadWeight();
uint16_t ReadTri(uint16_t vertCount);

// Active cursor accessor for callers that want to introspect begin /
// current / end. Returns nullptr if no cursor is active.
const BufCursor *Current();
}  // namespace buf_cursor

// Buf_Read<T> replacement. The original signature (Buf_Read<T>(unsigned
// char **pos)) is preserved so every existing call site compiles
// unchanged. When a buf_cursor::BufCursor is active, the read enforces
// the count-style bounds check; otherwise it falls back to the original
// unbounded behavior so legacy callers and tests that never activate a
// cursor continue to work.
template <typename T>
inline T Buf_Read(unsigned char **pos)
{
    const buf_cursor::BufCursor *cursor = buf_cursor::Current();
    if (cursor != nullptr)
    {
        if (cursor->failed)
        {
            return static_cast<T>(0);
        }
        if (cursor->current + sizeof(T) > cursor->end)
        {
            const_cast<buf_cursor::BufCursor *>(cursor)->failed = true;
            return static_cast<T>(0);
        }
        T value = *(reinterpret_cast<const T *>(cursor->current));
        const_cast<buf_cursor::BufCursor *>(cursor)->current += sizeof(T);
        *pos = const_cast<unsigned char *>(cursor->current);
        return value;
    }
    T value = *(reinterpret_cast<const T *>(*pos));
    *pos += sizeof(T);
    return value;
}
