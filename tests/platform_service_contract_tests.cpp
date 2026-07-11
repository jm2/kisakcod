#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>

static_assert(std::is_same_v<std::underlying_type_t<CriticalSection>, std::int32_t>);
static_assert(sizeof(CriticalSection) == sizeof(std::int32_t));
static_assert(std::is_standard_layout_v<FastCriticalSection>);
static_assert(sizeof(FastCriticalSection) == 8);
static_assert(offsetof(FastCriticalSection, readCount) == 0);
static_assert(offsetof(FastCriticalSection, writeCount) == 4);

using MillisecondsFunction = std::uint32_t (KISAK_CDECL *)();
using SleepFunction = void (KISAK_CDECL *)(std::uint32_t);
using InitializeCriticalSectionsFunction = void (KISAK_CDECL *)();
using CriticalSectionFunction = void (KISAK_CDECL *)(int);
using FastCriticalSectionFunction = void (KISAK_CDECL *)(FastCriticalSection *);
using FastCriticalSectionQueryFunction =
    bool (KISAK_CDECL *)(const FastCriticalSection *);

static_assert(std::is_same_v<decltype(&Sys_Milliseconds), MillisecondsFunction>);
static_assert(std::is_same_v<decltype(&Sys_MillisecondsRaw), MillisecondsFunction>);
static_assert(std::is_same_v<decltype(&Sys_Sleep), SleepFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_InitializeCriticalSections),
    InitializeCriticalSectionsFunction>);
static_assert(std::is_same_v<decltype(&Sys_EnterCriticalSection), CriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LeaveCriticalSection), CriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LockRead), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_UnlockRead), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_LockWrite), FastCriticalSectionFunction>);
static_assert(std::is_same_v<decltype(&Sys_UnlockWrite), FastCriticalSectionFunction>);
static_assert(std::is_same_v<
    decltype(&Sys_IsWriteLocked),
    FastCriticalSectionQueryFunction>);

#if defined(KISAK_MP)
static_assert(CRITSECT_CONSOLE == 0x0);
static_assert(CRITSECT_SYS_EVENT_QUEUE == 0xA);
static_assert(CRITSECT_FATAL_ERROR == 0xC);
static_assert(CRITSECT_CINEMATIC_TARGET_CHANGE == 0x13);
static_assert(CRITSECT_CBUF == 0x15);
static_assert(CRITSECT_COUNT == 0x16);
#elif defined(KISAK_SP)
static_assert(CRITSECT_CONSOLE == 0x0);
static_assert(CRITSECT_SOUND_ALLOC == 0x4);
static_assert(CRITSECT_SCRIPT_STRING == 0x13);
static_assert(CRITSECT_CBUF == 0x1F);
static_assert(CRITSECT_SYS_EVENT_QUEUE == 0x20);
static_assert(CRITSECT_FATAL_ERROR == 0x21);
static_assert(CRITSECT_GPU_FENCE == 0x22);
static_assert(CRITSECT_COUNT == 0x23);
#else
#error "Platform service contract tests require KISAK_MP or KISAK_SP"
#endif

int main()
{
	return 0;
}
