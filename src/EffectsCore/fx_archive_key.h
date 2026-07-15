#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

namespace fx::archive
{
// An archive-local effect-definition identity.  This is neither a native
// pointer nor a fast-file PointerToken: every nonzero 32-bit value is legal,
// including the values reserved by disk32 for inline fast-file objects.
struct EffectDefinitionKey32 final
{
    std::uint32_t value = 0;

    constexpr EffectDefinitionKey32() noexcept = default;
    explicit constexpr EffectDefinitionKey32(
        const std::uint32_t key) noexcept
        : value(key)
    {
    }
};

ONDISK_SIZE(EffectDefinitionKey32, 4);
static_assert(alignof(EffectDefinitionKey32) == 4);

[[nodiscard]] constexpr bool EffectDefinitionKeyIsValid(
    const EffectDefinitionKey32 key) noexcept
{
    return key.value != 0;
}

enum class EffectTableSaveKeyPolicy : std::uint8_t
{
    // Preserve the exact legacy x86 archive key: the complete native identity
    // must fit in 32 bits and is emitted unchanged.
    LegacyPointerBits,
    // Allocate a nonzero archive-local key for each first-seen native
    // identity. Repeated identities reuse their original key.
    OpaqueSequential,
};
} // namespace fx::archive
