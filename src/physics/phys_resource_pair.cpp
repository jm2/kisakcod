#include "phys_resource_pair.h"

namespace physics::allocation
{
ResourcePairResult TryCreateResourcePair(
    const ResourcePairCallbacks &callbacks,
    const bool companionRequired) noexcept
{
    if (!callbacks.createPrimary
        || (companionRequired
            && (!callbacks.createCompanion
                || !callbacks.destroyPrimary)))
    {
        return {ResourcePairStatus::InvalidCallbacks, nullptr, nullptr};
    }

    ResourceHandle const primary =
        callbacks.createPrimary(callbacks.context);
    if (!primary)
    {
        return {ResourcePairStatus::PrimaryUnavailable, nullptr, nullptr};
    }

    if (!companionRequired)
        return {ResourcePairStatus::Success, primary, nullptr};

    ResourceHandle const companion =
        callbacks.createCompanion(callbacks.context, primary);
    if (!companion)
    {
        callbacks.destroyPrimary(callbacks.context, primary);
        return {ResourcePairStatus::CompanionUnavailable, nullptr, nullptr};
    }

    return {ResourcePairStatus::Success, primary, companion};
}
} // namespace physics::allocation
