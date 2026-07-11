#pragma once
//
// sys_atomic.h - fixed-width, sequentially consistent atomics for shared
// engine state that cannot become std::atomic without changing frozen layouts.
//
// The canonical Sys_Atomic* API is available on every supported compiler. MSVC
// uses the unsuffixed _Interlocked* intrinsics from <intrin.h>; GCC and Clang
// use __atomic_* with __ATOMIC_SEQ_CST. The unsuffixed MSVC operations and the
// selected GNU memory order are both full barriers.
//
// Only aligned int32_t/uint32_t storage is accepted by the word operations.
// This is intentional: C long is 32-bit on Windows but 64-bit on Linux/macOS,
// and accepting it would silently preserve the exact LP64 bug this boundary is
// meant to remove. Increment/Decrement return the NEW value; FetchAdd,
// Exchange, and CompareExchange return the OLD value. CompareExchange keeps
// the Win32 argument order (destination, exchange, comparand).
//
// Existing non-MSVC Interlocked* aliases remain at the bottom only as a
// migration aid for engine translation units not yet moved to Sys_Atomic*.
// They are deliberately not defined on MSVC, where those names belong to the
// Windows SDK. Delete the aliases after the final call-site migration.

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <universal/kisak_abi.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define KISAK_ATOMIC_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define KISAK_ATOMIC_INLINE inline __attribute__((always_inline))
#else
#define KISAK_ATOMIC_INLINE inline
#endif

namespace kisak_atomic_detail
{
template <class T>
KISAK_ATOMIC_INLINE void require_word() noexcept
{
    using Word = std::remove_cv_t<T>;
    static_assert(std::is_same_v<Word, std::int32_t>
                      || std::is_same_v<Word, std::uint32_t>,
                  "Sys_Atomic word storage must be int32_t or uint32_t");
    static_assert(alignof(Word) >= 4, "Sys_Atomic word storage must be 4-byte aligned");
}

#if defined(_MSC_VER)
static_assert(std::numeric_limits<unsigned long>::digits == 32,
              "MSVC _Interlocked word must match int32_t");

template <class T>
KISAK_ATOMIC_INLINE volatile long *as_msvc_word(volatile T *value) noexcept
{
    require_word<T>();
    return reinterpret_cast<volatile long *>(value);
}

template <class T>
KISAK_ATOMIC_INLINE volatile long *as_msvc_word(const volatile T *value) noexcept
{
    require_word<T>();
    return reinterpret_cast<volatile long *>(
        const_cast<volatile T *>(value));
}

template <class T>
KISAK_ATOMIC_INLINE long to_msvc_word(T value) noexcept
{
    require_word<T>();
    return std::bit_cast<long>(value);
}

template <class T>
KISAK_ATOMIC_INLINE T from_msvc_word(long value) noexcept
{
    require_word<T>();
    return std::bit_cast<T>(value);
}
#endif
} // namespace kisak_atomic_detail

template <class T, class E, class C>
KISAK_ATOMIC_INLINE T Sys_AtomicCompareExchange(
    volatile T *destination,
    E exchange,
    C comparand) noexcept
{
    kisak_atomic_detail::require_word<T>();
    const T exchangeWord = static_cast<T>(exchange);
    const T comparandWord = static_cast<T>(comparand);
#if defined(_MSC_VER)
    const long oldValue = _InterlockedCompareExchange(
        kisak_atomic_detail::as_msvc_word(destination),
        kisak_atomic_detail::to_msvc_word(exchangeWord),
        kisak_atomic_detail::to_msvc_word(comparandWord));
    return kisak_atomic_detail::from_msvc_word<T>(oldValue);
#else
    T expected = comparandWord;
    __atomic_compare_exchange_n(destination, &expected, exchangeWord, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
#endif
}

template <class T>
KISAK_ATOMIC_INLINE T Sys_AtomicLoad(const volatile T *source) noexcept
{
    kisak_atomic_detail::require_word<T>();
    return Sys_AtomicCompareExchange(
        const_cast<volatile T *>(source), T{}, T{});
}

template <class T, class V>
KISAK_ATOMIC_INLINE void Sys_AtomicStore(volatile T *destination, V value) noexcept
{
    kisak_atomic_detail::require_word<T>();
    const T wordValue = static_cast<T>(value);
#if defined(_MSC_VER)
    (void)_InterlockedExchange(
        kisak_atomic_detail::as_msvc_word(destination),
        kisak_atomic_detail::to_msvc_word(wordValue));
#else
    (void)__atomic_exchange_n(destination, wordValue, __ATOMIC_SEQ_CST);
#endif
}

template <class T>
KISAK_ATOMIC_INLINE T Sys_AtomicIncrement(volatile T *value) noexcept
{
    kisak_atomic_detail::require_word<T>();
#if defined(_MSC_VER)
    return kisak_atomic_detail::from_msvc_word<T>(
        _InterlockedIncrement(kisak_atomic_detail::as_msvc_word(value)));
#else
    return __atomic_add_fetch(value, static_cast<T>(1), __ATOMIC_SEQ_CST);
#endif
}

template <class T>
KISAK_ATOMIC_INLINE T Sys_AtomicDecrement(volatile T *value) noexcept
{
    kisak_atomic_detail::require_word<T>();
#if defined(_MSC_VER)
    return kisak_atomic_detail::from_msvc_word<T>(
        _InterlockedDecrement(kisak_atomic_detail::as_msvc_word(value)));
#else
    return __atomic_sub_fetch(value, static_cast<T>(1), __ATOMIC_SEQ_CST);
#endif
}

template <class T, class V>
KISAK_ATOMIC_INLINE T Sys_AtomicFetchAdd(volatile T *destination, V value) noexcept
{
    kisak_atomic_detail::require_word<T>();
    const T wordValue = static_cast<T>(value);
#if defined(_MSC_VER)
    return kisak_atomic_detail::from_msvc_word<T>(
        _InterlockedExchangeAdd(
            kisak_atomic_detail::as_msvc_word(destination),
            kisak_atomic_detail::to_msvc_word(wordValue)));
#else
    return __atomic_fetch_add(destination, wordValue, __ATOMIC_SEQ_CST);
#endif
}

template <class T, class V>
KISAK_ATOMIC_INLINE T Sys_AtomicExchange(volatile T *destination, V value) noexcept
{
    kisak_atomic_detail::require_word<T>();
    const T wordValue = static_cast<T>(value);
#if defined(_MSC_VER)
    return kisak_atomic_detail::from_msvc_word<T>(
        _InterlockedExchange(
            kisak_atomic_detail::as_msvc_word(destination),
            kisak_atomic_detail::to_msvc_word(wordValue)));
#else
    return __atomic_exchange_n(destination, wordValue, __ATOMIC_SEQ_CST);
#endif
}

template <class T>
KISAK_ATOMIC_INLINE T *Sys_AtomicExchangePointer(
    T *volatile *destination,
    T *value) noexcept
{
#if defined(_MSC_VER)
    return static_cast<T *>(_InterlockedExchangePointer(
        reinterpret_cast<void *volatile *>(destination),
        static_cast<void *>(value)));
#else
    return __atomic_exchange_n(destination, value, __ATOMIC_SEQ_CST);
#endif
}

#if !defined(_MSC_VER)
#define InterlockedIncrement(p) Sys_AtomicIncrement((p))
#define InterlockedDecrement(p) Sys_AtomicDecrement((p))
#define InterlockedExchangeAdd(p, v) Sys_AtomicFetchAdd((p), (v))
#define InterlockedExchange(p, v) Sys_AtomicExchange((p), (v))
#define InterlockedExchangePointer(p, v) Sys_AtomicExchangePointer((p), (v))
#define InterlockedCompareExchange(d, e, c) Sys_AtomicCompareExchange((d), (e), (c))
#endif

#undef KISAK_ATOMIC_INLINE
