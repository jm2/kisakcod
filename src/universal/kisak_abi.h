#pragma once
//
// kisak_abi.h - single ABI/target contract header for the KisakCOD port.
//
// COMPILE-TIME ONLY: this header defines nothing that emits code or data, so
// including it in any translation unit is byte-identical on the MSVC x86 build
// (the M1 invariant). It (1) pulls in the fixed-width integer types the port
// standardizes on, (2) derives target OS / arch / pointer-width macros from the
// compiler predefineds (CMake never passes these as -D; KISAK_PLATFORM is a
// CMake cache variable only), (3) provides the layout-freeze assert macros, and
// (4) folds in the calling-convention / alignment shim.
//
// It is the umbrella header: it INCLUDES platform_compat.h (the minimal
// calling-convention leaf) rather than the reverse, so platform_compat.h stays a
// dependency-free leaf that huffman.h and the portable tests keep including
// standalone. Ptr32<T> is deliberately NOT defined here - its single source of
// truth is disk32:: in <database/db_disk32.h> (the packed-mirror seed); on-disk
// mirror headers include both. See docs/PORTING.md sections 8-9.

#include <cstddef>
#include <cstdint>

#include <universal/platform_compat.h>

// ---- Target OS ----
#if defined(_WIN32)
#  define KISAK_OS_WINDOWS 1
#  define KISAK_TARGET_OS  "win32"
#elif defined(__APPLE__)
#  define KISAK_OS_MACOS   1
#  define KISAK_TARGET_OS  "macos"
#elif defined(__linux__)
#  define KISAK_OS_LINUX   1
#  define KISAK_TARGET_OS  "linux"
#else
#  error "kisak_abi.h: unsupported target OS"
#endif
#ifndef KISAK_OS_WINDOWS
#  define KISAK_OS_WINDOWS 0
#endif
#ifndef KISAK_OS_LINUX
#  define KISAK_OS_LINUX 0
#endif
#ifndef KISAK_OS_MACOS
#  define KISAK_OS_MACOS 0
#endif

// ---- Target architecture ----
#if defined(_M_IX86) || defined(__i386__)
#  define KISAK_ARCH_X86    1
#  define KISAK_TARGET_ARCH "x86"
#elif defined(_M_X64) || defined(__x86_64__)
#  define KISAK_ARCH_X64    1
#  define KISAK_TARGET_ARCH "x86_64"
#elif defined(_M_ARM64) || defined(__aarch64__)
#  define KISAK_ARCH_ARM64  1
#  define KISAK_TARGET_ARCH "arm64"
#elif defined(_M_ARM) || defined(__arm__)
#  define KISAK_ARCH_ARM    1
#  define KISAK_TARGET_ARCH "arm"
#else
#  error "kisak_abi.h: unsupported target architecture"
#endif
#ifndef KISAK_ARCH_X86
#  define KISAK_ARCH_X86 0
#endif
#ifndef KISAK_ARCH_X64
#  define KISAK_ARCH_X64 0
#endif
#ifndef KISAK_ARCH_ARM64
#  define KISAK_ARCH_ARM64 0
#endif
#ifndef KISAK_ARCH_ARM
#  define KISAK_ARCH_ARM 0
#endif

// ---- Pointer width (the axis the whole ABI port turns on) ----
// Derived from <cstdint> UINTPTR_MAX so it is correct on LLP64 (Win64) and LP64
// (Linux/macOS) alike; C `long` is deliberately never consulted for width.
#if UINTPTR_MAX == 0xFFFFFFFFu
#  define KISAK_PTR_BITS   32
#  define KISAK_ARCH_64BIT 0
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFull
#  define KISAK_PTR_BITS   64
#  define KISAK_ARCH_64BIT 1
#else
#  error "kisak_abi.h: unexpected pointer width"
#endif

// ---- Fixed-width integer policy ----
// Shared ABI / serialization / atomics code uses the <cstdint> exact-width types.
// C `long` / `unsigned long` are BANNED where width is load-bearing: they are
// 32-bit on Win32/Win64/WinARM64 (LLP64) but 64-bit on Linux/macOS (LP64). See
// docs/PORTING.md section 8.

// ---- Layout-freeze asserts ----
// COMPILE-TIME ONLY: each expands to a static_assert, which emits zero
// instructions and zero data, so adopting them keeps the MSVC x86 image
// byte-identical. This header (and any future layout_asserts.h) are the only
// files allowed to spell `sizeof(T) == ...`; the M4 CI tripwire must allowlist
// them.
//
//   ONDISK_SIZE / ONDISK_OFFSET   - on-disk / wire mirrors: freeze the ORIGINAL
//                                   32-bit size/offset on EVERY target.
//   RUNTIME_SIZE / RUNTIME_OFFSET  - runtime structs: assert the ILP32 value on
//                                   32-bit and the LP64/LLP64 value on 64-bit,
//                                   so drift is still caught on both builds.
#define ONDISK_SIZE(T, n) \
    static_assert(sizeof(T) == (n), #T " on-disk mirror size drift (must stay 32-bit-frozen)")
#define ONDISK_OFFSET(T, field, n) \
    static_assert(offsetof(T, field) == (n), #T "." #field " on-disk offset drift")
#if KISAK_ARCH_64BIT
#  define RUNTIME_SIZE(T, n32, n64) \
      static_assert(sizeof(T) == (n64), #T " runtime size drift (LP64/LLP64)")
#  define RUNTIME_OFFSET(T, field, n32, n64) \
      static_assert(offsetof(T, field) == (n64), #T "." #field " runtime offset drift (64-bit)")
#else
#  define RUNTIME_SIZE(T, n32, n64) \
      static_assert(sizeof(T) == (n32), #T " runtime size drift (ILP32)")
#  define RUNTIME_OFFSET(T, field, n32, n64) \
      static_assert(offsetof(T, field) == (n32), #T "." #field " runtime offset drift (32-bit)")
#endif
