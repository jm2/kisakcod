#pragma once
//
// sys_atomic.h - portable, fixed-width, sequentially-consistent atomics shim for
// the Win32 Interlocked* usage (197 real call sites across ~32 files).
//
// Windows Interlocked* are full sequentially-consistent barriers on x86/x64 (they
// compile to LOCK-prefixed read-modify-write). This header preserves those exact
// semantics on non-MSVC compilers by mapping the six Interlocked names onto the
// __atomic_* builtins with __ATOMIC_SEQ_CST.
//
// MSVC BYTE-IDENTITY: the entire mapping lives under `#if !defined(_MSC_VER)`, so
// on MSVC this header defines nothing atomic and the engine keeps resolving
// Interlocked* to the identical <windows.h>/<intrin.h> intrinsics it uses today.
// Including it in an engine TU on MSVC therefore emits no code - byte-identical.
//
// The macros are TYPE-GENERIC: each operand pointer binds at its real declared
// width (int32_t*, uint32_t*, void*, ...), so no unified operand type is needed.
// The only correctness rule that is easy to get wrong: Increment/Decrement return
// the NEW (post-op) value, while ExchangeAdd/Exchange/CompareExchange return the
// OLD (pre-op) value - matching Win32 exactly. See docs/PORTING.md section 8.
//
// Engine-wide adoption (adding this include next to the current <Windows.h> use in
// the atomics TUs) rides M3's platform-header decouple; today those TUs only build
// on MSVC, where this shim is inert. The portable compile-check in
// tests/abi_atomics_tests.cpp is the coverage for the non-MSVC path.

#include <universal/kisak_abi.h>

#if !defined(_MSC_VER)

// Full sequentially-consistent barrier == Windows Interlocked semantics. Do NOT
// relax to acquire/release/relaxed: that would change observable ordering versus
// the byte-identical MSVC reference and could surface as rare threading bugs in
// the lock-free FX allocator and the worker-command ring buffer.
#define InterlockedIncrement(p)          __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p)          __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd(p, v)     __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchange(p, v)        __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchangePointer(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)

// C11/__atomic compare-exchange differs from Win32: it takes (ptr, expected*,
// desired, weak, succ, fail), returns a bool, and writes the old value into
// *expected. This wrapper reproduces the Win32 contract - argument order
// (dest, exchange, comparand) and a return of the pre-op value. `expected` holds
// the actual old value on both the success and failure paths (on success it
// already equals `comparand`), so returning it is correct either way. The
// template deduces T from the destination pointer, so mixed operand widths bind.
template <class T, class E, class C>
inline T kisak_interlocked_cas(volatile T *dest, E exchange, C comparand)
{
    T expected = static_cast<T>(comparand);
    __atomic_compare_exchange_n(dest, &expected, static_cast<T>(exchange), false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
#define InterlockedCompareExchange(d, e, c) kisak_interlocked_cas((d), (e), (c))

#endif // !defined(_MSC_VER)
