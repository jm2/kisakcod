#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <sched.h>
#include <time.h>

#include <qcommon/sys_time.h>

namespace
{
template <typename T>
std::uint64_t CheckedNonnegativeInteger(const T value)
{
    static_assert(std::is_integral_v<T>);

    if constexpr (std::is_signed_v<T>)
    {
        if (value < 0)
        {
            std::abort();
        }
    }

    using UnsignedT = std::make_unsigned_t<T>;
    const UnsignedT unsignedValue = static_cast<UnsignedT>(value);
    if constexpr (sizeof(UnsignedT) > sizeof(std::uint64_t))
    {
        if (unsignedValue > std::numeric_limits<std::uint64_t>::max())
        {
            std::abort();
        }
    }

    return static_cast<std::uint64_t>(unsignedValue);
}

std::uint32_t MonotonicMillisecondsRaw()
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        std::abort();
    }

    const std::uint64_t seconds = CheckedNonnegativeInteger(now.tv_sec);
    const std::uint64_t nanoseconds = CheckedNonnegativeInteger(now.tv_nsec);
    if (nanoseconds >= 1'000'000'000ULL)
    {
        std::abort();
    }

    constexpr std::uint64_t uint32Modulus =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1ULL;
    const std::uint64_t milliseconds =
        (seconds % uint32Modulus) * 1'000ULL + nanoseconds / 1'000'000ULL;
    return static_cast<std::uint32_t>(milliseconds % uint32Modulus);
}
}

std::uint32_t KISAK_CDECL Sys_MillisecondsRaw()
{
    return MonotonicMillisecondsRaw();
}

std::uint32_t KISAK_CDECL Sys_Milliseconds()
{
    static const std::uint32_t timeBase = Sys_MillisecondsRaw();
    return Sys_MillisecondsRaw() - timeBase;
}

void KISAK_CDECL Sys_Sleep(const std::uint32_t msec)
{
    if (msec == 0)
    {
        if (sched_yield() != 0)
        {
            std::abort();
        }
        return;
    }

    timespec remaining{
        static_cast<time_t>(msec / 1'000U),
        static_cast<long>(msec % 1'000U) * 1'000'000L,
    };
    while (nanosleep(&remaining, &remaining) != 0)
    {
        if (errno != EINTR)
        {
            std::abort();
        }
    }
}
