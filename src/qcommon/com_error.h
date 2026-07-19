#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

enum errorParm_t : std::int32_t
{
    ERR_FATAL = 0x0,
    ERR_DROP = 0x1,
    ERR_SERVERDISCONNECT = 0x2,
    ERR_DISCONNECT = 0x3,
    ERR_SCRIPT = 0x4,
    ERR_SCRIPT_DROP = 0x5,
    ERR_LOCALIZATION = 0x6,
    ERR_MAPLOADERRORSUMMARY = 0x7,
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(__printf__, 2, 3)))
#endif
void __cdecl Com_Error(errorParm_t code, const char *fmt, ...);
