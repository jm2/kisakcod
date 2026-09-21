// SPDX-License-Identifier: GPL-3.0-only
//
// Internal helpers shared by the download protocol translation units
// (dl_http.cpp, dl_http_url.cpp, dl_http_parse.cpp). This header is not
// part of the public contract in dl_http.h: nothing outside those units
// may include it. Every helper is dependency-free and bounded -- no
// strlen on untrusted ranges and no unchecked copies -- so the protocol
// units stay analyzer-clean and behavior-preserving.

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>

namespace DlHttpInternal
{
// Case-insensitive ASCII equality between a bounded left range and an
// explicitly bounded right range; locale-independent by construction.
inline bool EqualsIgnoreCase(const char *const left,
    const std::uint32_t leftLength,
    const char *const right,
    const std::uint32_t rightLength) noexcept
{
    if (leftLength != rightLength)
        return false;
    for (std::uint32_t index = 0; index < leftLength; ++index)
    {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    }
    return true;
}

// Literal-pattern overload: the right bound comes from the string
// literal's own size, so no unbounded scan of the pattern is ever
// performed.
template <std::size_t N>
inline bool EqualsIgnoreCase(const char *const left,
    const std::uint32_t leftLength,
    const char (&right)[N]) noexcept
{
    static_assert(N > 0, "pattern must be a string literal");
    return EqualsIgnoreCase(left, leftLength, right,
        static_cast<std::uint32_t>(N - 1));
}

// Copies `length` raw bytes of `source` into `out` (capacity `capacity`)
// with the bounds check ahead of every written byte, then terminates the
// result. Returns false on overflow, leaving a terminated prefix. When
// `outLength` is non-null it receives the stored length on success.
inline bool CopyRaw(const char *const source,
    const std::uint32_t length,
    char *const out,
    const std::uint32_t capacity,
    std::uint32_t *const outLength = nullptr) noexcept
{
    if (length + 1 > capacity)
    {
        out[0] = '\0';
        return false;
    }
    for (std::uint32_t index = 0; index < length; ++index)
        out[index] = source[index];
    out[length] = '\0';
    if (outLength)
        *outLength = length;
    return true;
}
} // namespace DlHttpInternal
