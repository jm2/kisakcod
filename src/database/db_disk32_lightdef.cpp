#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from db_disk32.schema

// LightDef's 64-bit load (header slot, pointer step and record body) is
// generated from its schema entry. Its attenuation image loads through
// Image's pointer step (db_disk32_image.cpp).
void __cdecl DB_LoadGfxLightDefPtrDisk32(bool atStreamStart, GfxLightDef **slot)
{
    db::disk32_load::LoadGfxLightDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
