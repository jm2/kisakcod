// Census-only stub (scripts/ci/native64_census.py); never used by a real build.
// Force-included on winarm64. clang 18 types the ARM64 __prefetch builtin
// void(void *), while mingw-w64 and clang 19 use const void *. The clash
// fails <intrin.h> and every winnt.h PreFetchCacheLine call for a toolchain
// reason, so __prefetch names this const-correct declaration instead.
#pragma once
#ifdef __cplusplus
extern "C"
#endif
void __kisak_census_prefetch(const void *);
#define __prefetch __kisak_census_prefetch
