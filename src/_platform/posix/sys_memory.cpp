#include <sys/mman.h>
#include <unistd.h>

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
        const long result = sysconf(_SC_PAGESIZE);
        return result > 0 ? static_cast<std::size_t>(result) : std::size_t{0};
    }();
    return pageSize;
}

void *KISAK_CDECL Sys_VirtualMemoryReserve(const std::size_t size)
{
    std::size_t alignedSize;
    if (!AlignUpToPage(size, Sys_VirtualMemoryPageSize(), &alignedSize))
        return nullptr;

    void *const address = mmap(
        nullptr,
        alignedSize,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (address == MAP_FAILED)
        return nullptr;

    try
    {
        const std::lock_guard<std::mutex> lock(s_reservationsMutex);
        s_reservations.push_back({address, alignedSize});
    }
    catch (...)
    {
        munmap(address, alignedSize);
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

    return mprotect(address, alignedSize, PROT_READ | PROT_WRITE) == 0;
}

bool KISAK_CDECL Sys_VirtualMemoryDecommit(
    void *const address,
    const std::size_t size)
{
    const std::lock_guard<std::mutex> lock(s_reservationsMutex);
    std::size_t alignedSize;
    if (!ValidateRange(address, size, &alignedSize))
        return false;

    if (mprotect(address, alignedSize, PROT_NONE) != 0)
        return false;

    // Replace the subrange with fresh anonymous pages instead of relying on
    // MADV_DONTNEED, which is only a paging-strategy hint on macOS. MAP_FIXED
    // preserves the reserved addresses and a new anonymous mapping guarantees
    // zero-fill when the range is committed again.
    void *const replacement = mmap(
        address,
        alignedSize,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0);
    if (replacement == address)
        return true;

    if (replacement != MAP_FAILED)
        munmap(replacement, alignedSize);

    // mprotect already removed access. If replacement fails, retain PROT_NONE
    // rather than accidentally granting access to a previously reserved page.
    return false;
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

        if (munmap(address, reservation->size) != 0)
            return false;

        s_reservations.erase(reservation);
        return true;
    }

    return false;
}
