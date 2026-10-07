// fs_shutdown_handle_tests.cpp: which file handles FS_Shutdown closes
// (FS_ShutdownClosesHandle, com_files.h). FS_Restart runs it on every map
// load; it must close read and IWD handles, whose search paths it frees,
// and keep a written OS file such as the log file, which Com_Printf goes on
// writing through its handle.

#include <cstdio>

#include <universal/com_files.h>

namespace
{
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}
} // namespace

int main()
{
    // The rule reads only the pointers' presence; neither is dereferenced.
    FILE *const os = reinterpret_cast<FILE *>(&g_failures);
    iwd_t *const iwd = reinterpret_cast<iwd_t *>(&g_failures);

    fileHandleData_t log{};
    log.handleFiles.file.o = os; // FS_FOpenFileByMode(FS_WRITE / FS_APPEND): size 0, no IWD
    Check(!FS_ShutdownClosesHandle(log), "a written OS file stays open (the log file)");

    fileHandleData_t reading = log;
    reading.fileSize = 42; // FS_FOpenFileByMode(FS_READ): the file's length
    Check(FS_ShutdownClosesHandle(reading), "a read OS file closes");

    fileHandleData_t inIwd = log;
    inIwd.zipFile = iwd; // inside an IWD, even an empty one
    Check(FS_ShutdownClosesHandle(inIwd), "a file inside an IWD closes, size or not");

    fileHandleData_t closed{};
    closed.fileSize = 42;
    Check(!FS_ShutdownClosesHandle(closed), "a free handle is left alone");

    if (!g_failures)
        std::printf("fs shutdown handles: all checks passed\n");
    return g_failures ? 1 : 0;
}
