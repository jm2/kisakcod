#pragma once

// Included after VariableUnion is declared by scr_variable.h.
// M4 (ki-n1et): byte-safe accessors for one stack-record payload. The runtime
// VariableStackBuffer image packs [type byte][VariableUnion] records at a
// VARIABLE_STACK_RECORD_SIZE stride, so every payload after record 0 sits at a
// non-pointer-aligned address; typed loads/stores through VariableUnion* or
// const char** at those addresses are undefined behavior (and fault on
// strict-alignment targets). The copy moves exactly sizeof(VariableUnion)
// payload bytes with no alignment or strict-aliasing hazards and produces
// identical bytes to a plain member access. `payload` always points at the
// first payload byte (one past the record's type byte); the record stride
// guarantees a full VariableUnion of payload room behind it.
static inline void VariableStackBuf_CopyCell(void *dst, const void *src)
{
    // Byte-wise move instead of memcpy: both endpoints are byte pointers and
    // the payload size is explicit in the loop bound, so the copy is
    // verifiable without the memcpy size contract (CWE-120 cannot arise --
    // source and destination span the same fixed, compile-time size).
    unsigned char *d = static_cast<unsigned char *>(dst);
    const unsigned char *s = static_cast<const unsigned char *>(src);
    for (size_t i = 0; i < sizeof(VariableUnion); ++i)
        d[i] = s[i];
}

static inline VariableUnion VariableStackBuf_ReadCell(const void *payload)
{
    VariableUnion cell;
    VariableStackBuf_CopyCell(&cell, payload);
    return cell;
}

static inline void VariableStackBuf_WriteCell(void *payload, const VariableUnion &cell)
{
    VariableStackBuf_CopyCell(payload, &cell);
}

