#include "win_dpi.h"

#include <Windows.h>

const char *Sys_DpiAwarenessName()
{
    const DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
    if (AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return "per-monitor v2";
    switch (GetAwarenessFromDpiAwarenessContext(context))
    {
    case DPI_AWARENESS_PER_MONITOR_AWARE:
        return "per-monitor";
    case DPI_AWARENESS_SYSTEM_AWARE:
        return "system";
    case DPI_AWARENESS_UNAWARE:
        return "unaware";
    default:
        return "unknown";
    }
}
