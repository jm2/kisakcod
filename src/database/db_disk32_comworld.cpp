#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

// ComWorld's 64-bit load (header slot, pointer step and record body) is
// generated from its schema entry.
void __cdecl DB_LoadComWorldPtrDisk32(bool atStreamStart, ComWorld **slot)
{
    db::disk32_load::LoadComWorldHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
