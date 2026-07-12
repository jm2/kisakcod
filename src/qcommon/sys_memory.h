#pragma once

#include <cstddef>

#include <universal/platform_compat.h>

// Returns the host's native virtual-memory page size. A zero result means the
// backend could not determine a usable page size.
std::size_t KISAK_CDECL Sys_VirtualMemoryPageSize();

// Reserves an inaccessible address range. The backend rounds size up to a
// native-page boundary and returns a native-page-aligned base address.
void *KISAK_CDECL Sys_VirtualMemoryReserve(std::size_t size);

// Commit and decommit operate on native-page-aligned subranges of a live
// reservation. Size is rounded up to a native-page boundary. Decommitted pages
// are inaccessible and read as zero after they are committed again.
bool KISAK_CDECL Sys_VirtualMemoryCommit(void *address, std::size_t size);
bool KISAK_CDECL Sys_VirtualMemoryDecommit(void *address, std::size_t size);

// Releases a complete reservation by its original base address exactly once.
bool KISAK_CDECL Sys_VirtualMemoryRelease(void *address);
