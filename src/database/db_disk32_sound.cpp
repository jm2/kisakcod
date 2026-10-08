#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/16-sound.schema
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Sound, a wave-3 parse family (docs/design/FASTFILE_LOADER.md). The body
// mirrors Load_snd_alias_list_t and Load_snd_alias_t: the list streams into the
// temp block, then with block 4 pushed its name, alias array and each alias's
// strings, sound file, curve (through SoundCurve's step) and speaker map. The
// alias array, sound files and speaker maps are completed objects streamed
// 4-aligned in block 4; an offset token names an earlier one and resolves to
// its native twin. A loaded-sound file loads through LoadedSound's step.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
constexpr auto kAliasBytes = static_cast<std::uint32_t>(sizeof(disk32::SndAliasDisk32));

// A completed object at `token`: null, an earlier object's native twin, or
// (-1) a record streamed here that `convert` turns into native storage.
template <typename Native, typename Convert>
bool LoadCompleted(disk32::PointerToken token, DBAliasKind kind, std::uint32_t bytes, Native **out, Convert convert)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    if (!token.isInline())
    {
        std::uintptr_t native = 0;
        const db::relocation::Status status = DB_ResolveCompletedObjectNative(token, kind, bytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return false;
        }
        *out = reinterpret_cast<Native *>(native);
        return true;
    }
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!record)
        return false;
    const DBAliasHandle completed = DB_RegisterPointerSlot(record, kind);
    Native *object = nullptr;
    if (!completed || !convert(record, &object) || !DB_CompleteObject(completed, kind, record, bytes, bytes, object))
        return false;
    *out = object;
    return true;
}

// Load_SoundFile: a loaded sound's token, or a streamed file's two strings.
bool ConvertSoundFile(std::uint8_t *record, SoundFile **out)
{
    disk32::SoundFileDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (!db::validation::SoundFileHeaderValid(disk.type, disk.exists))
        return Drop("Invalid fast-file sound-file header");
    SoundFile *const file = AllocNative<SoundFile>(1);
    if (!file)
        return false;
    file->type = disk.type;
    file->exists = disk.exists;
    std::memset(&file->u, 0, sizeof(file->u));
    *out = file;
    if (disk.type == 1)
    {
        LoadLoadedSoundPtr(disk.dir.token, &file->u.loadSnd, SlotOf(record, disk, disk.dir));
        return true;
    }
    StreamFileNameRaw &raw = file->u.streamSnd.filename.info.raw;
    return LoadXString(disk.dir, &raw.dir) && LoadXString(disk.name, &raw.name);
}

// DB_ValidateSpeakerMap's rules on the converted map.
bool CheckSpeakerMap(const SpeakerMap &map, std::uint8_t isDefault)
{
    if (!map.name)
        return Drop("Invalid fast-file speaker-map identity");
    if (isDefault > 1 || (!isDefault && !*map.name))
        return Drop("Invalid fast-file speaker-map default flag or name");
    for (std::uint32_t source = 0; source < 2; ++source)
    {
        for (std::uint32_t output = 0; output < 2; ++output)
        {
            const MSSChannelMap &channels = map.channelMaps[source][output];
            const std::uint32_t expected = db::validation::SpeakerMapExpectedSpeakerCount(output);
            if (channels.speakerCount != static_cast<std::int32_t>(expected))
                return Drop("Invalid fast-file speaker-map channel count");
            for (std::uint32_t speaker = 0; speaker < expected; ++speaker)
            {
                const MSSSpeakerLevels &levels = channels.speakers[speaker];
                if (!db::validation::SpeakerMapEntryValid(speaker, levels.speaker, levels.numLevels, source + 1,
                                                          levels.levels[0], levels.levels[1]))
                    return Drop("Invalid fast-file speaker-map levels");
            }
        }
    }
    return true;
}

// Load_SpeakerMap: the record, its name, and the channel maps as their bytes.
bool ConvertSpeakerMap(std::uint8_t *record, SpeakerMap **out)
{
    disk32::SpeakerMapDisk32 disk{};
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    SpeakerMap *const map = AllocNative<SpeakerMap>(1);
    if (!map)
        return false;
    map->isDefault = disk.isDefault != 0;
    // The schema's RUNTIME_SIZE/RUNTIME_OFFSET pin both to 400 bytes.
    std::memcpy(map->channelMaps, disk.channelMaps, sizeof(map->channelMaps));
    if (!LoadXString(disk.name, &map->name) || !CheckSpeakerMap(*map, disk.isDefault))
        return false;
    *out = map;
    return true;
}

bool LoadAliasStrings(const disk32::SndAliasDisk32 &disk, snd_alias_t *out)
{
    return LoadXString(disk.aliasName, &out->aliasName) && LoadXString(disk.subtitle, &out->subtitle)
        && LoadXString(disk.secondaryAliasName, &out->secondaryAliasName)
        && LoadXString(disk.chainAliasName, &out->chainAliasName);
}

// Load_snd_alias_t on one retail alias, then DB_ValidateSoundAlias.
bool LoadAlias(const disk32::SndAliasDisk32 &disk, const std::uint8_t *record, snd_alias_t *out)
{
    CopySndAliasScalars(disk, out);
    if (!LoadAliasStrings(disk, out)
        || !LoadCompleted(disk.soundFile.token, DBAliasKind::SoundFile, disk32::kSoundFileBytes, &out->soundFile,
                          ConvertSoundFile))
    {
        return false;
    }
    out->volumeFalloffCurve = nullptr;
    LoadSndCurvePtr(disk.volumeFalloffCurve.token, &out->volumeFalloffCurve,
                    SlotOf(record, disk, disk.volumeFalloffCurve));
    if (!LoadCompleted(disk.speakerMap.token, DBAliasKind::SpeakerMap, disk32::kSpeakerMapBytes, &out->speakerMap,
                       ConvertSpeakerMap))
    {
        return false;
    }
    if (!out->aliasName || !*out->aliasName || !out->soundFile || !out->volumeFalloffCurve || !out->speakerMap
        || out->soundFile->type != (static_cast<std::uint32_t>(out->flags) >> 6 & 3u))
    {
        return Drop("Invalid completed fast-file sound alias");
    }
    return true;
}

// Load_snd_alias_tArray: every retail alias streams first, then each converts.
bool ConvertAliases(std::uint8_t *record, std::int32_t count, std::int32_t bytes, snd_alias_t **out)
{
    if (!StreamBytes(record, bytes))
        return false;
    snd_alias_t *const aliases = AllocNative<snd_alias_t>(count);
    if (!aliases)
        return false;
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::SndAliasDisk32 disk{};
        const std::uint8_t *const at = record + static_cast<std::size_t>(index) * sizeof(disk);
        std::memcpy(&disk, at, sizeof(disk));
        if (!LoadAlias(disk, at, &aliases[index]))
            return false;
    }
    *out = aliases;
    return true;
}
} // namespace

// The list at the temp block's position and Load_snd_alias_list_t's count
// rules, then its name and its aliases with block 4 pushed.
bool LoadSndAliasList(snd_alias_list_t *out)
{
    disk32::SndAliasListDisk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    out->count = disk.count;
    std::int32_t bytes = 0;
    if (!db::validation::CheckedArrayBytes(disk.count, kAliasBytes, &bytes))
        return Drop("Invalid fast-file direct span for sound-alias entries");
    if ((disk.count == 0) != disk.head.token.isNull())
        return Drop("Invalid fast-file pointer/count for sound-alias entries");
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadXString(disk.aliasName, &out->aliasName))
        return false;
    if (!out->aliasName || !*out->aliasName)
        return Drop("Fast-file sound-alias list has no name"); // the asset pool hashes it
    const auto convert = [&](std::uint8_t *aliases, snd_alias_t **converted) {
        return ConvertAliases(aliases, disk.count, bytes, converted);
    };
    if (!LoadCompleted(disk.head.token, DBAliasKind::SndAliasArray, static_cast<std::uint32_t>(bytes), &out->head,
                       convert))
    {
        return false;
    }
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadSndAliasListPtrDisk32(bool atStreamStart, snd_alias_list_t **slot)
{
    db::disk32_load::LoadSndAliasListHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
