#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/21-weapon.schema

#include <cstddef>
#include <cstdint>
#include <cstring>

// Weapon, a wave-3 family (docs/design/FASTFILE_LOADER.md). The body mirrors
// Load_WeaponDef: the 2168-byte record streams into the temp block and its
// scalar runs copy into the native 2832-byte record, then with block 4
// pushed each pointer loads in the 32-bit loader's order (kParts). Strings,
// script strings (interned), sounds by name through their string holders,
// and the bounce-sound table load here; models, effects, materials and the
// accuracy graphs do not yet, so a weapon that names one fails closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using Disk = disk32::WeaponDefDisk32;

enum class Kind : std::uint8_t
{
    String,
    Model,
    Effect,
    Sound,
    Material,
    BounceSounds,
    Knots,
};

// count pointers at a retail offset and at their native offset.
struct Part
{
    Kind kind;
    std::uint16_t disk;
    std::uint16_t native;
    std::uint8_t count;
};

constexpr Part P(Kind kind, std::size_t disk, std::size_t native, std::uint8_t count = 1)
{
    return {kind, static_cast<std::uint16_t>(disk), static_cast<std::uint16_t>(native), count};
}

// Load_WeaponDef's order.
constexpr Part kParts[] = {
    P(Kind::String, offsetof(Disk, szInternalName), offsetof(WeaponDef, szInternalName)),
    P(Kind::String, offsetof(Disk, szDisplayName), offsetof(WeaponDef, szDisplayName)),
    P(Kind::String, offsetof(Disk, szOverlayName), offsetof(WeaponDef, szOverlayName)),
    P(Kind::Model, offsetof(Disk, gunXModel), offsetof(WeaponDef, gunXModel), 16),
    P(Kind::Model, offsetof(Disk, handXModel), offsetof(WeaponDef, handXModel)),
    P(Kind::String, offsetof(Disk, szXAnims), offsetof(WeaponDef, szXAnims), 33),
    P(Kind::String, offsetof(Disk, szModeName), offsetof(WeaponDef, szModeName)),
    P(Kind::Effect, offsetof(Disk, viewFlashEffect), offsetof(WeaponDef, viewFlashEffect)),
    P(Kind::Effect, offsetof(Disk, worldFlashEffect), offsetof(WeaponDef, worldFlashEffect)),
    P(Kind::Sound, offsetof(Disk, pickupSound), offsetof(WeaponDef, pickupSound), 45),
    P(Kind::BounceSounds, offsetof(Disk, bounceSound), offsetof(WeaponDef, bounceSound)),
    P(Kind::Effect, offsetof(Disk, viewShellEjectEffect), offsetof(WeaponDef, viewShellEjectEffect), 4),
    P(Kind::Material, offsetof(Disk, reticleCenter), offsetof(WeaponDef, reticleCenter), 2),
    P(Kind::Model, offsetof(Disk, worldModel), offsetof(WeaponDef, worldModel), 20), // and its four models
    P(Kind::Material, offsetof(Disk, hudIcon), offsetof(WeaponDef, hudIcon)),
    P(Kind::Material, offsetof(Disk, ammoCounterIcon), offsetof(WeaponDef, ammoCounterIcon)),
    P(Kind::String, offsetof(Disk, szAmmoName), offsetof(WeaponDef, szAmmoName)),
    P(Kind::String, offsetof(Disk, szClipName), offsetof(WeaponDef, szClipName)),
    P(Kind::String, offsetof(Disk, szSharedAmmoCapName), offsetof(WeaponDef, szSharedAmmoCapName)),
    P(Kind::Material, offsetof(Disk, overlayMaterial), offsetof(WeaponDef, overlayMaterial), 2),
    P(Kind::Material, offsetof(Disk, killIcon), offsetof(WeaponDef, killIcon)),
    P(Kind::Material, offsetof(Disk, dpadIcon), offsetof(WeaponDef, dpadIcon)),
    P(Kind::String, offsetof(Disk, szAltWeaponName), offsetof(WeaponDef, szAltWeaponName)),
    P(Kind::Model, offsetof(Disk, projectileModel), offsetof(WeaponDef, projectileModel)),
    P(Kind::Effect, offsetof(Disk, projExplosionEffect), offsetof(WeaponDef, projExplosionEffect)),
    P(Kind::Effect, offsetof(Disk, projDudEffect), offsetof(WeaponDef, projDudEffect)),
    P(Kind::Sound, offsetof(Disk, projExplosionSound), offsetof(WeaponDef, projExplosionSound), 2),
    P(Kind::Effect, offsetof(Disk, projTrailEffect), offsetof(WeaponDef, projTrailEffect)),
    P(Kind::Effect, offsetof(Disk, projIgnitionEffect), offsetof(WeaponDef, projIgnitionEffect)),
    P(Kind::Sound, offsetof(Disk, projIgnitionSound), offsetof(WeaponDef, projIgnitionSound)),
    P(Kind::String, offsetof(Disk, accuracyGraphName), offsetof(WeaponDef, accuracyGraphName)),
    P(Kind::Knots, offsetof(Disk, accuracyGraphKnots), offsetof(WeaponDef, accuracyGraphKnots)),
    P(Kind::Knots, offsetof(Disk, originalAccuracyGraphKnots), offsetof(WeaponDef, originalAccuracyGraphKnots)),
    P(Kind::String, offsetof(Disk, accuracyGraphName) + 4, offsetof(WeaponDef, accuracyGraphName[1])),
    P(Kind::Knots, offsetof(Disk, accuracyGraphKnots) + 4, offsetof(WeaponDef, accuracyGraphKnots[1])),
    P(Kind::Knots, offsetof(Disk, originalAccuracyGraphKnots) + 4, offsetof(WeaponDef, originalAccuracyGraphKnots[1])),
    P(Kind::String, offsetof(Disk, szUseHintString), offsetof(WeaponDef, szUseHintString)),
    P(Kind::String, offsetof(Disk, dropHintString), offsetof(WeaponDef, dropHintString)),
    P(Kind::String, offsetof(Disk, szScript), offsetof(WeaponDef, szScript)),
    P(Kind::String, offsetof(Disk, fireRumble), offsetof(WeaponDef, fireRumble)),
    P(Kind::String, offsetof(Disk, meleeImpactRumble), offsetof(WeaponDef, meleeImpactRumble)),
};

// The distinct members a part of several covers are contiguous pointers.
constexpr bool Contiguous(std::size_t first, std::size_t next, std::size_t count)
{
    return next - first == count * sizeof(void *);
}
static_assert(Contiguous(offsetof(WeaponDef, pickupSound), offsetof(WeaponDef, bounceSound), 45));
static_assert(Contiguous(offsetof(WeaponDef, viewShellEjectEffect), offsetof(WeaponDef, reticleCenter), 4));
static_assert(Contiguous(offsetof(WeaponDef, reticleCenter), offsetof(WeaponDef, iReticleCenterSize), 2));
static_assert(Contiguous(offsetof(WeaponDef, worldModel), offsetof(WeaponDef, hudIcon), 20));
static_assert(Contiguous(offsetof(WeaponDef, overlayMaterial), offsetof(WeaponDef, overlayReticle), 2));
static_assert(Contiguous(offsetof(WeaponDef, projExplosionSound), offsetof(WeaponDef, bProjImpactExplode), 2));

disk32::PointerToken TokenAt(const Disk &disk, const Part &part, std::uint32_t index)
{
    disk32::PointerToken token{};
    std::memcpy(&token, reinterpret_cast<const std::uint8_t *>(&disk) + part.disk + index * sizeof(token),
                sizeof(token));
    return token;
}

// The native pointer slot a part's index-th token fills.
template <typename T>
T **SlotAt(WeaponDef *out, const Part &part, std::uint32_t index)
{
    return reinterpret_cast<T **>(reinterpret_cast<std::uint8_t *>(out) + part.native + index * sizeof(void *));
}

// Load_XStringPtr's -1: a 4-byte holder 4-aligned here, registered as the
// alias identity, then its string. Its native twin holds the string.
bool LoadHolder(const char *const **out)
{
    std::uint8_t *const holder = DB_AllocStreamPos(3);
    const DBAliasHandle completed =
        holder ? DB_RegisterPointerSlot(holder, DBAliasKind::XStringPointerSlot) : DBAliasHandle{};
    if (!completed || !StreamBytes(holder, sizeof(disk32::PointerToken)))
        return false;
    disk32::Ptr32<const char> token{};
    std::memcpy(&token, holder, sizeof(token));
    const char **const native = AllocNative<const char *>(1);
    if (!native || !LoadXString(token, native) || !DB_CompleteStringHolder(completed, holder, native))
        return false;
    *out = native;
    return true;
}

// Load_SndAliasCustom's lookup of a sound by its name.
bool FindSound(const char *name, snd_alias_list_t **out)
{
    *out = DB_FindXAssetHeader(ASSET_TYPE_SOUND, name).sound;
    return true;
}

// Load_snd_alias_list_name: a string holder, -1 here or an earlier one by
// offset, whose nonempty name DB_FindXAssetHeader looks up.
bool LoadSoundName(disk32::PointerToken token, snd_alias_list_t **out)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    const char *const *holder = nullptr;
    if (token.isInline())
        return LoadHolder(&holder) && FindSound(*holder, out);
    std::uintptr_t native = 0;
    const db::relocation::Status status =
        DB_ResolveCompletedObjectNative(token, DBAliasKind::XStringPointerSlot, 0, &native);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
        return false;
    }
    return FindSound(*reinterpret_cast<const char *const *>(native), out);
}

// The bounce-sound table: -1 streams its 29 name tokens 4-aligned here as a
// completed object whose native twin holds the sounds; any other token
// names an earlier table.
bool LoadBounceSounds(disk32::PointerToken token, snd_alias_list_t ***out)
{
    *out = nullptr;
    std::uintptr_t native = 0;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        const db::relocation::Status status = DB_ResolveCompletedObjectNative(
            token, DBAliasKind::WeaponBounceSoundTable, disk32::kWeaponBounceSoundTableBytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
        *out = reinterpret_cast<snd_alias_list_t **>(native);
        return true;
    }
    std::uint8_t *const table = DB_AllocStreamPos(3);
    const DBAliasHandle completed =
        table ? DB_RegisterPointerSlot(table, DBAliasKind::WeaponBounceSoundTable) : DBAliasHandle{};
    snd_alias_list_t **const sounds = AllocNative<snd_alias_list_t *>(disk32::kWeaponBounceSoundCount);
    if (!completed || !StreamBytes(table, disk32::kWeaponBounceSoundTableBytes) || !sounds)
        return false;
    for (std::uint32_t index = 0; index < disk32::kWeaponBounceSoundCount; ++index)
    {
        disk32::PointerToken name{};
        std::memcpy(&name, table + index * sizeof(name), sizeof(name));
        if (!LoadSoundName(name, &sounds[index]))
            return false;
    }
    *out = sounds;
    return DB_CompleteObject(completed, DBAliasKind::WeaponBounceSoundTable, table,
                             disk32::kWeaponBounceSoundTableBytes, disk32::kWeaponBounceSoundTableBytes, sounds);
}

bool LoadPart(const Disk &disk, const Part &part, WeaponDef *out)
{
    for (std::uint32_t index = 0; index < part.count; ++index)
    {
        const disk32::PointerToken token = TokenAt(disk, part, index);
        bool loaded = true;
        if (part.kind == Kind::String)
            loaded = LoadXString(disk32::Ptr32<const char>{token}, SlotAt<const char>(out, part, index));
        else if (part.kind == Kind::Sound)
            loaded = LoadSoundName(token, SlotAt<snd_alias_list_t>(out, part, index));
        else if (part.kind == Kind::BounceSounds)
            loaded = LoadBounceSounds(token, SlotAt<snd_alias_list_t *>(out, part, index));
        else if (!token.isNull())
            return Drop("Fast-file weapon models, effects, materials and graphs have no 64-bit loader yet");
        if (!loaded)
            return false;
    }
    return true;
}

// Load_ScriptStringArray over the tags and notetrack maps: each zone index
// becomes its interned id.
void LoadScriptStrings(WeaponDef *out)
{
    for (std::uint16_t &tag : out->hideTags)
        Load_ScriptStringCustom(&tag);
    for (std::uint16_t &key : out->notetrackSoundMapKeys)
        Load_ScriptStringCustom(&key);
    for (std::uint16_t &value : out->notetrackSoundMapValues)
        Load_ScriptStringCustom(&value);
}
} // namespace

// The record at the temp block's position, its scalars, then its pointers
// with block 4 pushed.
bool LoadWeaponDef(WeaponDef *out)
{
    Disk disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    CopyWeaponDefScalars(disk, out);
    if (disk.szInternalName.token.isNull())
        return Drop("Fast-file weapon has no name"); // the asset pool hashes it
    LoadScriptStrings(out);
    DB_PushStreamPos(kVirtualBlock);
    for (const Part &part : kParts)
    {
        if (!LoadPart(disk, part, out))
            return false;
    }
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadWeaponDefPtrDisk32(bool atStreamStart, WeaponDef **slot)
{
    db::disk32_load::LoadWeaponDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
