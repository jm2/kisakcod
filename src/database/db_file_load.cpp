#include "database.h"
#include "db_load_atomic.h"
#include "db_validation.h"

#include <qcommon/threads.h>
#include <win32/win_local.h>
#include <universal/com_files.h>
#include <universal/sys_atomic.h>

#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/r_image.h>
#include <gfx_d3d/r_buffers.h>
#endif

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

static volatile uint32_t g_minimumFastFileLoaded;

DB_LoadData g_load;
static db::load_atomic::ProgressState g_loadProgress;
static volatile uint32_t g_trackLoadProgress;
static db::load_atomic::FileReadState g_fileRead;
bool g_fileReadEof;
bool g_inflateInitialized;
bool g_inflateStreamEnded;

XAssetList g_varXAssetList;

static int32_t DB_WaitXFileStageInternal();
static VOID CALLBACK DB_FileReadCompletion(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped);

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
    while (!db::load_atomic::FileReadComplete(&g_fileRead))
    {
        const DWORD waitResult = SleepEx(30000u, TRUE);
        if (db::load_atomic::FileReadComplete(&g_fileRead))
            break;

        if (waitResult == 0)
        {
            if (cancelRequested)
            {
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
                if (cancelError == ERROR_NOT_FOUND)
                {
                    // The request can finish after the completion check but
                    // before cancellation.  Enter another alertable wait so
                    // its already-queued completion APC can publish the slot.
                    cancelRequested = true;
                    continue;
                }
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
            Com_Error(
                ERR_FATAL,
                "Fast-file read wait failed for '%s' (error %lu)",
                g_load.filename,
                waitError);
            return 0;
        }
    }

    --g_load.outstandingReads;
    const db::load_atomic::FileReadSnapshot readResult =
        db::load_atomic::SnapshotFileRead(&g_fileRead);
    iassert(readResult.complete);
    const uint32_t readError = readResult.error;
    const uint32_t readBytes = readResult.bytes;
    if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
    {
        g_fileReadEof = true;
        return 0;
    }
    if (readBytes > db::load_atomic::kFileReadBytes
        || !db::validation::CanAppendBytes(
            g_load.stream.avail_in,
            readBytes,
            db::load_atomic::kFileBufferBytes))
    {
        g_fileReadEof = true;
        return 0;
    }

    if (readBytes
        && db::load_atomic::AccumulateProgress(&g_loadProgress, 1, 0)
            != db::load_atomic::ProgressUpdateResult::Applied)
    {
        g_fileReadEof = true;
        Com_Error(ERR_DROP, "Fast-file internal load progress overflow");
        return 0;
    }
    g_load.stream.avail_in += readBytes;

    if (readError == ERROR_HANDLE_EOF
        || readBytes < db::load_atomic::kFileReadBytes)
    {
        g_fileReadEof = true;
    }
    else
    {
        ULARGE_INTEGER fileOffset;
        fileOffset.LowPart = g_load.overlapped.Offset;
        fileOffset.HighPart = g_load.overlapped.OffsetHigh;
        fileOffset.QuadPart += db::load_atomic::kFileReadBytes;
        g_load.overlapped.Offset = fileOffset.LowPart;
        g_load.overlapped.OffsetHigh = fileOffset.HighPart;
    }
    return static_cast<int32_t>(readBytes);
}

int32_t DB_WaitXFileStage()
{
    const int32_t readBytes = DB_WaitXFileStageInternal();
    if (readBytes <= 0)
    {
        const uint32_t readError =
            db::load_atomic::SnapshotFileRead(&g_fileRead).error;
        DB_CancelLoadXFile();
        if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
            Com_Error(
                ERR_DROP,
                "Read error %lu for fast-file '%s'",
                static_cast<unsigned long>(readError),
                g_load.filename);
        else
            Com_Error(ERR_DROP, "Fast-file '%s' ended unexpectedly", g_load.filename);
        return 0;
    }
    return readBytes;
}

void __cdecl DB_LoadedExternalData(int32_t size)
{
    if (db::load_atomic::AccumulateProgress(&g_loadProgress, 0, size)
        != db::load_atomic::ProgressUpdateResult::Applied)
        Com_Error(ERR_DROP, "Invalid fast-file external load progress increment %d", size);
}

double __cdecl DB_GetLoadedFraction()
{
    const db::load_atomic::ProgressSnapshot snapshot =
        db::load_atomic::SnapshotProgress(&g_loadProgress);
    if (snapshot.totalChunks < 0 || snapshot.loadedChunks < 0
        || snapshot.totalExternalBytes < 0
        || snapshot.loadedExternalBytes < 0)
    {
        MyAssertHandler(
            ".\\database\\db_file_load.cpp",
            341,
            0,
            "%s",
            "load progress counters are non-negative");
        return 0.0;
    }
    return static_cast<float>(db::load_atomic::LoadedFraction(snapshot));
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
                const uint32_t readError =
                    db::load_atomic::SnapshotFileRead(&g_fileRead).error;
                DB_CancelLoadXFile();
                if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
                    Com_Error(
                        ERR_DROP,
                        "Read error %lu for fast-file '%s'",
                        static_cast<unsigned long>(readError),
                        g_load.filename);
                else
                    Com_Error(ERR_DROP, "Fastfile for zone '%s' ended unexpectedly.", g_load.filename);
                return;
            }
            DB_ReadXFileStage();
        }

        const uintptr_t bufferStart = reinterpret_cast<uintptr_t>(g_load.compressBufferStart);
        const uintptr_t bufferEnd = reinterpret_cast<uintptr_t>(g_load.compressBufferEnd);
        const uintptr_t nextIn = reinterpret_cast<uintptr_t>(g_load.stream.next_in);
        if (!bufferStart || bufferEnd < bufferStart
            || bufferEnd - bufferStart != db::load_atomic::kFileBufferBytes
            || !db::validation::SpanWithinBlock(
                bufferStart,
                db::load_atomic::kFileBufferBytes,
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
            if (!db::validation::SpanWithinBlock(
                    bufferStart,
                    db::load_atomic::kFileBufferBytes,
                    updatedNextIn,
                    0))
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
    fileBuffer = &g_load.compressBufferStart[
        g_load.overlapped.Offset % db::load_atomic::kFileBufferBytes];
    Sys_WaitDatabaseThread();
    db::load_atomic::ResetFileRead(&g_fileRead);
    if (!ReadFileEx(
            g_load.f,
            fileBuffer,
            db::load_atomic::kFileReadBytes,
            &g_load.overlapped,
            DB_FileReadCompletion))
        return 0;
    ++g_load.outstandingReads;
    return 1;
}

static VOID CALLBACK DB_FileReadCompletion(
    DWORD dwErrorCode,
    DWORD dwNumberOfBytesTransfered,
    LPOVERLAPPED lpOverlapped)
{
    const bool validOverlapped = lpOverlapped == &g_load.overlapped;
    (void)db::load_atomic::PublishFileRead(
        &g_fileRead,
        validOverlapped
            ? static_cast<uint32_t>(dwErrorCode)
            : static_cast<uint32_t>(ERROR_INVALID_DATA),
        validOverlapped
            ? static_cast<uint32_t>(dwNumberOfBytesTransfered)
            : 0u,
        static_cast<uint32_t>(ERROR_INVALID_DATA));
}

#ifdef KISAK_DEDI_HEADLESS
namespace
{
void __cdecl DB_FinalizeHeadlessDelayedImage(XAssetHeader header, void *data)
{
    bool *imageLoadFailed = static_cast<bool *>(data);
    GfxImage *image = header.image;
    if (!image)
        return;

    // The fast-file texture load definition is never a live texture in the
    // null-resource backend, including when this image is visited twice via a
    // copied asset entry.
    image->texture.basemap = nullptr;
    if (!image->delayLoadPixels)
        return;

    const int32_t externalDataSize = image->cardMemory.platform[0];
    if (externalDataSize < 0)
    {
        if (imageLoadFailed)
            *imageLoadFailed = true;
        return;
    }
    image->delayLoadPixels = false;
    image->cardMemory.platform[0] = 0;
    image->cardMemory.platform[1] = 0;
    DB_LoadedExternalData(externalDataSize);
}
}
#endif

void __cdecl DB_LoadDelayedImages()
{
#ifdef KISAK_DEDI_HEADLESS
    bool imageLoadFailed = false;
    DB_EnumXAssets(
        ASSET_TYPE_IMAGE,
        DB_FinalizeHeadlessDelayedImage,
        &imageLoadFailed,
        0);
    for (uint32_t copyIter = 0; copyIter < g_copyInfoCount; ++copyIter)
    {
        if (g_copyInfo[copyIter]->asset.type == ASSET_TYPE_IMAGE)
        {
            DB_FinalizeHeadlessDelayedImage(
                g_copyInfo[copyIter]->asset.header,
                &imageLoadFailed);
        }
    }
    if (imageLoadFailed)
        Com_Error(ERR_DROP, "Invalid headless delayed image size");
#else
    uint32_t copyIter; // [esp+0h] [ebp-4h]
    bool imageLoadFailed = false;

    DB_EnumXAssets(
        ASSET_TYPE_IMAGE,
        R_DelayLoadImage,
        &imageLoadFailed,
        0);
    for (copyIter = 0; copyIter < g_copyInfoCount; ++copyIter)
    {
        if (g_copyInfo[copyIter]->asset.type == ASSET_TYPE_IMAGE)
            R_DelayLoadImage(
                g_copyInfo[copyIter]->asset.header,
                &imageLoadFailed);
    }
    if (imageLoadFailed)
        Com_Error(ERR_DROP, "One or more delayed images could not be loaded");
#endif
}

void __cdecl DB_FinishGeometryBlocks(XZoneMemory *zoneMem)
{
#ifdef KISAK_DEDI_HEADLESS
    iassert(zoneMem);

    // The CPU blocks remain owned by the zone.  Renderer handles and their
    // transient mapped pointers must never be synthesized from those blocks.
    zoneMem->lockedVertexData = nullptr;
    zoneMem->lockedIndexData = nullptr;
    zoneMem->vertexBuffer = nullptr;
    zoneMem->indexBuffer = nullptr;
#else
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
#endif
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
        const uint32_t readError =
            db::load_atomic::SnapshotFileRead(&g_fileRead).error;
        DB_CancelLoadXFile();
        if (readError != ERROR_SUCCESS && readError != ERROR_HANDLE_EOF)
            Com_Error(
                ERR_DROP,
                "Read error %lu for fast-file '%s'",
                static_cast<unsigned long>(readError),
                g_load.filename);
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
    if (Sys_AtomicLoad(&g_trackLoadProgress))
    {
        LARGE_INTEGER nativeFileSize{};
        if (GetFileSizeEx(g_load.f, &nativeFileSize) && nativeFileSize.QuadPart >= 0)
        {
            const uint64_t fileSize = static_cast<uint64_t>(nativeFileSize.QuadPart);
            const uint64_t totalSize = fileSize + file.externalSize;
            const uint64_t fileChunks =
                (fileSize + db::load_atomic::kFileReadBytes - 1u)
                / db::load_atomic::kFileReadBytes;
            if (totalSize >= 0x100000u
                && fileChunks <= static_cast<uint64_t>(INT32_MAX)
                && file.externalSize <= static_cast<uint32_t>(INT32_MAX))
            {
                if (db::load_atomic::RebaseProgress(
                        &g_loadProgress,
                        static_cast<int32_t>(fileChunks),
                        static_cast<int32_t>(file.externalSize))
                    != db::load_atomic::ProgressUpdateResult::Applied)
                {
                    Com_Error(ERR_DROP, "Invalid fast-file load progress state");
                    return;
                }
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
    DB_CompleteLoadingAsset();
    Load_DelayStream();
    DB_LoadDelayedImages();
    iassert(g_load.compressBufferStart);
    Com_Printf(10, "Loaded zone '%s'\n", g_load.filename);
    if (!Sys_AtomicLoad(&g_minimumFastFileLoaded)
        && I_stricmp("localized_code_post_gfx_mp", g_load.filename) == 0)
    {
        Sys_AtomicStore(&g_minimumFastFileLoaded, 1u);
    }
    DB_CancelLoadXFile();
}

bool __cdecl DB_IsMinimumFastFileLoaded()
{
    return Sys_AtomicLoad(&g_minimumFastFileLoaded) != 0;
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
    const db::load_atomic::ProgressUpdateResult resetResult =
        db::load_atomic::ConfigureProgress(&g_loadProgress, 0, 0);
    iassert(resetResult == db::load_atomic::ProgressUpdateResult::Applied);
    Sys_AtomicStore(&g_trackLoadProgress, trackLoadProgress != 0 ? 1u : 0u);
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
    db::load_atomic::ResetFileRead(&g_fileRead);
    g_load.f = f;
    g_load.filename = filename;
    g_load.zoneMem = zoneMem;
    g_load.interrupt = interrupt;
    g_load.allocType = allocType;
    g_load.compressBufferStart = buf;
    g_load.compressBufferEnd = buf + db::load_atomic::kFileBufferBytes;
    g_load.stream.next_in = buf;
    g_load.stream.avail_in = 0;
}
