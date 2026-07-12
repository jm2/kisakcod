#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#include <qcommon/sys_memory.h>

namespace
{
struct Reservation
{
    void *base;
    std::size_t size;
};

std::mutex s_reservationsMutex;
std::vector<Reservation> s_reservations;

bool AlignUpToPage(
    const std::size_t size,
    const std::size_t pageSize,
    std::size_t *const alignedSize)
{
    if (!size || !pageSize || !alignedSize)
        return false;

    const std::size_t remainder = size % pageSize;
    const std::size_t adjustment = remainder ? pageSize - remainder : 0;
    if (size > (std::numeric_limits<std::size_t>::max)() - adjustment)
        return false;

    *alignedSize = size + adjustment;
    return true;
}

std::vector<Reservation>::iterator FindContainingReservation(
    const void *const address,
    const std::size_t size)
{
    const std::uintptr_t addressValue = reinterpret_cast<std::uintptr_t>(address);
    for (auto reservation = s_reservations.begin();
         reservation != s_reservations.end();
         ++reservation)
    {
        const std::uintptr_t baseValue =
            reinterpret_cast<std::uintptr_t>(reservation->base);
        if (addressValue < baseValue)
            continue;

        const std::uintptr_t offset = addressValue - baseValue;
        if (offset <= reservation->size && size <= reservation->size - offset)
            return reservation;
    }

    return s_reservations.end();
}

bool ValidateRange(
    void *const address,
    const std::size_t size,
    std::size_t *const alignedSize)
{
    const std::size_t pageSize = Sys_VirtualMemoryPageSize();
    if (!address
        || !AlignUpToPage(size, pageSize, alignedSize)
        || reinterpret_cast<std::uintptr_t>(address) % pageSize != 0)
    {
        return false;
    }

    return FindContainingReservation(address, *alignedSize)
        != s_reservations.end();
}
}

std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize()
{
    static const std::size_t pageSize = [] {
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        return static_cast<std::size_t>(systemInfo.dwPageSize);
    }();
    return pageSize;
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    std::size_t alignedSize;
    if (!AlignUpToPage(size, Sys_VirtualMemoryPageSize(), &alignedSize))
        return nullptr;

    void *const address = VirtualAlloc(
        nullptr,
        alignedSize,
        MEM_RESERVE,
        PAGE_NOACCESS);
    if (!address)
        return nullptr;

    try
    {
        const std::lock_guard<std::mutex> lock(s_reservationsMutex);
        s_reservations.push_back({address, alignedSize});
    }
    catch (...)
    {
        VirtualFree(address, 0, MEM_RELEASE);
        return nullptr;
    }

    return address;
}

bool KISAK_CDECL Sys_VirtualMemoryCommit(
    void *const address,
    const std::size_t size)
{
    const std::lock_guard<std::mutex> lock(s_reservationsMutex);
    std::size_t alignedSize;
    if (!ValidateRange(address, size, &alignedSize))
        return false;

    return VirtualAlloc(
        address,
        alignedSize,
        MEM_COMMIT,
        PAGE_READWRITE) == address;
}

bool KISAK_CDECL Sys_VirtualMemoryDecommit(
    void *const address,
    const std::size_t size)
{
    const std::lock_guard<std::mutex> lock(s_reservationsMutex);
    std::size_t alignedSize;
    if (!ValidateRange(address, size, &alignedSize))
        return false;

    return VirtualFree(address, alignedSize, MEM_DECOMMIT) != FALSE;
}

bool KISAK_CDECL Sys_VirtualMemoryRelease(void *const address)
{
    if (!address)
        return false;

    const std::lock_guard<std::mutex> lock(s_reservationsMutex);
    for (auto reservation = s_reservations.begin();
         reservation != s_reservations.end();
         ++reservation)
    {
        if (reservation->base != address)
            continue;

        if (!VirtualFree(address, 0, MEM_RELEASE))
            return false;

        s_reservations.erase(reservation);
        return true;
    }

    return false;
}
