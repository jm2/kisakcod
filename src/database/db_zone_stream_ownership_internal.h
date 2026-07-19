#pragma once

#include <database/db_relocation.h>

namespace db::zone_stream_ownership::detail
{
// Private bridge for the legacy DB_* stream wrappers.  Public ownership APIs
// and tests do not receive mutable relocation capabilities.
[[nodiscard]] relocation::AliasRegistry &AliasRegistryForLegacyStream()
    noexcept;
[[nodiscard]] relocation::DirectResolver &DirectResolverForLegacyStream()
    noexcept;
[[nodiscard]] bool OwnershipBindingActive() noexcept;
} // namespace db::zone_stream_ownership::detail
