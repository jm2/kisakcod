#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/06-image.schema
#include <database/db_disk32_renderer_hooks.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Image, a wave-3 parse family (docs/design/FASTFILE_LOADER.md): other assets
// point at images, and only client code reads their pixels. The header slot
// and the inserted pointer step are generated from its schema entry; the
// record body below is custom. It mirrors Load_GfxImage, Load_GfxTextureLoad
// and Load_GfxImageLoadDef in db_load.cpp: the 36-byte record streams into
// the temp block, its name into block 4, and a load definition and its pixels
// into the temp block after the record, which popping the temp block reclaims.
// A client makes the image's texture from them (Load_Texture); a headless
// server finalizes them as DB_FinalizeHeadlessTextureLoad does and keeps the
// texture null. Either way the pool's copy points into no temp bytes.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr auto kLoadDefHeaderBytes = static_cast<std::int32_t>(offsetof(disk32::GfxImageLoadDefDisk32, data));
// IMG_CATEGORY_WATER, from gfx_d3d/r_image.h, which no headless TU includes.
constexpr std::uint8_t kWaterCategory = 5;

// The 16-byte header at `at`, then exactly the resourceSize pixel bytes it
// declares, which is all the loader reads from it.
bool StreamLoadDef(std::uint8_t *at, std::int32_t *resourceSize)
{
    std::int32_t declared = 0;
    if (!StreamBytes(at, kLoadDefHeaderBytes))
        return false;
    std::memcpy(&declared, at + offsetof(disk32::GfxImageLoadDefDisk32, resourceSize), sizeof(declared));
    *resourceSize = declared;
    return StreamBytes(at + kLoadDefHeaderBytes, declared);
}

// As the headless server: embedded pixels and water own no external payload;
// an external image that is not delayed is accounted now, and a delayed one
// keeps its flag and byte count for DB_LoadDelayedImages.
bool FinalizeTexture(std::int32_t resourceSize, GfxImage *image)
{
    if (resourceSize > 0 || image->category == kWaterCategory)
    {
        image->delayLoadPixels = false;
        image->cardMemory = {};
    }
    else if (!image->delayLoadPixels)
    {
        const std::int32_t externalDataSize = image->cardMemory.platform[0];
        if (externalDataSize < 0)
            return Drop("Invalid fast-file external image size");
        image->cardMemory = {};
        DB_LoadedExternalData(externalDataSize);
    }
    return true;
}

// A texture offset token names an earlier texture whose image has the same
// map type: a client's texture, which this image shares, or a server's null.
bool ResolveTexture(disk32::PointerToken token, GfxImage *image, std::uint32_t mapType)
{
    std::uintptr_t texture = 0;
    const db::relocation::Status status = DB_ResolveInsertedPointer(token, DBAliasKind::GfxTexture, mapType, &texture);
    if (status == db::relocation::Status::Ok)
    {
        ShareTexture(image, texture);
        return true;
    }
    Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
    return false;
}

// The texture union: null, an offset token, or a load definition streamed
// into the temp block (-1, or -2 registering the GfxTexture alias).
bool LoadTexture(disk32::PointerToken token, GfxImage *image)
{
    image->texture.basemap = nullptr;
    const auto mapType = static_cast<std::uint32_t>(image->mapType);
    if (token.isNull())
        return true;
    if (token.isOffset())
        return ResolveTexture(token, image, mapType);
    DB_PushStreamPos(kTempBlock);
    std::uint8_t *const loadDef = DB_AllocStreamPos(3);
    if (!loadDef)
        return false;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::GfxTexture) : DBAliasHandle{};
    std::int32_t resourceSize = 0;
    if ((token.isSharedInline() && !inserted) || !StreamLoadDef(loadDef, &resourceSize))
        return false;
    // GfxImageLoadDef holds no pointers: its disk32 bytes are the native
    // record, 4-aligned in the temp block.
    if (kCreatesRendererObjects)
        CreateTexture(image, reinterpret_cast<GfxImageLoadDef *>(loadDef));
    else if (!FinalizeTexture(resourceSize, image))
        return false;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::GfxTexture, image->texture.basemap, mapType);
    DB_PopStreamPos();
    return true;
}
} // namespace

// The record at the temp block's position, its scalars from their retail
// offsets (bools as != 0), then the name and the texture with block 4 pushed.
bool LoadGfxImage(GfxImage *out)
{
    disk32::GfxImageDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    out->mapType = static_cast<MapType>(disk.mapType);
    out->picmip.platform[0] = disk.picmip[0];
    out->picmip.platform[1] = disk.picmip[1];
    out->noPicmip = disk.noPicmip != 0;
    out->semantic = disk.semantic;
    out->track = disk.track;
    out->cardMemory.platform[0] = disk.cardMemory[0];
    out->cardMemory.platform[1] = disk.cardMemory[1];
    out->width = disk.width;
    out->height = disk.height;
    out->depth = disk.depth;
    out->category = disk.category;
    out->delayLoadPixels = disk.delayLoadPixels != 0;
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file image has no name"); // the asset pool hashes it
    if (!LoadTexture(disk.texture.token, out))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadGfxImagePtrDisk32(bool atStreamStart, GfxImage **slot)
{
    db::disk32_load::LoadGfxImageHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
