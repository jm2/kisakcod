#include "files.h"
#include <universal/assertive.h>
#include <universal/q_shared.h>
#include <universal/com_files.h>
#include <universal/info_string.h>
#include "qcommon.h"
#include <database/database.h>
#include "cmd.h"

#include <array>
#include <climits>
#ifndef KISAK_DEDI_HEADLESS
#include <sound/snd_public.h>
#endif

int fs_numServerReferencedIwds;
char basename[64];
const char *fs_serverReferencedIwdNames[1024];
int fs_serverReferencedIwds[1024];

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

int __cdecl FS_CompareIwds(char *needediwds, int len, int dlstring)
{
    char *v4; // eax
    const char *v5; // [esp+8h] [ebp-20h]
    const char *string; // [esp+Ch] [ebp-1Ch]
    uint32_t v7; // [esp+Ch] [ebp-1Ch]
    int haveiwd; // [esp+1Ch] [ebp-Ch]
    searchpath_s *j; // [esp+20h] [ebp-8h]
    int i; // [esp+24h] [ebp-4h]

    if (!fs_numServerReferencedIwds)
        return 0;
    *needediwds = 0;
    string = fs_gameDirVar->current.string;
    v5 = string + 1;
    v7 = (uint32_t)&string[strlen(string) + 1];
    for (i = 0; i < fs_numServerReferencedIwds; ++i)
    {
        haveiwd = 0;
        if ((const char *)v7 == v5 || !FS_serverPak(fs_serverReferencedIwdNames[i]))
        {
            for (j = fs_searchpaths; j; j = j->next)
            {
                if (j->iwd && j->iwd->checksum == fs_serverReferencedIwds[i])
                {
                    haveiwd = 1;
                    break;
                }
            }
            if (!haveiwd && fs_serverReferencedIwdNames[i] && *fs_serverReferencedIwdNames[i])
            {
                if ((const char *)v7 == v5
                    || I_strnicmp(fs_serverReferencedIwdNames[i], fs_gameDirVar->current.string, v7 - (_DWORD)v5)
                    || FS_iwIwd((char *)fs_serverReferencedIwdNames[i], (char*)"main"))
                {
                    I_strncpyz(needediwds, (char *)fs_serverReferencedIwdNames[i], len);
                    I_strncat(needediwds, len, ".iwd");
                    return 2;
                }
                if (dlstring)
                {
                    I_strncat(needediwds, len, "@");
                    I_strncat(needediwds, len, (char *)fs_serverReferencedIwdNames[i]);
                    I_strncat(needediwds, len, ".iwd");
                    I_strncat(needediwds, len, "@");
                    I_strncat(needediwds, len, (char *)fs_serverReferencedIwdNames[i]);
                    I_strncat(needediwds, len, ".iwd");
                }
                else
                {
                    I_strncat(needediwds, len, (char *)fs_serverReferencedIwdNames[i]);
                    I_strncat(needediwds, len, ".iwd");
                    v4 = va("%s.iwd", fs_serverReferencedIwdNames[i]);
                    if (FS_SV_FileExists(v4))
                        I_strncat(needediwds, len, " (local file exists with wrong checksum)");
                    I_strncat(needediwds, len, "\n");
                }
            }
        }
    }
    if (!*needediwds)
        return 0;
    Com_Printf(10, "Need iwds: %s\n", needediwds);
    return 1;
}

int fs_numServerReferencedFFs;
const char *fs_serverReferencedFFNames[32];
int fs_serverReferencedFFCheckSums[32];
static_assert(ARRAY_COUNT(fs_serverReferencedFFNames) == ARRAY_COUNT(fs_serverReferencedFFCheckSums));
int __cdecl FS_CompareFFs(char *neededFFs, int len, int dlstring)
{
    int v4; // eax
    const char *v5; // [esp+18h] [ebp-28h]
    const char *string; // [esp+1Ch] [ebp-24h]
    uint32_t v7; // [esp+1Ch] [ebp-24h]
    char *ffName; // [esp+2Ch] [ebp-14h]
    const char *ffNamea; // [esp+2Ch] [ebp-14h]
    int fileSize; // [esp+30h] [ebp-10h]
    int i; // [esp+3Ch] [ebp-4h]

    if (!fs_numServerReferencedFFs)
        return 0;
    *neededFFs = 0;
    string = fs_gameDirVar->current.string;
    v5 = string + 1;
    v7 = (uint32_t)&string[strlen(string) + 1];
    for (i = 0; i < fs_numServerReferencedFFs; ++i)
    {
        if (I_strncmp(fs_serverReferencedFFNames[i], "mods", 4)
            || (ffName = strchr((char *)fs_serverReferencedFFNames[i] + 5, 47)) == 0
            || strlen(ffName) <= 1)
        {
            v4 = DB_FileSize(fs_serverReferencedFFNames[i], 0);
        }
        else
        {
            ffNamea = ffName + 1;
            iassert( ffName[0] );
            v4 = DB_FileSize(ffNamea, 1);
        }
        fileSize = v4;
        if (v4 != fs_serverReferencedFFCheckSums[i] && fs_serverReferencedFFNames[i] && *fs_serverReferencedFFNames[i])
        {
            if ((const char *)v7 == v5
                || I_strnicmp(fs_serverReferencedFFNames[i], fs_gameDirVar->current.string, v7 - (_DWORD)v5))
            {
                I_strncpyz(neededFFs, (char *)fs_serverReferencedFFNames[i], len);
                I_strncat(neededFFs, len, ".ff");
                return 2;
            }
            if (dlstring)
            {
                I_strncat(neededFFs, len, "@");
                I_strncat(neededFFs, len, (char *)fs_serverReferencedFFNames[i]);
                I_strncat(neededFFs, len, ".ff");
                I_strncat(neededFFs, len, "@");
                I_strncat(neededFFs, len, (char *)fs_serverReferencedFFNames[i]);
                I_strncat(neededFFs, len, ".ff");
            }
            else
            {
                I_strncat(neededFFs, len, (char *)fs_serverReferencedFFNames[i]);
                I_strncat(neededFFs, len, ".ff");
                if (fileSize)
                    I_strncat(neededFFs, len, " (local file exists with wrong filesize)");
                I_strncat(neededFFs, len, "\n");
            }
        }
    }
    if (!*neededFFs)
        return 0;
    Com_Printf(10, "Need FFs: %s\n", neededFFs);
    return 1;
}

FS_SERVER_COMPARE_RESULT __cdecl FS_CompareWithServerFiles(char *neededFiles, int len, int dlstring)
{
    FS_SERVER_COMPARE_RESULT iwdCompareResult; // [esp+10h] [ebp-Ch]
    int neededIWDStrLen; // [esp+14h] [ebp-8h]
    FS_SERVER_COMPARE_RESULT ffCompareResult; // [esp+18h] [ebp-4h]

    *neededFiles = 0;
    iwdCompareResult = (FS_SERVER_COMPARE_RESULT)FS_CompareIwds(neededFiles, len, dlstring);
    if (iwdCompareResult == NOT_DOWNLOADABLE)
        return (FS_SERVER_COMPARE_RESULT)2;
    neededIWDStrLen = strlen(neededFiles);
    iassert( len >= neededIWDStrLen );
    ffCompareResult = (FS_SERVER_COMPARE_RESULT)FS_CompareFFs(&neededFiles[neededIWDStrLen], len - neededIWDStrLen, dlstring);
    if (ffCompareResult == NOT_DOWNLOADABLE)
        return (FS_SERVER_COMPARE_RESULT)2;
    return (FS_SERVER_COMPARE_RESULT)(iwdCompareResult == NEED_DOWNLOAD || ffCompareResult == NEED_DOWNLOAD);
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
