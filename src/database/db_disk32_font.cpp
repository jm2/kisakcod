#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/14-font.schema
#include <database/db_validation.h>

#include <cstdint>
#include <cstring>

// Font, a wave-2 parse family (docs/design/FASTFILE_LOADER.md): only client
// code draws text. The header slot and the inserted pointer step are
// generated from its schema entry; the record body below is custom. It
// mirrors Load_Font in db_load.cpp: the 24-byte record streams into the temp
// block, its glyph table is checked, then its name, materials and glyphs load
// with block 4 pushed. A Glyph holds no pointer (its schema entry asserts
// both widths share its 24 bytes), so the glyphs stay where they stream and
// the native pointer points at them. The materials load through Material's
// pointer step, as Load_MaterialHandle loads them.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
bool LoadMaterial(disk32::Ptr32<void> field, Material **out)
{
    *out = nullptr;
    LoadMaterialPtr(field.token, out);
    return true;
}

// -1: the glyphs stream here, 4-aligned. Any other token names glyphs an
// earlier record streamed into block 4, as DB_ConvertOffsetToPointer resolves it.
bool LoadGlyphs(disk32::PointerToken token, std::int32_t count, Glyph **out)
{
    std::int32_t bytes = 0;
    if (!db::validation::CheckedArrayBytes(count, sizeof(disk32::GlyphDisk32), &bytes))
        return Drop("Invalid fast-file font glyph table");
    if (token.isInline())
    {
        std::uint8_t *const glyphs = DB_AllocStreamPos(3);
        if (!StreamBytes(glyphs, bytes))
            return false;
        *out = reinterpret_cast<Glyph *>(glyphs);
        return true;
    }
    std::uintptr_t address = 0;
    const db::relocation::Status status = DB_ResolveOffsetBytes(
        token, static_cast<std::uint64_t>(bytes), alignof(std::uint32_t), db::relocation::BlockBit(kVirtualBlock),
        &address);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer offset: %s", db::relocation::StatusName(status));
        return false;
    }
    *out = reinterpret_cast<Glyph *>(address);
    return true;
}
} // namespace

// The record at the temp block's position and Load_Font's glyph-table rule,
// then the name, the materials and the glyphs with block 4 pushed.
bool LoadFont(Font_s *out)
{
    disk32::FontDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    CopyFontScalars(disk, out);
    if (!db::validation::CountInRange(out->glyphCount, 96, 65536) || disk.glyphs.token.isNull())
        return Drop("Invalid fast-file font glyph table");
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.fontName, &out->fontName))
        return false;
    if (!out->fontName)
        return Drop("Fast-file font has no name"); // the asset pool hashes it
    if (!LoadMaterial(disk.material, &out->material) || !LoadMaterial(disk.glowMaterial, &out->glowMaterial)
        || !LoadGlyphs(disk.glyphs.token, out->glyphCount, &out->glyphs))
    {
        return false;
    }
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadFontPtrDisk32(bool atStreamStart, Font_s **slot)
{
    db::disk32_load::LoadFontHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
