#include "database/db_graph_digest.h"

#include "database/database.h"

#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/q_shared.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
using db::graph_hash::Digest;
using db::graph_hash::GraphHashBuilder;

// The family record that frames the per-asset digests.
constexpr std::uint32_t kFamilyRecord = 0x4B344641u; // "K4FA"
enum FamilyField : std::uint32_t
{
    kFamilyType = 1,
    kFamilyCount,
    kFamilyAsset,
};

enum RawFileField : std::uint32_t
{
    kRawFileName = 1,
    kRawFileLen,
    kRawFileHasBuffer,
    kRawFileBuffer,
};

// The buffer holds len bytes and a terminator; the bytes are the content.
void HashRawFile(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const RawFile &raw = *header.rawfile;
    builder.FieldString(kRawFileName, raw.name);
    builder.FieldI64(kRawFileLen, raw.len);
    builder.FieldU64(kRawFileHasBuffer, raw.buffer != nullptr);
    builder.FieldBytes(kRawFileBuffer, raw.buffer, raw.buffer && raw.len > 0 ? static_cast<std::size_t>(raw.len) : 0);
}

using Walker = void (*)(GraphHashBuilder &, const XAssetHeader &);

Walker WalkerFor(const std::int32_t assetType)
{
    switch (assetType)
    {
    case ASSET_TYPE_RAWFILE:
        return HashRawFile;
    default:
        return nullptr;
    }
}

void __cdecl CollectAsset(XAssetHeader header, void *assets)
{
    static_cast<std::vector<XAssetHeader> *>(assets)->push_back(header);
}

void PrintAsset(const char *name, const Digest &digest)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  %s %s\n", hex, name);
}
} // namespace

Digest DB_GraphHashAsset(const std::int32_t assetType, const XAssetHeader &header)
{
    GraphHashBuilder builder;
    builder.BeginRecord(static_cast<std::uint32_t>(assetType));
    if (const Walker walker = WalkerFor(assetType))
        walker(builder, header);
    builder.EndRecord();
    return builder.Finish();
}

int DB_GraphHashFamily(
    const std::int32_t assetType,
    Digest &digest,
    void (*perAsset)(const char *name, const Digest &digest))
{
    if (!WalkerFor(assetType))
        return -1;

    // Pool order follows the name hash and the load order; name order is the
    // same on every target.
    std::vector<XAssetHeader> assets;
    DB_EnumXAssets(static_cast<XAssetType>(assetType), CollectAsset, &assets, false);
    std::sort(assets.begin(), assets.end(), [assetType](const XAssetHeader &a, const XAssetHeader &b) {
        return std::strcmp(DB_GetXAssetHeaderName(assetType, &a), DB_GetXAssetHeaderName(assetType, &b)) < 0;
    });

    GraphHashBuilder family;
    family.BeginRecord(kFamilyRecord);
    family.FieldU64(kFamilyType, static_cast<std::uint64_t>(assetType));
    family.FieldU64(kFamilyCount, assets.size());
    for (const XAssetHeader &header : assets)
    {
        const Digest asset = DB_GraphHashAsset(assetType, header);
        if (perAsset)
            perAsset(DB_GetXAssetHeaderName(assetType, &header), asset);
        family.FieldBytes(kFamilyAsset, asset.data(), asset.size());
    }
    family.EndRecord();
    digest = family.Finish();
    return static_cast<int>(assets.size());
}

void __cdecl DB_GraphHash_f()
{
    const char *const family = Cmd_Argv(1);
    std::int32_t type = 0;
    while (type < ASSET_TYPE_COUNT && I_stricmp(family, DB_GetXAssetTypeName(type)))
        ++type;
    const bool list = Cmd_Argc() > 2 && !I_stricmp(Cmd_Argv(2), "list");

    Digest digest{};
    const int count = type < ASSET_TYPE_COUNT ? DB_GraphHashFamily(type, digest, list ? PrintAsset : nullptr) : -1;
    if (count < 0)
    {
        Com_Printf(CON_CHANNEL_DONT_FILTER, "usage: db_graphhash <family> [list]; families:");
        for (type = 0; type < ASSET_TYPE_COUNT; ++type)
        {
            if (WalkerFor(type))
                Com_Printf(CON_CHANNEL_DONT_FILTER, " %s", DB_GetXAssetTypeName(type));
        }
        Com_Printf(CON_CHANNEL_DONT_FILTER, "\n");
        return;
    }
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "db_graphhash %s: %d assets, sha256 %s\n", DB_GetXAssetTypeName(type), count, hex);
}
