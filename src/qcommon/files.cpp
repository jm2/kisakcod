#include "files.h"
#include <universal/assertive.h>
#include <universal/q_shared.h>
#include <universal/com_files.h>
#include <universal/info_string.h>
#include "qcommon.h"
#include "server_file_compare.h"
#include <database/database.h>
#include "cmd.h"

#include <array>
#include <cstddef>
#include <cstring>
#ifndef KISAK_DEDI_HEADLESS
#include <sound/snd_public.h>
#endif

int fs_numServerReferencedIwds;
char basename[64];
const char *fs_serverReferencedIwdNames[1024];
int fs_serverReferencedIwds[1024];
static_assert(
    ARRAY_COUNT(fs_serverReferencedIwdNames)
    == ARRAY_COUNT(fs_serverReferencedIwds));

// KISAKTODO header-ify
extern int fs_fakeChkSum;

char *__cdecl FS_GetMapBaseName(char *mapname)
{
    uint32_t v2; // [esp+0h] [ebp-18h]
    int c; // [esp+10h] [ebp-8h]
    signed int len; // [esp+14h] [ebp-4h]

    iassert( mapname );
    if (!I_strnicmp(mapname, "maps/mp/", 8))
        mapname += 8;
    v2 = strlen(mapname);
    len = v2;
    if (!I_stricmp(&mapname[v2 - 3], "bsp"))
        len = v2 - 7;
    memcpy((uint8_t *)basename, (uint8_t *)mapname, len);
    basename[len] = 0;
    for (c = 0; c < len; ++c)
    {
        if (basename[c] == 37)
            basename[c] = 95;
    }
    return basename;
}

int __cdecl FS_serverPak(const char *pak)
{
    char szFile[68]; // [esp+10h] [ebp-48h] BYREF

    if (!pak || strlen(pak) >= sizeof(szFile))
        return 0;

    I_strncpyz(szFile, pak, sizeof(szFile));
    I_strlwr(szFile);
    return strstr(szFile, "_svr_") != 0;
}

int __cdecl FS_iwIwd(char *iwd, char *base)
{
    const char *v2; // eax
    const char *v4; // eax
    const char *v7; // [esp+Ch] [ebp-68h]
    char *str2; // [esp+20h] [ebp-54h]
    char szFile[68]; // [esp+24h] [ebp-50h] BYREF
    const char *pszLoc; // [esp+6Ch] [ebp-8h]
    int i; // [esp+70h] [ebp-4h]

    if (!iwd || !base || strlen(iwd) >= sizeof(szFile))
        return 0;

    for (i = 0; i < 25; ++i)
    {
        v2 = va("%s/iw_%02d", base, i);
        if (!FS_FilenameCompare(iwd, v2))
            return 1;
    }
    pszLoc = strstr(iwd, "localized_");
    if (pszLoc)
    {
        I_strncpyz(szFile, iwd, sizeof(szFile));
        szFile[pszLoc - iwd + 10] = 0;
        v4 = va("%s/localized_", base);
        if (!FS_FilenameCompare(szFile, v4))
        {
            v7 = pszLoc + 10;
            I_strncpyz(szFile, v7, sizeof(szFile));
            I_strlwr(szFile);
            for (i = 0; i < 25; ++i)
            {
                str2 = va("_iw%02d", i);
                if (strstr(szFile, str2))
                    return 1;
            }
        }
    }
    return 0;
}

int fs_numServerReferencedFFs;
const char *fs_serverReferencedFFNames[32];
int fs_serverReferencedFFCheckSums[32];
static_assert(
    ARRAY_COUNT(fs_serverReferencedFFNames)
    == ARRAY_COUNT(fs_serverReferencedFFCheckSums));

namespace
{
bool ServerHasIwdChecksum(void *, const int checksum)
{
    for (searchpath_s *searchPath = fs_searchpaths;
         searchPath;
         searchPath = searchPath->next)
    {
        if (searchPath->iwd && searchPath->iwd->checksum == checksum)
            return true;
    }
    return false;
}

bool ServerNameIsPak(void *, const char *const name)
{
    return FS_serverPak(name) != 0;
}

bool ServerNameIsOfficialMainIwd(void *, const char *const name)
{
    return FS_iwIwd(
        const_cast<char *>(name), const_cast<char *>("main")) != 0;
}

bool ServerIwdFileExists(void *, const char *const name)
{
    return FS_SV_FileExists(va("%s.iwd", name)) != 0;
}

int ServerFastFileSize(
    void *,
    const char *const name,
    const bool gameDirectory)
{
    return DB_FileSize(name, gameDirectory ? 1 : 0);
}

server_file_compare::Callbacks ServerFileCallbacks()
{
    return {
        nullptr,
        ServerHasIwdChecksum,
        ServerNameIsPak,
        ServerNameIsOfficialMainIwd,
        ServerIwdFileExists,
        ServerFastFileSize,
    };
}

const char *CurrentGameDirectory()
{
    if (!fs_gameDirVar || !fs_gameDirVar->current.string)
        return "";
    return fs_gameDirVar->current.string;
}

bool ServerReferenceCountsAreValid()
{
    return fs_numServerReferencedIwds >= 0
        && static_cast<std::size_t>(fs_numServerReferencedIwds)
            <= ARRAY_COUNT(fs_serverReferencedIwds)
        && fs_numServerReferencedFFs >= 0
        && static_cast<std::size_t>(fs_numServerReferencedFFs)
            <= ARRAY_COUNT(fs_serverReferencedFFCheckSums);
}

server_file_compare::IwdReferences ServerIwdReferences()
{
    return {
        fs_serverReferencedIwdNames,
        fs_serverReferencedIwds,
        static_cast<std::size_t>(fs_numServerReferencedIwds),
    };
}

server_file_compare::FastFileReferences ServerFastFileReferences()
{
    return {
        fs_serverReferencedFFNames,
        fs_serverReferencedFFCheckSums,
        static_cast<std::size_t>(fs_numServerReferencedFFs),
    };
}

FS_SERVER_COMPARE_RESULT LegacyCompareResult(
    const server_file_compare::Result result)
{
    static_assert(
        static_cast<int>(server_file_compare::Result::Match)
        == static_cast<int>(FILES_MATCH));
    static_assert(
        static_cast<int>(server_file_compare::Result::NeedDownload)
        == static_cast<int>(NEED_DOWNLOAD));
    static_assert(
        static_cast<int>(server_file_compare::Result::NotDownloadable)
        == static_cast<int>(NOT_DOWNLOADABLE));
    return static_cast<FS_SERVER_COMPARE_RESULT>(result);
}
}

int __cdecl FS_CompareIwds(char *needediwds, int len, int dlstring)
{
    if (!needediwds || len <= 0 || !ServerReferenceCountsAreValid())
        return NOT_DOWNLOADABLE;

    const server_file_compare::Result result =
        server_file_compare::CompareAll(
            needediwds,
            static_cast<std::size_t>(len),
            dlstring != 0,
            CurrentGameDirectory(),
            ServerIwdReferences(),
            {},
            ServerFileCallbacks());
    if (result == server_file_compare::Result::NeedDownload)
        Com_Printf(10, "Need iwds: %s\n", needediwds);
    return LegacyCompareResult(result);
}

int __cdecl FS_CompareFFs(char *neededFFs, int len, int dlstring)
{
    if (!neededFFs || len <= 0 || !ServerReferenceCountsAreValid())
        return NOT_DOWNLOADABLE;

    const server_file_compare::Result result =
        server_file_compare::CompareAll(
            neededFFs,
            static_cast<std::size_t>(len),
            dlstring != 0,
            CurrentGameDirectory(),
            {},
            ServerFastFileReferences(),
            ServerFileCallbacks());
    if (result == server_file_compare::Result::NeedDownload)
        Com_Printf(10, "Need FFs: %s\n", neededFFs);
    return LegacyCompareResult(result);
}

FS_SERVER_COMPARE_RESULT __cdecl FS_CompareWithServerFiles(char *neededFiles, int len, int dlstring)
{
    if (!neededFiles || len <= 0 || !ServerReferenceCountsAreValid())
        return NOT_DOWNLOADABLE;

    return LegacyCompareResult(server_file_compare::CompareAll(
        neededFiles,
        static_cast<std::size_t>(len),
        dlstring != 0,
        CurrentGameDirectory(),
        ServerIwdReferences(),
        ServerFastFileReferences(),
        ServerFileCallbacks()));
}

void __cdecl FS_ShutdownServerFileReferences(int *numFiles, const char **fileNames)
{
    int i; // [esp+0h] [ebp-4h]

    for (i = 0; i < *numFiles; ++i)
    {
        if (fileNames[i])
        {
            FreeString(fileNames[i]);
            fileNames[i] = 0;
        }
    }
    *numFiles = 0;
}

void __cdecl FS_ShutdownServerReferencedIwds()
{
    FS_ShutdownServerFileReferences(&fs_numServerReferencedIwds, fs_serverReferencedIwdNames);
}

int __cdecl FS_ServerSetReferencedFiles(
    char *fileSums,
    char *fileNames,
    int maxFiles,
    int *fs_sums,
    const char **fs_names)
{
    static_assert(
        ARRAY_COUNT(fs_serverReferencedIwds)
        <= static_cast<std::size_t>(INT_MAX));
    constexpr int kChecksumScratchCount =
        static_cast<int>(ARRAY_COUNT(fs_serverReferencedIwds));
    std::array<int, ARRAY_COUNT(fs_serverReferencedIwds)> parsedChecksums{};

    if (!fileSums
        || !fileNames
        || maxFiles <= 0
        || maxFiles > kChecksumScratchCount
        || !fs_sums
        || !fs_names)
    {
        Com_Error(ERR_DROP, "Invalid referenced-file list");
        return 0;
    }

    Cmd_TokenizeString(fileSums);
    const int checksumCount = Cmd_Argc();
    Cmd_EndTokenizedString();
    if (checksumCount > maxFiles)
    {
        Com_Error(
            ERR_DROP,
            "Too many referenced-file checksums (%d > %d)",
            checksumCount,
            maxFiles);
        return 0;
    }
    Cmd_TokenizeString(fileSums);
    for (int i = 0; i < checksumCount; ++i)
    {
        if (!info_string::TryParseSignedDecimalToken(
                Cmd_Argv(i),
                &parsedChecksums[static_cast<std::size_t>(i)]))
        {
            Cmd_EndTokenizedString();
            Com_Error(
                ERR_DROP,
                "Invalid referenced-file checksum at index %d",
                i);
            return 0;
        }
    }
    Cmd_EndTokenizedString();

    if (fileNames && *fileNames)
    {
        Cmd_TokenizeString(fileNames);
        const int nameCount = Cmd_Argc();
        if (nameCount > maxFiles)
        {
            Cmd_EndTokenizedString();
            Com_Error(
                ERR_DROP,
                "Too many referenced-file names (%d > %d)",
                nameCount,
                maxFiles);
            return 0;
        }
        if (checksumCount != nameCount)
        {
            Cmd_EndTokenizedString();
            Com_Error(ERR_DROP, "file sum/name mismatch");
            return 0;
        }

        // Validate every remote component before allocating or publishing any
        // entry so a bad later name cannot leave a partially replaced list.
        for (int i = 0; i < nameCount; ++i)
        {
            const char *const name = Cmd_Argv(i);
            if (!name
                || !*name
                || !info_string::IsSafeUnquotedPathTokenComponent(name))
            {
                Cmd_EndTokenizedString();
                Com_Error(
                    ERR_DROP,
                    "Invalid referenced-file name at index %d",
                    i);
                return 0;
            }
        }
        for (int i = 0; i < nameCount; ++i)
        {
            fs_names[i] = CopyString(Cmd_Argv(i));
        }
        Cmd_EndTokenizedString();
    }
    else if (checksumCount)
    {
        Com_Error(ERR_DROP, "file sum/name mismatch");
        return 0;
    }

    // Checksums are committed only after the paired name list passes its full
    // preflight, preserving the caller's arrays on every validation failure.
    for (int i = 0; i < checksumCount; ++i)
    {
        fs_sums[i] = parsedChecksums[static_cast<std::size_t>(i)];
    }
    return checksumCount;
}

void __cdecl FS_ServerSetReferencedIwds(char *iwdSums, char *iwdNames)
{
    FS_ShutdownServerReferencedIwds();
    fs_numServerReferencedIwds = FS_ServerSetReferencedFiles(
        iwdSums,
        iwdNames,
        1024,
        fs_serverReferencedIwds,
        fs_serverReferencedIwdNames);
}

void __cdecl FS_ShutdownServerReferencedFFs()
{
    FS_ShutdownServerFileReferences(&fs_numServerReferencedFFs, fs_serverReferencedFFNames);
}

void __cdecl FS_ServerSetReferencedFFs(char *FFSums, char *FFNames)
{
    FS_ShutdownServerReferencedFFs();
    fs_numServerReferencedFFs = FS_ServerSetReferencedFiles(
        FFSums,
        FFNames,
        ARRAY_COUNT(fs_serverReferencedFFCheckSums),
        fs_serverReferencedFFCheckSums,
        fs_serverReferencedFFNames);
}

void __cdecl FS_ShutdownServerIwdNames()
{
    FS_ShutdownServerFileReferences(&fs_numServerIwds, fs_serverIwdNames);
}

void __cdecl FS_PureServerSetLoadedIwds(char *iwdSums, char *iwdNames)
{
    const char *v2; // eax
    int v3; // eax
    const char *v4; // eax
    const char *v5; // eax
    int i; // [esp+0h] [ebp-2014h]
    int v7; // [esp+4h] [ebp-2010h]
    int v8; // [esp+8h] [ebp-200Ch]
    int src[1025]; // [esp+Ch] [ebp-2008h] BYREF
    int argIndex; // [esp+1010h] [ebp-1004h]
    const char *s0[1024]; // [esp+1014h] [ebp-1000h] BYREF

    iassert( iwdSums );
    iassert( iwdNames );
    Cmd_TokenizeString(iwdSums);
    v7 = Cmd_Argc();
    if (v7 > 1024)
        v7 = 1024;
    for (argIndex = 0; argIndex < v7; ++argIndex)
    {
        v2 = Cmd_Argv(argIndex);
        v3 = atoi(v2);
        src[argIndex] = v3;
    }
    Cmd_EndTokenizedString();
    Cmd_TokenizeString(iwdNames);
    v8 = Cmd_Argc();
    if (v8 > 1024)
        v8 = 1024;
    for (argIndex = 0; argIndex < v8; ++argIndex)
    {
        v4 = Cmd_Argv(argIndex);
        v5 = CopyString(v4);
        s0[argIndex] = v5;
    }
    Cmd_EndTokenizedString();
    if (v7 != v8)
        Com_Error(ERR_DROP, "iwd sum/name mismatch");
    if (v7 == fs_numServerIwds)
    {
        argIndex = 0;
    LABEL_19:
        if (argIndex >= v7)
        {
            for (argIndex = 0; argIndex < v8; ++argIndex)
                FreeString(s0[argIndex]);
            return;
        }
        for (i = 0; i < fs_numServerIwds; ++i)
        {
            if (src[argIndex] == fs_serverIwds[i] && !I_stricmp(s0[argIndex], fs_serverIwdNames[i]))
            {
                ++argIndex;
                goto LABEL_19;
            }
        }
    }
#ifndef KISAK_DEDI_HEADLESS
    SND_StopSounds(SND_STOP_STREAMED);
#endif
    FS_ShutdownServerIwdNames();
    fs_numServerIwds = v7;
    if (v7)
    {
        Com_DPrintf(10, "Connected to a pure server.\n");
        Com_Memcpy(fs_serverIwds, src, 4 * fs_numServerIwds);
        Com_Memcpy(fs_serverIwdNames, s0, 4 * fs_numServerIwds);
        fs_fakeChkSum = 0;
    }
}
