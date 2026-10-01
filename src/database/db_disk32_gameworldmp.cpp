#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

// GameWorldMp's 64-bit load (header slot, pointer step and record body) is
// generated from its schema entry.
void __cdecl DB_LoadGameWorldMpPtrDisk32(bool atStreamStart, GameWorldMp **slot)
{
    db::disk32_load::LoadGameWorldMpHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
