#include "database.h"
#include "db_validation.h"

#include <qcommon/threads.h>
#include <win32/win_local.h>
#include <universal/com_files.h>

#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_buffers.h>

//uint32_t volatile g_loadingAssets      828e3f3c     db_file_load.obj
//int32_t marker_db_file_load  828e3f40     db_file_load.obj

struct DB_LoadData // sizeof=0x68
{                                       // ...
    void* f;                            // ...
    const char* filename;               // ...
    XZoneMemory* zoneMem;               // ...
    int32_t outstandingReads;               // ...
    OVERLAPPED overlapped;             // ...
    z_stream_s stream;                  // ...
    uint8_t* compressBufferStart; // ...
    uint8_t* compressBufferEnd; // ...
    void(__cdecl* interrupt)();        // ...
    int32_t allocType;                      // ...
};

bool g_minimumFastFileLoaded;

DB_LoadData g_load;
LONG g_loadedSize;
LONG g_loadedExternalBytes;
volatile int32_t g_totalSize;
volatile int32_t g_totalExternalBytes;
int32_t g_trackLoadProgress;
volatile LONG g_fileReadComplete;
volatile LONG g_fileReadError;
volatile LONG g_fileReadBytes;
bool g_fileReadEof;
bool g_inflateInitialized;
bool g_inflateStreamEnded;

XAssetList g_varXAssetList;

static int32_t DB_WaitXFileStageInternal();

void __cdecl DB_CancelLoadXFile()
{
    while (g_load.outstandingReads > 0)
        DB_WaitXFileStageInternal();

    if (g_inflateInitialized)
    {
        DB_AuthLoad_InflateEnd(&g_load.stream);
        g_inflateInitialized = false;
    }
    if (g_load.f)
    {
        CloseHandle(g_load.f);
        g_load.f = nullptr;
    }
    g_load.compressBufferStart = nullptr;
    g_load.compressBufferEnd = nullptr;
    g_load.stream.next_in = nullptr;
    g_load.stream.avail_in = 0;
    g_load.stream.next_out = nullptr;
    g_load.stream.avail_out = 0;
    g_fileReadEof = false;
    g_inflateStreamEnded = false;
}

static int32_t DB_WaitXFileStageInternal()
{
    if (!g_load.f || g_load.f == INVALID_HANDLE_VALUE || g_load.outstandingReads <= 0)
    {
        g_load.outstandingReads = 0;
        return 0;
    }

    bool cancelRequested = false;
    while (!InterlockedCompareExchange(&g_fileReadComplete, 0, 0))
    {
        const DWORD waitResult = SleepEx(30000u, TRUE);
        if (InterlockedCompareExchange(&g_fileReadComplete, 0, 0))
            break;

        if (waitResult == 0)
        {
            if (cancelRequested)
            {
                g_load.outstandingReads = 0;
                Com_Error(
                    ERR_FATAL,
                    "Timed out cancelling fast-file read for '%s' (error %lu)",
                    g_load.filename,
                    static_cast<unsigned long>(ERROR_TIMEOUT));
                return 0;
            }
            if (!CancelIo(g_load.f))
            {
                const DWORD cancelError = GetLastError();
                g_load.outstandingReads = 0;
                Com_Error(
                    ERR_FATAL,
                    "Could not cancel fast-file read for '%s' (error %lu)",
                    g_load.filename,
                    cancelError);
                return 0;
            }
            cancelRequested = true;
        }
        else if (waitResult == WAIT_FAILED)
        {
            const DWORD waitError = GetLastError();
            g_load.outstandingReads = 0;
            Com_Error(
                ERR_FATAL,
                "Fast-file read wait failed for '%s' (error %lu)",
                g_load.filename,
                waitError);
            return 0;
        }
    }

    --g_load.outstandingReads;
    const LONG readError = InterlockedCompareExchange(&g_fileReadError, 0, 0);
    const LONG readBytes = InterlockedCompareExchange(&g_fileReadBytes, 0, 0);
    if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
    {
        g_fileReadEof = true;
        return 0;
    }
    if (readBytes < 0 || readBytes > 0x40000
        || !db::validation::CanAppendBytes(
            g_load.stream.avail_in,
            static_cast<uint32_t>(readBytes),
            0x80000u))
    {
        g_fileReadEof = true;
        return 0;
    }

    if (readBytes)
        InterlockedIncrement(&g_loadedSize);
    g_load.stream.avail_in += static_cast<uint32_t>(readBytes);

    if (readError == ERROR_HANDLE_EOF || readBytes < 0x40000)
    {
        g_fileReadEof = true;
    }
    else
    {
        ULARGE_INTEGER fileOffset;
        fileOffset.LowPart = g_load.overlapped.Offset;
        fileOffset.HighPart = g_load.overlapped.OffsetHigh;
        fileOffset.QuadPart += 0x40000;
        g_load.overlapped.Offset = fileOffset.LowPart;
        g_load.overlapped.OffsetHigh = fileOffset.HighPart;
    }
    return readBytes;
}

int32_t DB_WaitXFileStage()
{
    const int32_t readBytes = DB_WaitXFileStageInternal();
    if (readBytes <= 0)
    {
        const LONG readError = InterlockedCompareExchange(&g_fileReadError, 0, 0);
        DB_CancelLoadXFile();
        if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
            Com_Error(ERR_DROP, "Read error %ld for fast-file '%s'", readError, g_load.filename);
        else
            Com_Error(ERR_DROP, "Fast-file '%s' ended unexpectedly", g_load.filename);
        return 0;
    }
    return readBytes;
}

void __cdecl DB_LoadedExternalData(int32_t size)
{
    InterlockedExchangeAdd(&g_loadedExternalBytes, size);
}

double __cdecl DB_GetLoadedFraction()
{
    double loadedBytesInternal; // [esp+14h] [ebp-20h]
    double totalBytesInternal; // [esp+1Ch] [ebp-18h]
    double loadedBytesExternal; // [esp+24h] [ebp-10h]
    double totalBytesExternal; // [esp+2Ch] [ebp-8h]

    if (!g_totalSize)
        return 0.0;
    totalBytesInternal = (double)g_totalSize * 262144.0;
    loadedBytesInternal = (double)g_loadedSize * 262144.0;
    if (loadedBytesInternal < 0.0)
        MyAssertHandler(".\\database\\db_file_load.cpp", 341, 0, "%s", "loadedBytesInternal >= 0");
    if (totalBytesInternal < loadedBytesInternal)
        loadedBytesInternal = totalBytesInternal;
    totalBytesExternal = (double)g_totalExternalBytes;
    loadedBytesExternal = (double)g_loadedExternalBytes;
    if (totalBytesExternal < loadedBytesExternal)
        loadedBytesExternal = totalBytesExternal;
    return (float)((loadedBytesInternal + loadedBytesExternal) / (totalBytesInternal + totalBytesExternal));
}

void __cdecl DB_LoadXFileData(uint8_t *pos, uint32_t size)
{
    if (!pos || !size || !g_load.f || !g_inflateInitialized
        || g_inflateStreamEnded || g_load.stream.avail_out)
    {
        Com_Error(ERR_DROP, "Invalid fast-file output request for zone '%s'", g_load.filename);
        return;
    }

    g_load.stream.next_out = pos;
    g_load.stream.avail_out = size;
    while (g_load.stream.avail_out)
    {
        if (!g_load.stream.avail_in)
        {
            if (g_load.outstandingReads <= 0 || DB_WaitXFileStageInternal() <= 0)
            {
                const LONG readError = InterlockedCompareExchange(&g_fileReadError, 0, 0);
                DB_CancelLoadXFile();
                if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
                    Com_Error(ERR_DROP, "Read error %ld for fast-file '%s'", readError, g_load.filename);
                else
                    Com_Error(ERR_DROP, "Fastfile for zone '%s' ended unexpectedly.", g_load.filename);
                return;
            }
            DB_ReadXFileStage();
        }

        const uintptr_t bufferStart = reinterpret_cast<uintptr_t>(g_load.compressBufferStart);
        const uintptr_t bufferEnd = reinterpret_cast<uintptr_t>(g_load.compressBufferEnd);
        const uintptr_t nextIn = reinterpret_cast<uintptr_t>(g_load.stream.next_in);
        if (!bufferStart || bufferEnd < bufferStart || bufferEnd - bufferStart != 0x80000u
            || !db::validation::SpanWithinBlock(
                bufferStart,
                0x80000u,
                nextIn,
                g_load.stream.avail_in))
        {
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fast-file input span escaped its read buffer");
            return;
        }

        const uint32_t previousAvailIn = g_load.stream.avail_in;
        const uint32_t previousAvailOut = g_load.stream.avail_out;
        const int32_t err = static_cast<int32_t>(DB_AuthLoad_Inflate(&g_load.stream, 2));
        if (err != Z_OK && err != Z_STREAM_END)
        {
            KISAK_NULLSUB();
            DB_CancelLoadXFile();
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' appears corrupt or unreadable (code %i.)",
                g_load.filename,
                err);
            return;
        }

        if (g_load.f)
        {
            const uintptr_t updatedNextIn = reinterpret_cast<uintptr_t>(g_load.stream.next_in);
            if (!db::validation::SpanWithinBlock(bufferStart, 0x80000u, updatedNextIn, 0))
            {
                DB_CancelLoadXFile();
                Com_Error(ERR_DROP, "Fast-file input cursor escaped its read buffer");
                return;
            }
            if (g_load.stream.next_in == g_load.compressBufferEnd)
                g_load.stream.next_in = g_load.compressBufferStart;
        }

        if (err == Z_STREAM_END && g_load.stream.avail_out)
        {
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fastfile for zone '%s' has truncated output.", g_load.filename);
            return;
        }
        if (err == Z_STREAM_END)
            g_inflateStreamEnded = true;
        else if (g_load.stream.avail_in == previousAvailIn
            && g_load.stream.avail_out == previousAvailOut)
        {
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fastfile for zone '%s' made no decompression progress.", g_load.filename);
            return;
        }
    }

    const db::relocation::Status materialized =
        DB_MarkStreamRangeMaterialized(pos, size);
    if (materialized != db::relocation::Status::Ok
        && materialized != db::relocation::Status::InvalidContext
        && materialized != db::relocation::Status::OutOfRange)
    {
        DB_CancelLoadXFile();
        Com_Error(
            ERR_DROP,
            "Cannot record fast-file output range: %s",
            db::relocation::StatusName(materialized));
    }
}

void DB_ReadXFileStage()
{
    if (g_load.f && !g_fileReadEof)
    {
        if (g_load.outstandingReads)
        {
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Fast-file read already outstanding for zone '%s'", g_load.filename);
            return;
        }
        if (!DB_ReadData())
        {
            const DWORD readError = GetLastError();
            if (readError == ERROR_HANDLE_EOF)
            {
                g_fileReadEof = true;
                return;
            }
            DB_CancelLoadXFile();
            Com_Error(ERR_DROP, "Read error %lu of file '%s'", readError, g_load.filename);
            return;
        }
    }
}

int32_t __cdecl DB_ReadData()
{
    uint8_t *fileBuffer; // [esp+0h] [ebp-4h]

    if (!g_load.compressBufferStart || !g_load.compressBufferEnd
        || !g_load.f || g_load.f == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (g_load.interrupt)
        g_load.interrupt();
    if (!g_load.f)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    fileBuffer = &g_load.compressBufferStart[g_load.overlapped.Offset % 0x80000];
    Sys_WaitDatabaseThread();
    InterlockedExchange(&g_fileReadComplete, FALSE);
    InterlockedExchange(&g_fileReadError, ERROR_SUCCESS);
    InterlockedExchange(&g_fileReadBytes, 0);
    if (!ReadFileEx(g_load.f, fileBuffer, 0x40000u, &g_load.overlapped, (LPOVERLAPPED_COMPLETION_ROUTINE)DB_FileReadCompletion))
        return 0;
    ++g_load.outstandingReads;
    return 1;
}

void __stdcall DB_FileReadCompletion(
    uint32_t dwErrorCode,
    uint32_t dwNumberOfBytesTransfered,
    _OVERLAPPED *lpOverlapped)
{
    if (lpOverlapped != &g_load.overlapped || dwNumberOfBytesTransfered > 0x40000)
    {
        InterlockedExchange(&g_fileReadError, ERROR_INVALID_DATA);
        InterlockedExchange(&g_fileReadBytes, 0);
    }
    else
    {
        InterlockedExchange(&g_fileReadError, static_cast<LONG>(dwErrorCode));
        InterlockedExchange(&g_fileReadBytes, static_cast<LONG>(dwNumberOfBytesTransfered));
    }
    InterlockedExchange(&g_fileReadComplete, TRUE);
}

void __cdecl DB_LoadDelayedImages()
{
    uint32_t copyIter; // [esp+0h] [ebp-4h]

    DB_EnumXAssets(ASSET_TYPE_IMAGE, (void(__cdecl *)(XAssetHeader, void *))R_DelayLoadImage, 0, 0);
    for (copyIter = 0; copyIter < g_copyInfoCount; ++copyIter)
    {
        if (g_copyInfo[copyIter]->asset.type == ASSET_TYPE_IMAGE)
            R_DelayLoadImage(g_copyInfo[copyIter]->asset.header);
    }
}

void __cdecl DB_FinishGeometryBlocks(XZoneMemory *zoneMem)
{
    if (zoneMem->lockedVertexData)
    {
        R_FinishStaticVertexBuffer((IDirect3DVertexBuffer9*)zoneMem->vertexBuffer);
        zoneMem->lockedVertexData = 0;
    }
    if (zoneMem->lockedIndexData)
    {
        R_FinishStaticIndexBuffer((IDirect3DIndexBuffer9*)zoneMem->indexBuffer);
        zoneMem->lockedIndexData = 0;
    }
}

void __cdecl DB_LoadXFileInternal()
{
    int32_t err; // [esp+8h] [ebp-4Ch]
    bool fileIsSecure; // [esp+Fh] [ebp-45h]
    uint32_t version; // [esp+10h] [ebp-44h]
    XFile file; // [esp+14h] [ebp-40h] BYREF
    const char *failureReason; // [esp+44h] [ebp-10h]
    char magic[8]; // [esp+48h] [ebp-Ch] BYREF

    if (!g_load.f || g_load.f == INVALID_HANDLE_VALUE || !g_load.filename)
    {
        Com_Error(ERR_DROP, "Fast-file loader was not initialized");
        return;
    }
    DB_ReadXFileStage();
    if (!g_load.outstandingReads)
    {
        DB_CancelLoadXFile();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is empty.", g_load.filename);
        return;
    }
    const int32_t initialReadSize = DB_WaitXFileStageInternal();
    if (initialReadSize < 12)
    {
        const LONG readError = InterlockedCompareExchange(&g_fileReadError, 0, 0);
        DB_CancelLoadXFile();
        if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
            Com_Error(ERR_DROP, "Read error %ld for fast-file '%s'", readError, g_load.filename);
        else
            Com_Error(ERR_DROP, "Fastfile for zone '%s' has a truncated header.", g_load.filename);
        return;
    }
    DB_ReadXFileStage();
    memcpy(magic, g_load.stream.next_in, sizeof(magic));
    g_load.stream.next_in += 8;
    g_load.stream.avail_in -= 8;
    if (memcmp(magic, "IWff0100", 8u) && memcmp(magic, "IWffu100", 8u))
    {
        KISAK_NULLSUB();
        DB_CancelLoadXFile();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' is corrupt or unreadable.", g_load.filename);
        return;
    }
    memcpy(&version, g_load.stream.next_in, sizeof(version));
    g_load.stream.next_in += 4;
    g_load.stream.avail_in -= 4;
    if (version != 5)
    {
        DB_CancelLoadXFile();
        if (version >= 5)
        {
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is newer than client executable (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
        }
        else
        {
            Com_Error(
                ERR_DROP,
                "Fastfile for zone '%s' is out of date (version %d, expecting %d)",
                g_load.filename,
                version,
                5);
        }
        return;
    }
    fileIsSecure = memcmp(magic, "IWffu100", 8u) != 0;
    err = DB_AuthLoad_InflateInit(&g_load.stream, fileIsSecure);
    g_inflateInitialized = err == Z_OK;
    failureReason = 0;
    if (fileIsSecure)
        failureReason = "authenticated file not supported";
    if (err)
        failureReason = "init failed";
    if (failureReason)
    {
        KISAK_NULLSUB();
        DB_CancelLoadXFile();
        Com_Error(ERR_DROP, "Fastfile for zone '%s' could not be loaded (%s)", g_load.filename, failureReason);
        return;
    }
    
    DB_LoadXFileData((uint8_t *)&file, sizeof(XFile));
    if (g_trackLoadProgress)
    {
        LARGE_INTEGER nativeFileSize{};
        if (GetFileSizeEx(g_load.f, &nativeFileSize) && nativeFileSize.QuadPart >= 0)
        {
            const uint64_t fileSize = static_cast<uint64_t>(nativeFileSize.QuadPart);
            const uint64_t totalSize = fileSize + file.externalSize;
            const uint64_t fileChunks = (fileSize + 0x3FFFFu) / 0x40000u;
            if (totalSize >= 0x100000u && fileChunks <= INT32_MAX
                && file.externalSize <= INT32_MAX)
            {
                const int32_t remainingChunks = static_cast<int32_t>(fileChunks) - g_loadedSize;
                const int32_t remainingExternal = static_cast<int32_t>(file.externalSize) - g_loadedExternalBytes;
                g_totalSize = remainingChunks > 0 ? remainingChunks : 0;
                g_loadedSize = 0;
                g_totalExternalBytes = remainingExternal > 0 ? remainingExternal : 0;
                g_loadedExternalBytes = 0;
            }
        }
    }
    DB_AllocXZoneMemory(file.blockSize, g_load.filename, g_load.zoneMem, g_load.allocType);
    DB_InitStreams(g_load.zoneMem);
    Load_XAssetListCustom();
    DB_PushStreamPos(4);
    if (varXAssetList->assets)
    {
        varXAssetList->assets = AllocLoad_FxElemVisStateSample();
        varXAsset = varXAssetList->assets;
        Load_XAssetArrayCustom(varXAssetList->assetCount);
    }
    DB_PopStreamPos();
    DB_FinishGeometryBlocks(g_load.zoneMem);
    --g_loadingAssets;
    Load_DelayStream();
    DB_LoadDelayedImages();
    iassert(g_load.compressBufferStart);
    Com_Printf(10, "Loaded zone '%s'\n", g_load.filename);
    if (!g_minimumFastFileLoaded)
        g_minimumFastFileLoaded = I_stricmp("localized_code_post_gfx_mp", g_load.filename) == 0;
    DB_CancelLoadXFile();
}

bool __cdecl DB_IsMinimumFastFileLoaded()
{
    return g_minimumFastFileLoaded;
}

void Load_XAssetListCustom()
{
    varXAssetList = &g_varXAssetList;
    
    DB_LoadXFileData((uint8_t *)&g_varXAssetList, sizeof(XAssetList));
    if (varXAssetList->assetCount < 0 || varXAssetList->assetCount > 32768)
    {
        Com_Error(ERR_DROP, "Invalid fast-file asset count %d", varXAssetList->assetCount);
        return;
    }
    if ((varXAssetList->assetCount != 0) != (varXAssetList->assets != nullptr))
    {
        Com_Error(ERR_DROP, "Invalid fast-file asset list pointer/count combination");
        return;
    }
    varScriptStringList = &varXAssetList->stringList;
    if (varScriptStringList->count < 0 || varScriptStringList->count > 65536
        || (varScriptStringList->count != 0) != (varScriptStringList->strings != nullptr))
    {
        Com_Error(
            ERR_DROP,
            "Invalid fast-file script-string list count %d",
            varScriptStringList->count);
        return;
    }
    DB_PushStreamPos(4);
    Load_ScriptStringList(0);
    DB_PopStreamPos();
}

void __cdecl Load_XAssetArrayCustom(int32_t count)
{
    XAsset *var; // [esp+0h] [ebp-8h]
    int32_t i; // [esp+4h] [ebp-4h]
    int32_t byteCount = 0;

    if (count > 32768 || !db::validation::CheckedArrayBytes(count, 8, &byteCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file asset array count %d", count);
        return;
    }
    Load_Stream(1, (uint8_t *)varXAsset, byteCount);
    var = varXAsset;
    for (i = 0; i < count; ++i)
    {
        varXAsset = var;
        Load_XAsset(0);
        ++var;
    }
}

void __cdecl DB_ResetZoneSize(int32_t trackLoadProgress)
{
    g_totalSize = 0;
    g_loadedSize = 0;
    g_totalExternalBytes = 0;
    g_loadedExternalBytes = 0;
    g_trackLoadProgress = trackLoadProgress;
}

void __cdecl DB_LoadXFile(
    const char *path,
    void *f,
    const char *filename,
    XZoneMemory *zoneMem,
    void(__cdecl *interrupt)(),
    uint8_t *buf,
    int32_t allocType)
{
    (void)path;
    if (!f || f == INVALID_HANDLE_VALUE || !filename || !*filename || !zoneMem || !buf
        || (reinterpret_cast<uintptr_t>(buf) & 3) != 0
        || (allocType != 0 && allocType != 1))
    {
        Com_Error(ERR_DROP, "Invalid fast-file loader initialization");
        return;
    }
    if (g_load.f || g_load.outstandingReads || g_load.compressBufferStart)
    {
        Com_Error(ERR_DROP, "Attempted to replace an active fast-file load");
        return;
    }

    memset((uint8_t *)&g_load, 0, sizeof(g_load));
    g_fileReadEof = false;
    g_inflateInitialized = false;
    g_inflateStreamEnded = false;
    InterlockedExchange(&g_fileReadComplete, FALSE);
    InterlockedExchange(&g_fileReadError, ERROR_SUCCESS);
    InterlockedExchange(&g_fileReadBytes, 0);
    g_load.f = f;
    g_load.filename = filename;
    g_load.zoneMem = zoneMem;
    g_load.interrupt = interrupt;
    g_load.allocType = allocType;
    g_load.compressBufferStart = buf;
    g_load.compressBufferEnd = buf + 0x80000;
    g_load.stream.next_in = buf;
    g_load.stream.avail_in = 0;
}
