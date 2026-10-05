#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/20-fx.schema

#include <cstdint>
#include <cstring>

// FX, a wave-3 family (docs/design/FASTFILE_LOADER.md). The body mirrors
// Load_FxEffectDef without its FX zone-adapter return: the 32-byte record
// streams into the temp block, then with block 4 pushed its name. Elements do
// not load yet, so an effect that has any fails closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr std::uint32_t kMaxElements = 256;

// The three counts' sum is at most 256, so none is negative, and its token
// agrees with it.
bool HeaderValid(const disk32::FxEffectDefDisk32 &effect, std::uint32_t *count)
{
    const std::uint64_t sum = std::uint64_t{static_cast<std::uint32_t>(effect.elemDefCountLooping)}
        + static_cast<std::uint32_t>(effect.elemDefCountOneShot)
        + static_cast<std::uint32_t>(effect.elemDefCountEmission);
    *count = static_cast<std::uint32_t>(sum);
    return sum <= kMaxElements && (sum == 0) == effect.elemDefs.token.isNull();
}
} // namespace

// The record at the temp block's position, then its name with block 4 pushed.
bool LoadFxEffectDef(FxEffectDef *out)
{
    disk32::FxEffectDefDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    std::uint32_t count = 0;
    if (!HeaderValid(disk, &count))
        return Drop("Invalid fast-file effect header");
    CopyFxEffectDefScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.name, &out->name))
        return false;
    if (!out->name)
        return Drop("Fast-file effect has no name"); // the asset pool hashes it
    out->elemDefs = nullptr;
    if (count)
        return Drop("Fast-file effect elements have no 64-bit loader yet");
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadFxEffectDefHandleDisk32(bool atStreamStart, const FxEffectDef **slot)
{
    db::disk32_load::LoadFxEffectDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
