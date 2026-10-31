// posix_desktop.cpp: Posix_DialogCanShow (posix_desktop.h). Its own file
// because the CoreGraphics headers clash with the Win32 shim the window code
// compiles against.

#include "posix_desktop.h"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

bool Posix_DialogCanShow()
{
#if defined(__APPLE__)
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session)
        return false;
    bool visible = true;
    const auto onConsole = static_cast<CFBooleanRef>(CFDictionaryGetValue(session, kCGSessionOnConsoleKey));
    if (onConsole && !CFBooleanGetValue(onConsole))
        visible = false;
    // Not in the SDK headers, but the key the window server reports.
    const auto locked = static_cast<CFBooleanRef>(CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked")));
    if (locked && CFBooleanGetValue(locked))
        visible = false;
    CFRelease(session);
    return visible;
#else
    return true;
#endif
}
