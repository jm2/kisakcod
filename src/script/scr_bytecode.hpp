#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <universal/kisak_abi.h>

// Runtime bytecode embeds native pointers; archive scalar tokens stay separate.
// Code operands need not be aligned, so access them through object-byte copies.
template <typename T> inline T Scr_ReadBytecodeValue(const void *source)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T value;
    std::memcpy(&value, source, sizeof(value));
    return value;
}
template <typename T> inline void Scr_WriteBytecodeValue(void *destination, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::memcpy(destination, &value, sizeof(value));
}
struct ScrSwitchCase
{
    uintptr_t name;
    const char *codePos;
};
RUNTIME_SIZE(ScrSwitchCase, 8, 16);
inline int Scr_CompareSwitchCases(const void *a, const void *b)
{
    const uintptr_t left = Scr_ReadBytecodeValue<uintptr_t>(a);
    const uintptr_t right = Scr_ReadBytecodeValue<uintptr_t>(b);
    return (left < right) - (left > right);
}
inline void Scr_SkipSwitchCases(const char **position, unsigned int count)
{
    *position += sizeof(ScrSwitchCase) * count;
}
