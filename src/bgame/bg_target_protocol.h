#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

#include <universal/info_string.h>

namespace bg::target_protocol
{
constexpr int kMaxTargets = 32;
constexpr int kNoMaterial = -1;
constexpr int kMaxMaterialIndex = 127;
constexpr int kAttackProfileTop = 1;
constexpr int kJavelinOnly = 2;
constexpr int kKnownFlags = kAttackProfileTop | kJavelinOnly;

enum class ConfigParseError
{
    None,
    InvalidArgument,
    MalformedInfoString,
    MissingEntity,
    InvalidEntity,
    InvalidOffset,
    InvalidMaterial,
    InvalidOffscreenMaterial,
    InvalidFlags,
};

struct ParsedConfig
{
    int entityNumber;
    float offset[3];
    int materialIndex;
    int offscreenMaterialIndex;
    int flags;
};

namespace detail
{
enum class ValueLookup
{
    Missing,
    Found,
    Malformed,
};

inline ValueLookup LookupValue(
    const char *const info,
    const char *const key,
    const char **const value,
    std::size_t *const length) noexcept
{
    if (info_string::TryGetExactValueView(info, key, value, length))
        return ValueLookup::Found;
    return info_string::HasExactKey(info, key)
        ? ValueLookup::Malformed
        : ValueLookup::Missing;
}

inline bool TryParseIntView(
    const char *const value,
    const std::size_t length,
    int *const output) noexcept
{
    if (!value || length == 0 || !output)
        return false;

    int parsed = 0;
    const char *const end = value + length;
    const std::from_chars_result result =
        std::from_chars(value, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end)
        return false;

    *output = parsed;
    return true;
}

constexpr bool IsOffsetSeparator(const char value) noexcept
{
    return value == ' ' || value == '\t';
}

inline bool TryParseOffsetView(
    const char *value,
    const std::size_t length,
    float (&output)[3]) noexcept
{
    if (!value || length == 0)
        return false;

    const char *const end = value + length;
    float parsed[3]{};
    for (int component = 0; component < 3; ++component)
    {
        while (value != end && IsOffsetSeparator(*value))
            ++value;
        if (value == end)
            return false;

        const std::from_chars_result result = std::from_chars(
            value, end, parsed[component], std::chars_format::general);
        if (result.ec != std::errc{}
            || result.ptr == value
            || !std::isfinite(parsed[component]))
        {
            return false;
        }

        value = result.ptr;
        if (component != 2
            && (value == end || !IsOffsetSeparator(*value)))
        {
            return false;
        }
    }

    while (value != end && IsOffsetSeparator(*value))
        ++value;
    if (value != end)
        return false;

    for (int component = 0; component < 3; ++component)
        output[component] = parsed[component];
    return true;
}
} // namespace detail

// Parses one active CS_TARGETS entry without publishing partial output. The
// optional fields retain the retail defaults when absent or present-empty;
// duplicate keys and malformed or out-of-domain values are rejected.
inline ConfigParseError ParseConfig(
    const char *const info,
    const int maxEntityCount,
    ParsedConfig *const output) noexcept
{
    if (!info || !*info || maxEntityCount <= 0 || !output)
        return ConfigParseError::InvalidArgument;
    if (!info_string::IsWellFormed(info))
        return ConfigParseError::MalformedInfoString;

    ParsedConfig parsed{};
    parsed.materialIndex = kNoMaterial;
    parsed.offscreenMaterialIndex = kNoMaterial;

    const char *value = nullptr;
    std::size_t length = 0;
    detail::ValueLookup lookup =
        detail::LookupValue(info, "ent", &value, &length);
    if (lookup == detail::ValueLookup::Malformed)
        return ConfigParseError::MalformedInfoString;
    if (lookup == detail::ValueLookup::Missing || length == 0)
        return ConfigParseError::MissingEntity;
    if (!detail::TryParseIntView(value, length, &parsed.entityNumber)
        || parsed.entityNumber < 0
        || parsed.entityNumber >= maxEntityCount)
    {
        return ConfigParseError::InvalidEntity;
    }

    lookup = detail::LookupValue(info, "offs", &value, &length);
    if (lookup == detail::ValueLookup::Malformed)
        return ConfigParseError::MalformedInfoString;
    if (lookup == detail::ValueLookup::Found
        && length != 0
        && !detail::TryParseOffsetView(value, length, parsed.offset))
    {
        return ConfigParseError::InvalidOffset;
    }

    lookup = detail::LookupValue(info, "mat", &value, &length);
    if (lookup == detail::ValueLookup::Malformed)
        return ConfigParseError::MalformedInfoString;
    if (lookup == detail::ValueLookup::Found && length != 0
        && (!detail::TryParseIntView(
                value, length, &parsed.materialIndex)
            || parsed.materialIndex < kNoMaterial
            || parsed.materialIndex > kMaxMaterialIndex))
    {
        return ConfigParseError::InvalidMaterial;
    }

    lookup = detail::LookupValue(info, "offmat", &value, &length);
    if (lookup == detail::ValueLookup::Malformed)
        return ConfigParseError::MalformedInfoString;
    if (lookup == detail::ValueLookup::Found && length != 0
        && (!detail::TryParseIntView(
                value, length, &parsed.offscreenMaterialIndex)
            || parsed.offscreenMaterialIndex < kNoMaterial
            || parsed.offscreenMaterialIndex > kMaxMaterialIndex))
    {
        return ConfigParseError::InvalidOffscreenMaterial;
    }

    lookup = detail::LookupValue(info, "flags", &value, &length);
    if (lookup == detail::ValueLookup::Malformed)
        return ConfigParseError::MalformedInfoString;
    if (lookup == detail::ValueLookup::Found && length != 0
        && (!detail::TryParseIntView(value, length, &parsed.flags)
            || parsed.flags < 0
            || (parsed.flags & ~kKnownFlags) != 0))
    {
        return ConfigParseError::InvalidFlags;
    }

    *output = parsed;
    return ConfigParseError::None;
}

inline const char *ConfigParseErrorName(
    const ConfigParseError error) noexcept
{
    switch (error)
    {
    case ConfigParseError::None:
        return "none";
    case ConfigParseError::InvalidArgument:
        return "invalid argument";
    case ConfigParseError::MalformedInfoString:
        return "malformed info string";
    case ConfigParseError::MissingEntity:
        return "missing entity";
    case ConfigParseError::InvalidEntity:
        return "invalid entity";
    case ConfigParseError::InvalidOffset:
        return "invalid offset";
    case ConfigParseError::InvalidMaterial:
        return "invalid material";
    case ConfigParseError::InvalidOffscreenMaterial:
        return "invalid offscreen material";
    case ConfigParseError::InvalidFlags:
        return "invalid flags";
    }
    return "unknown error";
}
} // namespace bg::target_protocol
