#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/21-weapon.schema

#include <cstddef>
#include <cstdint>
#include <cstring>

// Weapon, a wave-3 family (docs/design/FASTFILE_LOADER.md). The body mirrors
// Load_WeaponDef: the 2168-byte record streams into the temp block and its
// scalar runs copy into the native 2832-byte record, then with block 4
// pushed each pointer loads in the 32-bit loader's order (kParts). Names
// and other strings load here; models, effects, sounds, materials, script
// strings, the bounce-sound table and the accuracy graphs do not yet, so a
// weapon that names one fails closed.
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

bool LoadStrings(const Disk &disk, const Part &part, WeaponDef *out)
{
    for (std::uint32_t index = 0; index < part.count; ++index)
    {
        if (!LoadXString(disk32::Ptr32<const char>{TokenAt(disk, part, index)}, SlotAt<const char>(out, part, index)))
            return false;
    }
    return true;
}

bool LoadPart(const Disk &disk, const Part &part, WeaponDef *out)
{
    if (part.kind == Kind::String)
        return LoadStrings(disk, part, out);
    for (std::uint32_t index = 0; index < part.count; ++index)
    {
        if (!TokenAt(disk, part, index).isNull())
            return Drop("Fast-file weapon models, effects, sounds, materials and graphs have no 64-bit loader yet");
    }
    return true;
}

// The script strings the 32-bit loader interns: none yet.
bool ScriptStringsAbsent(const Disk &disk)
{
    std::uint16_t any = 0;
    for (const std::uint16_t value : disk.hideTags)
        any |= value;
    for (std::size_t index = 0; index < 16; ++index)
        any |= disk.notetrackSoundMapKeys[index] | disk.notetrackSoundMapValues[index];
    return !any || Drop("Fast-file weapon script strings have no 64-bit loader yet");
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
    if (!ScriptStringsAbsent(disk))
        return false;
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
