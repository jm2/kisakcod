#include "scr_stringlist.h"

#include <string.h> // strlen()
#include <limits>

#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <universal/assertive.h>
#include <universal/com_constantconfigstrings.h>
#include <universal/profile.h>
#include <universal/q_shared.h>
#include <universal/sys_atomic.h>

#include "scr_memorytree.h"
#include "scr_string_atomic.h"
#include "scr_string_transaction.h"

// Keep packed string ownership and payload layout private to this translation
// unit. External declarations may pass opaque RefString pointers through
// legacy signatures, but cannot bypass the authenticated ownership boundary by
// reading or writing packed fields directly.
struct RefString
{
	volatile std::uint32_t data;
	char str[1];
};
RUNTIME_OFFSET(RefString, data, 0, 0);
RUNTIME_OFFSET(RefString, str, 4, 4);
static_assert(
	std::is_same_v<decltype(RefString::data), volatile std::uint32_t>);
static_assert(std::is_standard_layout_v<RefString>);
static_assert(std::is_trivially_copyable_v<RefString>);

scrStringDebugGlob_t* scrStringDebugGlob;
static scrStringDebugGlob_t scrStringDebugGlobBuf;
static scrStringGlob_t scrStringGlob; // 0x244E300

static bool SL_TryResolveLegacyTransferTargetNoReport(
	uint32_t stringValue,
	RefString** outRefString,
	uint32_t* outByteCount) noexcept;

namespace
{
[[nodiscard]] volatile std::uint32_t *SL_RefStringWord(
	RefString *const refString) noexcept
{
	return &refString->data;
}

[[nodiscard]] const volatile std::uint32_t *SL_RefStringWord(
	const RefString *const refString) noexcept
{
	return &refString->data;
}

enum class SL_OwnershipBatchLifecycle : std::uint8_t
{
    Idle,
    Active,
    Poisoned,
    Frozen,
};

std::uint64_t sl_nextOwnershipBatchSerial = 0;
std::uintptr_t sl_activeOwnershipBatchAddress = 0;
std::uint64_t sl_activeOwnershipBatchSerial = 0;
std::uintptr_t sl_activeOwnershipBatchAddressMirror = 0;
std::uint64_t sl_activeOwnershipBatchSerialMirror = 0;
std::uintptr_t sl_activeOwnershipBatchNestedLeaseAddress = 0;
std::uintptr_t sl_activeOwnershipBatchNestedLeaseAddressMirror = 0;
SL_OwnershipBatchLifecycle sl_ownershipBatchLifecycle =
    SL_OwnershipBatchLifecycle::Idle;
SL_OwnershipBatchLifecycle sl_ownershipBatchLifecycleMirror =
    SL_OwnershipBatchLifecycle::Idle;

thread_local std::uintptr_t sl_retainedOwnershipBatchAddress = 0;
thread_local std::uintptr_t sl_retainedOwnershipBatchAddressMirror = 0;
thread_local std::uint64_t sl_retainedOwnershipBatchSerial = 0;
thread_local std::uint64_t sl_retainedOwnershipBatchSerialMirror = 0;
thread_local std::uintptr_t sl_retainedOwnershipBatchNestedLeaseAddress = 0;
thread_local std::uintptr_t
    sl_retainedOwnershipBatchNestedLeaseAddressMirror = 0;

constexpr std::uint64_t kAbandonedOwnershipBatchPoison =
    UINT64_C(0x534C5F4142414E44);
constexpr std::uint64_t kAbandonedOwnershipBatchPoisonMirror =
    UINT64_C(0xACB3A0BEBDBEB1BB);
std::uint64_t sl_abandonedOwnershipBatchPoison = 0;
std::uint64_t sl_abandonedOwnershipBatchPoisonMirror = 0;

// The transaction serializer permits only one registry ownership operation
// process-wide.  Keep shutdown's exhaustive ID snapshot in fixed BSS rather
// than allocating or placing a 40 KiB array on an engine thread's stack.
std::uint16_t sl_registryOwnershipSweepIds[STRINGLIST_SIZE]{};

void SL_ScrubRegistryOwnershipSweepIdsNoReport(
	const uint32_t count) noexcept
{
	volatile uint16_t* const ids = sl_registryOwnershipSweepIds;
	const uint32_t boundedCount = count < STRINGLIST_SIZE
		? count
		: STRINGLIST_SIZE;
	for (uint32_t index = 0; index < boundedCount; ++index)
		ids[index] = 0;
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
script_string::OwnershipValidationCounters sl_ownershipValidationCounters{};
#endif

[[nodiscard]] bool SL_IsOwnershipBatchBoundaryFrozenLocked() noexcept
{
    // Either durable poison word is terminal activity, so a torn publication
    // cannot reopen the ownership boundary.
    return sl_abandonedOwnershipBatchPoison != 0
        || sl_abandonedOwnershipBatchPoisonMirror != 0
        || sl_ownershipBatchLifecycle
            == SL_OwnershipBatchLifecycle::Frozen
        || sl_ownershipBatchLifecycleMirror
            == SL_OwnershipBatchLifecycle::Frozen;
}

[[nodiscard]] bool
SL_HasRetainedOwnershipBatchAuthenticationLocked() noexcept
{
    return sl_retainedOwnershipBatchAddress != 0
        || sl_retainedOwnershipBatchAddressMirror != 0
        || sl_retainedOwnershipBatchSerial != 0
        || sl_retainedOwnershipBatchSerialMirror != 0
        || sl_retainedOwnershipBatchNestedLeaseAddress != 0
        || sl_retainedOwnershipBatchNestedLeaseAddressMirror != 0;
}

[[nodiscard]] bool SL_HasOwnershipBatchRegistryActivityLocked() noexcept
{
	return sl_activeOwnershipBatchAddress != 0
		|| sl_activeOwnershipBatchSerial != 0
		|| sl_activeOwnershipBatchAddressMirror != 0
		|| sl_activeOwnershipBatchSerialMirror != 0
        || sl_activeOwnershipBatchNestedLeaseAddress != 0
        || sl_activeOwnershipBatchNestedLeaseAddressMirror != 0
        || sl_ownershipBatchLifecycle != SL_OwnershipBatchLifecycle::Idle
        || sl_ownershipBatchLifecycleMirror
            != SL_OwnershipBatchLifecycle::Idle
        || SL_HasRetainedOwnershipBatchAuthenticationLocked()
        || SL_IsOwnershipBatchBoundaryFrozenLocked();
}

[[nodiscard]] bool SL_IsOwnershipBatchRegistryConsistentLocked() noexcept
{
	if (SL_IsOwnershipBatchBoundaryFrozenLocked())
        return false;

	if (!SL_HasOwnershipBatchRegistryActivityLocked())
		return true;

	if (sl_activeOwnershipBatchAddress == 0
        || sl_activeOwnershipBatchSerial == 0
		|| sl_activeOwnershipBatchAddressMirror == 0
		|| sl_activeOwnershipBatchSerialMirror == 0
		|| sl_activeOwnershipBatchAddress
			!= sl_activeOwnershipBatchAddressMirror
		|| sl_activeOwnershipBatchSerial
			!= sl_activeOwnershipBatchSerialMirror
        || sl_activeOwnershipBatchNestedLeaseAddress == 0
        || sl_activeOwnershipBatchNestedLeaseAddress
            != sl_activeOwnershipBatchNestedLeaseAddressMirror
        || sl_ownershipBatchLifecycle
            != sl_ownershipBatchLifecycleMirror
        || (sl_ownershipBatchLifecycle
                != SL_OwnershipBatchLifecycle::Active
            && sl_ownershipBatchLifecycle
                != SL_OwnershipBatchLifecycle::Poisoned)
        || sl_retainedOwnershipBatchAddress
            != sl_activeOwnershipBatchAddress
        || sl_retainedOwnershipBatchAddressMirror
            != sl_activeOwnershipBatchAddressMirror
        || sl_retainedOwnershipBatchSerial
            != sl_activeOwnershipBatchSerial
        || sl_retainedOwnershipBatchSerialMirror
            != sl_activeOwnershipBatchSerialMirror
        || sl_retainedOwnershipBatchNestedLeaseAddress
            != sl_activeOwnershipBatchNestedLeaseAddress
        || sl_retainedOwnershipBatchNestedLeaseAddressMirror
            != sl_activeOwnershipBatchNestedLeaseAddressMirror)
	{
		return false;
	}

	// Registry consistency is deliberately pointer-free. Stored stack addresses
	// are only compared with an explicit live argument below and are never
	// converted back to pointers or dereferenced by generic rejection paths.
	return true;
}

[[nodiscard]] bool SL_IsAuthorizedOwnershipLeaseLocked(
	MT_ValidationLease *const lease) noexcept
{
	if (!lease || !SL_IsOwnershipBatchRegistryConsistentLocked())
        return false;
    const std::uintptr_t leaseAddress =
        reinterpret_cast<std::uintptr_t>(lease);
    return sl_activeOwnershipBatchNestedLeaseAddress == leaseAddress
        && sl_activeOwnershipBatchNestedLeaseAddressMirror == leaseAddress;
}

void SL_ClearOwnershipBatchRegistryLocked() noexcept
{
    sl_activeOwnershipBatchAddress = 0;
    sl_activeOwnershipBatchSerial = 0;
    sl_activeOwnershipBatchAddressMirror = 0;
    sl_activeOwnershipBatchSerialMirror = 0;
    sl_activeOwnershipBatchNestedLeaseAddress = 0;
    sl_activeOwnershipBatchNestedLeaseAddressMirror = 0;
    sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Idle;
    sl_ownershipBatchLifecycleMirror = SL_OwnershipBatchLifecycle::Idle;
}

void SL_ClearRetainedOwnershipBatchAuthenticationLocked() noexcept
{
    sl_retainedOwnershipBatchAddress = 0;
    sl_retainedOwnershipBatchAddressMirror = 0;
    sl_retainedOwnershipBatchSerial = 0;
    sl_retainedOwnershipBatchSerialMirror = 0;
    sl_retainedOwnershipBatchNestedLeaseAddress = 0;
    sl_retainedOwnershipBatchNestedLeaseAddressMirror = 0;
}

void SL_FreezeOwnershipBatchBoundaryLocked() noexcept
{
    sl_abandonedOwnershipBatchPoison = kAbandonedOwnershipBatchPoison;
    sl_abandonedOwnershipBatchPoisonMirror =
        kAbandonedOwnershipBatchPoisonMirror;
    sl_activeOwnershipBatchAddress = 0;
    sl_activeOwnershipBatchSerial = 0;
    sl_activeOwnershipBatchAddressMirror = 0;
    sl_activeOwnershipBatchSerialMirror = 0;
    sl_activeOwnershipBatchNestedLeaseAddress = 0;
    sl_activeOwnershipBatchNestedLeaseAddressMirror = 0;
    sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Frozen;
    sl_ownershipBatchLifecycleMirror = SL_OwnershipBatchLifecycle::Frozen;
}

[[nodiscard]] bool SL_IsOwnershipBatchActiveNoReport() noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool active = SL_HasOwnershipBatchRegistryActivityLocked();
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return active;
}
} // namespace

#define SCR_SYS_GAME 1

static bool SL_IsValidUserMask(uint32_t user, bool allowZero)
{
	if (user > (std::numeric_limits<uint8_t>::max)())
		return false;
	return user ? (user & (user - 1)) == 0 : allowZero;
}

static uint8_t SL_UserReferenceCount(uint8_t users) noexcept
{
	uint8_t count = 0;
	while (users)
	{
		count += static_cast<uint8_t>(users & 1u);
		users = static_cast<uint8_t>(users >> 1);
	}
	return count;
}

bool SL_TryResetCanonicalStringState(
	short (&canonicalStrings)[SL_MAX_STRING_INDEX],
	uint16_t *const canonicalCount) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	// Reject the retained/frozen boundary before dereferencing either supplied
	// output. This is the narrow callback-free gate used by SL_BeginLoadScripts.
	if (SL_HasOwnershipBatchRegistryActivityLocked() || !canonicalCount)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return false;
	}
	memset(canonicalStrings, 0, sizeof(canonicalStrings));
	*canonicalCount = 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return true;
}

static uint32_t SL_DebugRefCount(uint32_t stringValue)
{
	if (!scrStringDebugGlob)
		return 0;
	return Sys_AtomicLoad(&scrStringDebugGlob->refCount[stringValue]);
}

static void SL_DebugAddRef(uint32_t stringValue)
{
	if (!scrStringDebugGlob)
		return;

	const uint32_t refCount = SL_DebugRefCount(stringValue);
	iassert(refCount < (std::numeric_limits<uint16_t>::max)());
	if (refCount >= (std::numeric_limits<uint16_t>::max)())
		return;

	Sys_AtomicIncrement(&scrStringDebugGlob->totalRefCount);
	Sys_AtomicIncrement(&scrStringDebugGlob->refCount[stringValue]);
}

static void SL_DebugRemoveRef(uint32_t stringValue)
{
	if (!scrStringDebugGlob)
		return;

	const uint32_t totalRefCount =
		Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount);
	const uint32_t refCount = SL_DebugRefCount(stringValue);
	iassert(totalRefCount && refCount);
	if (!totalRefCount || !refCount)
		return;

	Sys_AtomicDecrement(&scrStringDebugGlob->totalRefCount);
	Sys_AtomicDecrement(&scrStringDebugGlob->refCount[stringValue]);
}

static bool SL_FreeString(uint32_t stringValue, RefString* refStr, uint32_t len);

namespace
{
enum class SL_ValidationScope : uint8_t
{
	Complete,
	LegacyLocal,
	Leased,
};

[[nodiscard]] bool SL_IsValidationAuthorityWellFormed(
	const SL_ValidationScope scope,
	const MT_ValidationLease *const validationLease,
	const MT_ValidationLeaseAdmission *const admission) noexcept
{
	return scope == SL_ValidationScope::Leased
		? validationLease != nullptr && admission != nullptr
		: validationLease == nullptr && admission == nullptr;
}

constexpr uint32_t kHashEntryBits = HASH_STAT_MASK | UINT32_C(0xFFFF);

bool SL_IsHashEntryEncodingValidNoReport(const HashEntry &entry) noexcept
{
	return (entry.status_next & ~kHashEntryBits) == 0;
}

bool SL_TryGetAllocatedStringByteCountForScopeNoReport(
	uint32_t stringValue,
	RefString** outRefString,
	uint32_t* outByteCount,
	SL_ValidationScope scope,
	MT_ValidationLease* validationLease = nullptr,
	const MT_ValidationLeaseAdmission* admission = nullptr) noexcept;
bool SL_IsLegacyLookupHashStateValidNoReport(uint32_t hash) noexcept;
bool SL_IsCompleteSystemSweepStateValidNoReport() noexcept;
bool SL_TryFreeSystemSweepEntryNoReport(
	uint32_t owningHash,
	uint32_t targetIndex,
	uint32_t previousIndex,
	uint32_t stringValue,
	RefString* refString,
	uint32_t byteCount) noexcept;
bool SL_CanDebugRemoveRefNoReport(uint32_t stringValue) noexcept;
void SL_DebugRemoveRefNoReport(uint32_t stringValue) noexcept;
bool SL_TryRemoveRefToStringLockedNoReport(
	uint32_t stringValue,
	uint32_t expectedByteCount,
	bool enforceExpectedByteCount) noexcept;
} // namespace

static uint32_t __cdecl GetHashCode(const char *str, uint32_t len)
{
	uint32_t hash; // [esp+4h] [ebp-8h]

	if (len >= 0x100)
	{
		hash = len >> 2;
	}
	else
	{
		hash = 0;
		while (len)
		{
			hash = *str++ + 31 * hash;
			--len;
		}
	}

	return hash % (STRINGLIST_SIZE-1) + 1;
}

uint32_t __cdecl Scr_AllocString(char *s, int sys)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	iassert(sys == SCR_SYS_GAME);
	return SL_GetString(s, 1);
}

void SL_Init()
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	const bool stateAlreadyInitialized = scrStringGlob.inited != 0
		|| scrStringDebugGlob != nullptr;
	if (stateAlreadyInitialized)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		iassert(!stateAlreadyInitialized);
		return;
	}

	MT_Init();

	scrStringGlob.nextFreeEntry = nullptr;
	scrStringGlob.hashTable[0].status_next = 0;
	uint32_t prev = 0;
	for (uint32_t hash = 1; hash < STRINGLIST_SIZE; ++hash)
	{
		iassert(!(hash & HASH_STAT_MASK));
		scrStringGlob.hashTable[hash].status_next = HASH_STAT_FREE; // (0)
		scrStringGlob.hashTable[prev].status_next |= hash;
		scrStringGlob.hashTable[hash].u.prev = prev;
		prev = hash;
	}

	scrStringGlob.hashTable[0].u.prev = prev;
	SL_InitCheckLeaks();
	scrStringGlob.inited = 1;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SL_InitCheckLeaks()
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	const bool debugAlreadyInitialized = scrStringDebugGlob != nullptr;
	if (debugAlreadyInitialized)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		iassert(!debugAlreadyInitialized);
		return;
	}

	Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));
	scrStringDebugGlob = &scrStringDebugGlobBuf;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

static uint32_t SL_ConvertFromRefString(RefString *refString)
{
	return ((char *)refString - scrMemTreePub.mt_buffer) / MT_NODE_SIZE;
}

static RefString* SL_GetRefStringNoReport(
	const uint32_t stringValue) noexcept
{
	return reinterpret_cast<RefString*>(
		&scrMemTreePub.mt_buffer[MT_NODE_SIZE * stringValue]);
}

static bool SL_CanDebugAddRefNoReport(uint32_t stringValue) noexcept
{
	if (!script_string::IsCurrentRuntimeStringId(stringValue))
		return false;
	if (!scrStringDebugGlob)
		return true;
	return SL_DebugRefCount(stringValue)
		< (std::numeric_limits<uint16_t>::max)()
		&& Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount)
		< (std::numeric_limits<uint32_t>::max)();
}

static bool SL_CanDebugInitializeStringNoReport(
	const uint32_t stringValue) noexcept
{
	if (!script_string::IsCurrentRuntimeStringId(stringValue))
		return false;
	if (!scrStringDebugGlob)
		return true;
	return SL_DebugRefCount(stringValue) == 0
		&& Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount)
			< (std::numeric_limits<uint32_t>::max)();
}

static bool SL_IsDebugOwnershipExactNoReport(
	const uint32_t stringValue,
	const uint32_t packed) noexcept
{
	if (!scrStringDebugGlob)
		return true;
	const uint32_t refCount = scr_string_atomic::RefCount(packed);
	const uint32_t debugRefCount = SL_DebugRefCount(stringValue);
	return debugRefCount == refCount
		&& Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount)
			>= debugRefCount;
}

static void SL_DebugAddRefNoReport(uint32_t stringValue) noexcept
{
	if (!scrStringDebugGlob)
		return;
	Sys_AtomicIncrement(&scrStringDebugGlob->totalRefCount);
	Sys_AtomicIncrement(&scrStringDebugGlob->refCount[stringValue]);
}

static bool SL_TryAddUserInternalNoReport(
	RefString* const refStr,
	const uint32_t user,
	MT_ValidationLease* const validationLease = nullptr) noexcept
{
	if (SL_HasOwnershipBatchRegistryActivityLocked()
		&& !SL_IsAuthorizedOwnershipLeaseLocked(validationLease))
	{
		return false;
	}
	if (!refStr || !SL_IsValidUserMask(user, true))
		return false;
	const uint8_t userByte = static_cast<uint8_t>(user);
	const uint32_t before =
		scr_string_atomic::Load(SL_RefStringWord(refStr));
	const bool addsReference =
		userByte == 0 || (scr_string_atomic::User(before) & userByte) == 0;
	const uint32_t stringValue = SL_ConvertFromRefString(refStr);
	if (addsReference && !SL_CanDebugAddRefNoReport(stringValue))
		return false;

	const scr_string_atomic::AddUserRefResult result =
		scr_string_atomic::AddUserRef(SL_RefStringWord(refStr), userByte);
	if (result == scr_string_atomic::AddUserRefResult::Invalid)
		return false;
	if (result == scr_string_atomic::AddUserRefResult::Added)
		SL_DebugAddRefNoReport(stringValue);
	return true;
}

static bool SL_AddUserInternal(RefString* const refStr, const uint32_t user)
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return false;
	}
	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	const uintptr_t refStringAddress = reinterpret_cast<uintptr_t>(refStr);
	const bool addressValid = refStr != nullptr && memoryBegin != 0
		&& memoryBegin <= (std::numeric_limits<uintptr_t>::max)() - MT_SIZE
		&& refStringAddress > memoryBegin
		&& refStringAddress < memoryBegin + MT_SIZE
		&& (refStringAddress - memoryBegin) % MT_NODE_SIZE == 0;
	RefString* resolvedRefString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = SL_IsValidUserMask(user, true)
		&& addressValid
		&& SL_TryResolveLegacyTransferTargetNoReport(
			static_cast<uint32_t>(
				(refStringAddress - memoryBegin) / MT_NODE_SIZE),
			&resolvedRefString,
			&byteCount)
		&& resolvedRefString == refStr
		&& SL_TryAddUserInternalNoReport(resolvedRefString, user);
	(void)byteCount;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return valid;
}

void SL_AddRefToString(uint32_t stringValue)
{
	PROF_SCOPED("SL_AddRefToString");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	RefString* refStr = nullptr;
	uint32_t byteCount = 0;
	const bool added =
		SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refStr, &byteCount)
		&& SL_TryAddUserInternalNoReport(refStr, 0);
	(void)byteCount;
	if (!added)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		Com_Error(ERR_DROP, "invalid script string reference increment");
		return;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SL_CheckExists(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	const bool exists = script_string::IsCurrentRuntimeStringId(stringValue)
		&& (!scrStringDebugGlob || SL_DebugRefCount(stringValue) != 0);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	(void)exists;
	iassert(exists);
}

static void SL_CheckLeaks()
{
	if (SL_HasOwnershipBatchRegistryActivityLocked())
		return;
	if (scrStringDebugGlob)
	{
		if (!scrStringDebugGlob->ignoreLeaks)
		{
			for (int i = 1; i < 65536; ++i)
			{
				iassert(!SL_DebugRefCount(i));
			}
			iassert(!Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount));
		}
		scrStringDebugGlob = NULL;
	}
}

void SL_Shutdown()
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	if (scrStringGlob.inited)
	{
		scrStringGlob.inited = 0;
		SL_CheckLeaks();
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SL_ShutdownSystem(uint32_t user)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	iassert(SL_IsValidUserMask(user, false));
	const uint8_t userByte = static_cast<uint8_t>(user);
	if (!SL_IsValidUserMask(user, false))
	{
		Com_Error(ERR_DROP, "invalid script string shutdown user mask");
		return;
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}

	// Pass 1 authenticates the complete table/debug/allocator snapshot before
	// any ownership change. The held string lock keeps table writers out during
	// pass 2. Per-entry allocation/free transactions remain fail-closed; an
	// unexpected pass-2 allocator failure stops the sweep after any earlier
	// independently committed entries rather than attempting unsafe global
	// rollback across slots that may already have been returned to the allocator.
	bool invalidTransition = !SL_IsCompleteSystemSweepStateValidNoReport();
	if (!invalidTransition)
		scrStringGlob.nextFreeEntry = nullptr;
	for (uint32_t owningHash = 1;
		owningHash < STRINGLIST_SIZE && !invalidTransition;
		++owningHash)
	{
		if ((scrStringGlob.hashTable[owningHash].status_next & HASH_STAT_MASK)
			!= HASH_STAT_HEAD)
		{
			continue;
		}

		uint32_t targetIndex = owningHash;
		uint32_t previousIndex = owningHash;
		bool chainDone = false;
		for (uint32_t visited = 0;
			visited < STRINGLIST_SIZE && !invalidTransition;
			++visited)
		{
			HashEntry &entry = scrStringGlob.hashTable[targetIndex];
			const uint32_t nextIndex =
				static_cast<uint16_t>(entry.status_next);
			const uint32_t stringValue = entry.u.prev;
			RefString* refStr = nullptr;
			uint32_t len = 0;
			if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
					stringValue,
					&refStr,
					&len,
					SL_ValidationScope::LegacyLocal))
			{
				invalidTransition = true;
				break;
			}
			const uint32_t packedBefore =
				scr_string_atomic::Load(SL_RefStringWord(refStr));
			const scr_string_atomic::RemoveUserRefResult result =
				scr_string_atomic::RemoveUserRef(
					SL_RefStringWord(refStr), userByte);
			if (result.status == scr_string_atomic::RemoveUserRefStatus::NotPresent)
			{
				if (nextIndex == owningHash)
				{
					chainDone = true;
					break;
				}
				previousIndex = targetIndex;
				targetIndex = nextIndex;
				continue;
			}
			if (result.status == scr_string_atomic::RemoveUserRefStatus::Invalid)
			{
				invalidTransition = true;
				break;
			}

			SL_DebugRemoveRefNoReport(stringValue);
			if (result.reachedZero)
			{
				if (!SL_TryFreeSystemSweepEntryNoReport(
						owningHash,
						targetIndex,
						previousIndex,
						stringValue,
						refStr,
						len))
				{
					// This entry has not been unlinked/freed on rejection, so its
					// exact owner/debug state can still be restored safely.
					Sys_AtomicStore(SL_RefStringWord(refStr), packedBefore);
					SL_DebugAddRefNoReport(stringValue);
					invalidTransition = true;
					break;
				}
				if (targetIndex == owningHash)
				{
					if (nextIndex == owningHash)
					{
						chainDone = true;
						break;
					}
					targetIndex = owningHash;
					previousIndex = owningHash;
					continue;
				}
				if (nextIndex == owningHash)
				{
					chainDone = true;
					break;
				}
				targetIndex = nextIndex;
				continue;
			}

			if (nextIndex == owningHash)
			{
				chainDone = true;
				break;
			}
			previousIndex = targetIndex;
			targetIndex = nextIndex;
		}
		if (!invalidTransition && !chainDone)
			invalidTransition = true;
	}

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (invalidTransition)
		Com_Error(ERR_DROP, "invalid script string user-reference removal");
}

int SL_IsLowercaseString(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = SL_TryResolveLegacyTransferTargetNoReport(
		stringValue, &refString, &byteCount);
	if (!valid)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		iassert(valid);
		return 0;
	}

	for (uint32_t index = 0; index + 1 < byteCount; ++index)
	{
		const unsigned char value =
			static_cast<unsigned char>(refString->str[index]);
		if (value != static_cast<unsigned char>(tolower(value)))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return 0;
		}
	}

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return 1;
}

void SL_TransferSystem(uint32_t from, uint32_t to)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	iassert(SL_IsValidUserMask(from, false));
	iassert(SL_IsValidUserMask(to, false));
	const uint8_t fromByte = static_cast<uint8_t>(from);
	const uint8_t toByte = static_cast<uint8_t>(to);
	if (!SL_IsValidUserMask(from, false)
		|| !SL_IsValidUserMask(to, false))
	{
		Com_Error(ERR_DROP, "invalid script string transfer user mask");
		return;
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}

	// The complete first pass makes every physical owner ID safe to consume in
	// pass 2. The held string lock keeps the hash topology stable for transfer.
	bool invalidTransition = !SL_IsCompleteSystemSweepStateValidNoReport();
	for (uint32_t hash = 1;
		hash < STRINGLIST_SIZE && !invalidTransition;
		++hash)
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) != 0)
		{
			const uint32_t stringValue = scrStringGlob.hashTable[hash].u.prev;
			RefString* refStr = nullptr;
			uint32_t byteCount = 0;
			if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
					stringValue,
					&refStr,
					&byteCount,
					SL_ValidationScope::LegacyLocal))
			{
				invalidTransition = true;
				break;
			}
			(void)byteCount;
			const scr_string_atomic::TransferUserResult result =
				scr_string_atomic::TransferUser(
					SL_RefStringWord(refStr), fromByte, toByte);
			if (result == scr_string_atomic::TransferUserResult::ReleasedDuplicate)
				SL_DebugRemoveRefNoReport(stringValue);
			else if (result == scr_string_atomic::TransferUserResult::Invalid)
				invalidTransition = true;
		}
	}

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (invalidTransition)
		Com_Error(ERR_DROP, "invalid script string user transfer");
}

uint32_t SL_GetString_(const char* str, uint32_t user, int type)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	return SL_GetStringOfSize(str, user, strlen(str) + 1, type);
}

namespace
{
bool SL_IsInternHashStateValidNoReport(uint32_t hash) noexcept;
bool SL_IsLegacyInternHashStateValidNoReport(uint32_t hash) noexcept;
bool SL_IsInternHashStateValidForScopeNoReport(
	uint32_t hash,
	SL_ValidationScope scope,
	bool validateFreeList,
	MT_ValidationLease* validationLease = nullptr,
	const MT_ValidationLeaseAdmission* admission = nullptr) noexcept;
void SL_SetFreeListCertificateMemberNoReport(
	uint32_t entryIndex,
	bool member) noexcept;

bool SL_IsRepresentableRefStringBytesNoReport(
	const char* const bytes,
	const uint32_t byteCount) noexcept
{
	if (!bytes || byteCount == 0 || byteCount > UINT32_C(65531)
		|| bytes[byteCount - 1] != '\0')
	{
		return false;
	}

	// RefString stores only the low eight bits of byteCount. Readers recover
	// the complete size by checking NUL positions congruent to the final byte
	// modulo 256. An earlier NUL at one of those positions would make the
	// allocation decode to the wrong size and leave the string table unsafe.
	uint32_t terminatorOffset = static_cast<uint8_t>(byteCount - 1);
	while (terminatorOffset < byteCount - 1)
	{
		if (bytes[terminatorOffset] == '\0')
			return false;
		terminatorOffset += UINT32_C(256);
	}
	return terminatorOffset == byteCount - 1;
}

enum class SL_InternStatus : uint8_t
{
	Success,
	InvalidArgumentNoChange,
	PrimaryTableCapacityNoChange,
	RelocatedTableCapacityNoChange,
	MemoryCapacityNoChange,
	RefCountExhaustedNoChange,
	UnsafeCleanupFailure,
	UnsafeFailure,
};

MT_AllocIndexStatus SL_TryAllocateStringMemoryNoReport(
	const int numBytes,
	const int type,
	uint16_t* const outIndex,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	if (!SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission))
	{
		return MT_AllocIndexStatus::UnsafeFailure;
	}
	switch (scope)
	{
	case SL_ValidationScope::Complete:
		return validationLease
			? MT_AllocIndexStatus::UnsafeFailure
			: MT_TryAllocIndex(numBytes, type, outIndex);
	case SL_ValidationScope::LegacyLocal:
		return validationLease
			? MT_AllocIndexStatus::UnsafeFailure
			: MT_TryAllocIndexLegacy(numBytes, type, outIndex);
	case SL_ValidationScope::Leased:
		return MT_TryAllocIndexLeased(
			*validationLease, numBytes, type, outIndex, *admission);
	}
	return MT_AllocIndexStatus::UnsafeFailure;
}

MT_FreeIndexStatus SL_TryFreeStringMemoryNoReport(
	const uint32_t stringValue,
	const int numBytes,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	if (!SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission))
	{
		return MT_FreeIndexStatus::UnsafeFailure;
	}
	switch (scope)
	{
	case SL_ValidationScope::Complete:
		return validationLease
			? MT_FreeIndexStatus::UnsafeFailure
			: MT_TryFreeIndex(stringValue, numBytes);
	case SL_ValidationScope::LegacyLocal:
		return validationLease
			? MT_FreeIndexStatus::UnsafeFailure
			: MT_TryFreeIndexLegacy(stringValue, numBytes);
	case SL_ValidationScope::Leased:
		return MT_TryFreeIndexLeased(
			*validationLease, stringValue, numBytes, *admission);
	}
	return MT_FreeIndexStatus::UnsafeFailure;
}

SL_InternStatus SL_TryInternStringOfSizeWithValidation(
	const char* str,
	uint32_t user,
	uint32_t len,
	int type,
	uint32_t* outStringValue,
	const SL_ValidationScope validationScope,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	if (!SL_IsValidationAuthorityWellFormed(
			validationScope, validationLease, admission))
	{
		return SL_InternStatus::UnsafeFailure;
	}
	PROF_SCOPED("SL_GetStringOfSize");
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool entryAuthorized =
		SL_HasOwnershipBatchRegistryActivityLocked()
			? (validationScope == SL_ValidationScope::Leased
				&& SL_IsAuthorizedOwnershipLeaseLocked(validationLease))
			: (validationScope != SL_ValidationScope::Leased
				&& validationLease == nullptr);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!entryAuthorized)
		return SL_InternStatus::UnsafeFailure;

	const uint8_t userByte = static_cast<uint8_t>(user);
	if (!str || !outStringValue || len == 0 || len > UINT32_C(65531)
		|| type <= 0 || type >= 22
		|| !SL_IsValidUserMask(user, true))
		return SL_InternStatus::InvalidArgumentNoChange;

	uint32_t hash = GetHashCode(str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool registryActive =
		SL_HasOwnershipBatchRegistryActivityLocked();
	const bool leasedAccess =
		validationScope == SL_ValidationScope::Leased
		&& SL_IsAuthorizedOwnershipLeaseLocked(validationLease);
	if ((registryActive && !leasedAccess)
		|| (!registryActive
			&& (validationScope == SL_ValidationScope::Leased
				|| validationLease != nullptr)))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return SL_InternStatus::UnsafeFailure;
	}
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return SL_InternStatus::InvalidArgumentNoChange;
	}
	const bool hashStateValid =
		SL_IsInternHashStateValidForScopeNoReport(
			hash, validationScope, true, validationLease, admission);
	if (!hashStateValid)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return SL_InternStatus::UnsafeFailure;
	}

	RefString* refStr = NULL;

	uint32_t stringValue = 0;

	uint32_t prev;
	uint32_t next;
	uint32_t newIndex;

	HashEntry *entry = &scrStringGlob.hashTable[hash];
	HashEntry *newEntry;

	if ((entry->status_next & HASH_STAT_MASK) == HASH_STAT_HEAD)
	{
		uint32_t candidateByteCount = 0;
		if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
				entry->u.prev,
				&refStr,
				&candidateByteCount,
				validationScope,
				validationLease,
				admission))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return SL_InternStatus::UnsafeFailure;
		}

		// Check if this string is already stored, if it matches the string at this particular hash lookup, and return existing entry if so.
		if (candidateByteCount == len
			&& !memcmp(refStr->str, str, len))
		{
			if (!SL_TryAddUserInternalNoReport(
					refStr, user, validationLease))
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return SL_InternStatus::RefCountExhaustedNoChange;
			}

			stringValue = entry->u.prev;
			
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			*outStringValue = stringValue;
			return SL_InternStatus::Success;
		}

		prev = hash;
		newIndex = (uint16_t)entry->status_next;

		for (newEntry = &scrStringGlob.hashTable[newIndex]; newEntry != entry; newEntry = &scrStringGlob.hashTable[newIndex])
		{
			if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
					newEntry->u.prev,
					&refStr,
					&candidateByteCount,
					validationScope,
					validationLease,
					admission))
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return SL_InternStatus::UnsafeFailure;
			}

			if (candidateByteCount == len
				&& !memcmp(refStr->str, str, len))
			{
				// Do not reorder the collision chain until the ownership CAS has
				// succeeded. A rejected no-change acquire must not mutate even
				// lookup topology.
				if (!SL_TryAddUserInternalNoReport(
						refStr, user, validationLease))
				{
					Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
					return SL_InternStatus::RefCountExhaustedNoChange;
				}
				scrStringGlob.hashTable[prev].status_next =
					static_cast<uint16_t>(newEntry->status_next)
					| (scrStringGlob.hashTable[prev].status_next
						& HASH_STAT_MASK);
				newEntry->status_next =
					static_cast<uint16_t>(entry->status_next)
					| (newEntry->status_next & HASH_STAT_MASK);
				entry->status_next = newIndex
					| (entry->status_next & HASH_STAT_MASK);
				stringValue = newEntry->u.prev;
				newEntry->u.prev = entry->u.prev;
				entry->u.prev = stringValue;

				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				*outStringValue = stringValue;
				return SL_InternStatus::Success;
			}
			prev = newIndex;
			newIndex = (uint16_t)newEntry->status_next;
		} //for()

		newIndex = scrStringGlob.hashTable[0].status_next;

		if (!newIndex)
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return SL_InternStatus::PrimaryTableCapacityNoChange;
		}

		newEntry = &scrStringGlob.hashTable[newIndex];
		if ((newEntry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return SL_InternStatus::UnsafeFailure;
		}

		uint16_t allocatedIndex = 0;
		const MT_AllocIndexStatus allocationStatus =
			SL_TryAllocateStringMemoryNoReport(
				static_cast<int>(len + 4),
				type,
				&allocatedIndex,
				validationScope,
				validationLease,
				admission);
		if (allocationStatus != MT_AllocIndexStatus::Success)
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return allocationStatus
					== MT_AllocIndexStatus::InsufficientCapacityNoChange
				? SL_InternStatus::MemoryCapacityNoChange
				: SL_InternStatus::UnsafeFailure;
		}
		stringValue = allocatedIndex;
		if (!SL_CanDebugInitializeStringNoReport(stringValue))
		{
			const MT_FreeIndexStatus cleanupStatus =
				SL_TryFreeStringMemoryNoReport(
					stringValue,
					static_cast<int>(len + 4),
					validationScope,
					validationLease,
					admission);
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return cleanupStatus == MT_FreeIndexStatus::Success
				? SL_InternStatus::UnsafeFailure
				: SL_InternStatus::UnsafeCleanupFailure;
		}

		uint32_t newNext = (uint16_t)newEntry->status_next;

		scrStringGlob.hashTable[0].status_next = newNext;
		scrStringGlob.hashTable[newNext].u.prev = 0;
		newEntry->status_next = (uint16_t)entry->status_next | HASH_STAT_MOVABLE;
		entry->status_next = static_cast<uint16_t>(newIndex)
			| (entry->status_next & HASH_STAT_MASK);
		newEntry->u.prev = entry->u.prev;
		if (validationScope == SL_ValidationScope::Leased)
			SL_SetFreeListCertificateMemberNoReport(newIndex, false);
	}
	else
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) != 0)
		{
			next = (uint16_t)entry->status_next;

			for (prev = next;
				(uint16_t)scrStringGlob.hashTable[prev].status_next != hash;
				prev = (uint16_t)scrStringGlob.hashTable[prev].status_next)
			{
				;
			}

			newIndex = scrStringGlob.hashTable[0].status_next;

			if (!newIndex)
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return SL_InternStatus::RelocatedTableCapacityNoChange;
			}

			newEntry = &scrStringGlob.hashTable[newIndex];
			if ((newEntry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return SL_InternStatus::UnsafeFailure;
			}

			uint16_t allocatedIndex = 0;
			const MT_AllocIndexStatus allocationStatus =
				SL_TryAllocateStringMemoryNoReport(
					static_cast<int>(len + 4),
					type,
					&allocatedIndex,
					validationScope,
					validationLease,
					admission);
			if (allocationStatus != MT_AllocIndexStatus::Success)
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return allocationStatus
						== MT_AllocIndexStatus::InsufficientCapacityNoChange
					? SL_InternStatus::MemoryCapacityNoChange
					: SL_InternStatus::UnsafeFailure;
			}
			stringValue = allocatedIndex;
			if (!SL_CanDebugInitializeStringNoReport(stringValue))
			{
				const MT_FreeIndexStatus cleanupStatus =
					SL_TryFreeStringMemoryNoReport(
						stringValue,
						static_cast<int>(len + 4),
						validationScope,
						validationLease,
						admission);
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return cleanupStatus == MT_FreeIndexStatus::Success
					? SL_InternStatus::UnsafeFailure
					: SL_InternStatus::UnsafeCleanupFailure;
			}

			uint32_t newNext = (uint16_t)newEntry->status_next;

			scrStringGlob.hashTable[0].status_next = newNext;
			scrStringGlob.hashTable[newNext].u.prev = 0;
			scrStringGlob.hashTable[prev].status_next = newIndex
				| (scrStringGlob.hashTable[prev].status_next
					& HASH_STAT_MASK);
			newEntry->status_next = next | HASH_STAT_MOVABLE;
			newEntry->u.prev = entry->u.prev;
			if (validationScope == SL_ValidationScope::Leased)
				SL_SetFreeListCertificateMemberNoReport(
					newIndex, false);
		}
		else
		{
			uint16_t allocatedIndex = 0;
			const MT_AllocIndexStatus allocationStatus =
				SL_TryAllocateStringMemoryNoReport(
					static_cast<int>(len + 4),
				type,
				&allocatedIndex,
				validationScope,
				validationLease,
				admission);
			if (allocationStatus != MT_AllocIndexStatus::Success)
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return allocationStatus
						== MT_AllocIndexStatus::InsufficientCapacityNoChange
					? SL_InternStatus::MemoryCapacityNoChange
					: SL_InternStatus::UnsafeFailure;
			}
			stringValue = allocatedIndex;
			if (!SL_CanDebugInitializeStringNoReport(stringValue))
			{
				const MT_FreeIndexStatus cleanupStatus =
					SL_TryFreeStringMemoryNoReport(
					stringValue,
					static_cast<int>(len + 4),
					validationScope,
					validationLease,
					admission);
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return cleanupStatus == MT_FreeIndexStatus::Success
					? SL_InternStatus::UnsafeFailure
					: SL_InternStatus::UnsafeCleanupFailure;
			}
			prev = entry->u.prev;
			next = (uint16_t)entry->status_next;

			scrStringGlob.hashTable[prev].status_next = next
				| (scrStringGlob.hashTable[prev].status_next
					& HASH_STAT_MASK);
			scrStringGlob.hashTable[next].u.prev = prev;
		}
		entry->status_next = hash | HASH_STAT_HEAD;
		if (validationScope == SL_ValidationScope::Leased)
			SL_SetFreeListCertificateMemberNoReport(hash, false);
	}
	entry->u.prev = stringValue;

	refStr = SL_GetRefStringNoReport(stringValue);
	memcpy((uint8_t*)refStr->str, (uint8_t*)str, len);
	Sys_AtomicStore(
		SL_RefStringWord(refStr),
		scr_string_atomic::Pack(1, userByte, static_cast<uint8_t>(len)));
	SL_DebugAddRefNoReport(stringValue);

//END_CLEANUP:
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	*outStringValue = stringValue;
	return SL_InternStatus::Success;
}

SL_InternStatus SL_TryInternStringOfSize(
	const char* str,
	const uint32_t user,
	const uint32_t len,
	const int type,
	uint32_t* const outStringValue) noexcept
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return SL_InternStatus::UnsafeFailure;
	return SL_TryInternStringOfSizeWithValidation(
		str,
		user,
		len,
		type,
		outStringValue,
		SL_ValidationScope::Complete);
}
} // namespace

uint32_t SL_GetStringOfSize(const char* str, uint32_t user, uint32_t len, int type)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	iassert(str);
	iassert(SL_IsValidUserMask(user, true));
	if (!SL_IsValidUserMask(user, true))
	{
		Com_Error(ERR_DROP, "script string user mask exceeds 8 bits");
		return 0;
	}

	uint32_t stringValue = 0;
	const SL_InternStatus status =
		SL_TryInternStringOfSizeWithValidation(
			str,
			user,
			len,
			type,
			&stringValue,
			SL_ValidationScope::LegacyLocal);
	switch (status)
	{
	case SL_InternStatus::Success:
		return stringValue;
	case SL_InternStatus::PrimaryTableCapacityNoChange:
		Com_Error(ERR_DROP, "exceeded maximum number of script strings (increase STRINGLIST_SIZE)");
		break;
	case SL_InternStatus::RelocatedTableCapacityNoChange:
		Com_Error(ERR_DROP, "exceeded maximum number of script strings");
		break;
	case SL_InternStatus::MemoryCapacityNoChange:
		MT_Error("MT_AllocIndex", static_cast<int>(len + 4));
		break;
	case SL_InternStatus::RefCountExhaustedNoChange:
		Com_Error(ERR_DROP, "invalid script string reference increment");
		break;
	case SL_InternStatus::InvalidArgumentNoChange:
		Com_Error(ERR_DROP, "invalid script string intern arguments");
		break;
	case SL_InternStatus::UnsafeCleanupFailure:
	case SL_InternStatus::UnsafeFailure:
	default:
		Com_Error(ERR_DROP, "unsafe script string intern failure");
		break;
	}
	return 0;
}

const char* SL_ConvertToString(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return nullptr;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return nullptr;
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = stringValue == 0
		|| SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refString, &byteCount);
	(void)byteCount;
	const char* const result = valid && refString
		? refString->str : nullptr;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return nullptr;
	}
	return result;
}

RefString* GetRefString(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return nullptr;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return nullptr;
	}
	RefString* result = nullptr;
	uint32_t byteCount = 0;
	const bool valid = SL_TryResolveLegacyTransferTargetNoReport(
		stringValue, &result, &byteCount);
	(void)byteCount;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return nullptr;
	}
	return result;
}
RefString* GetRefString(const char* str)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return nullptr;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return nullptr;
	}
	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	const uintptr_t stringAddress = reinterpret_cast<uintptr_t>(str);
	const bool addressValid = str != nullptr && memoryBegin != 0
		&& memoryBegin <= (std::numeric_limits<uintptr_t>::max)() - MT_SIZE
		&& stringAddress >= memoryBegin + offsetof(RefString, str)
		&& stringAddress < memoryBegin + MT_SIZE
		&& (stringAddress - memoryBegin - offsetof(RefString, str))
			% MT_NODE_SIZE == 0;
	const uint32_t candidate = addressValid
		? static_cast<uint32_t>(
			(stringAddress - memoryBegin - offsetof(RefString, str))
			/ MT_NODE_SIZE)
		: 0;
	RefString* result = nullptr;
	uint32_t byteCount = 0;
	const bool valid = addressValid
		&& SL_TryResolveLegacyTransferTargetNoReport(
			candidate, &result, &byteCount)
		&& result->str == str;
	(void)byteCount;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return nullptr;
	}
	return result;
}

int SL_GetStringLen(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = SL_TryResolveLegacyTransferTargetNoReport(
		stringValue, &refString, &byteCount);
	(void)refString;
	const int length = valid ? static_cast<int>(byteCount - 1) : 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return 0;
	}
	return length;
}

static uint32_t FindStringOfSize(const char* str, uint32_t len)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	uint32_t stringValue = 0;

	PROF_SCOPED("FindStringOfSize");

	iassert(str);
	uint32_t hash = GetHashCode(str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked()
		|| !scrStringGlob.inited
		|| !SL_IsLegacyLookupHashStateValidNoReport(hash))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}

	HashEntry *entry = &scrStringGlob.hashTable[hash];

	if ((entry->status_next & HASH_STAT_MASK) != HASH_STAT_HEAD)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}

	RefString* refStr = nullptr;
	uint32_t candidateByteCount = 0;
	if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
			entry->u.prev,
			&refStr,
			&candidateByteCount,
			SL_ValidationScope::LegacyLocal))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}

	if (candidateByteCount != len
		|| memcmp(refStr->str, str, len))
	{
		uint32_t prev = hash;
		uint32_t newIndex = (uint16_t)entry->status_next;

		for (HashEntry* newEntry = &scrStringGlob.hashTable[newIndex];
			newEntry != entry;
			newEntry = &scrStringGlob.hashTable[newIndex])
		{
			if ((newEntry->status_next & HASH_STAT_MASK)
					!= HASH_STAT_MOVABLE
				|| !SL_TryGetAllocatedStringByteCountForScopeNoReport(
					newEntry->u.prev,
					&refStr,
					&candidateByteCount,
					SL_ValidationScope::LegacyLocal))
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return 0;
			}

			if (candidateByteCount == len
				&& !memcmp(refStr->str, str, len))
			{
				scrStringGlob.hashTable[prev].status_next =
					static_cast<uint16_t>(newEntry->status_next)
					| (scrStringGlob.hashTable[prev].status_next
						& HASH_STAT_MASK);
				newEntry->status_next =
					static_cast<uint16_t>(entry->status_next)
					| (newEntry->status_next & HASH_STAT_MASK);
				entry->status_next = newIndex
					| (entry->status_next & HASH_STAT_MASK);
				stringValue = newEntry->u.prev;
				newEntry->u.prev = entry->u.prev;
				entry->u.prev = stringValue;

				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return stringValue;
			}
			prev = newIndex;
			newIndex = (uint16_t)newEntry->status_next;
		} // for()
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	} //memcmp

	stringValue = entry->u.prev;

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return stringValue;
}

uint32_t SL_FindString(const char* str)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	return FindStringOfSize(str, strlen(str) + 1);
}

static bool SL_TryResolveLegacyTransferTargetNoReport(
	const uint32_t stringValue,
	RefString** const outRefString,
	uint32_t* const outByteCount) noexcept
{
	if (!outRefString || !outByteCount)
		return false;
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
			stringValue,
			&refString,
			&byteCount,
			SL_ValidationScope::LegacyLocal))
	{
		return false;
	}
	const uint32_t hash = GetHashCode(refString->str, byteCount);
	if (!SL_IsLegacyLookupHashStateValidNoReport(hash)
		|| (scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK)
			!= HASH_STAT_HEAD)
	{
		return false;
	}

	uint32_t entryIndex = hash;
	for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
	{
		const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
		if (entry.u.prev == stringValue)
		{
			*outRefString = refString;
			*outByteCount = byteCount;
			return true;
		}
		const uint32_t nextIndex =
			static_cast<uint16_t>(entry.status_next);
		if (nextIndex == hash)
			return false;
		entryIndex = nextIndex;
	}
	return false;
}

void __cdecl SL_TransferRefToUser(uint32_t stringValue, uint32_t user)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	PROF_SCOPED("SL_TransferRefToUser");

	const uint8_t userByte = static_cast<uint8_t>(user);
	iassert(SL_IsValidUserMask(user, false));
	if (!SL_IsValidUserMask(user, false))
	{
		Com_Error(ERR_DROP, "invalid script string transfer user mask");
		return;
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString* refStr = nullptr;
	uint32_t byteCount = 0;
	if (SL_HasOwnershipBatchRegistryActivityLocked()
		|| !script_string::IsCurrentRuntimeStringId(stringValue)
		|| !SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refStr, &byteCount))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	(void)byteCount;
	const scr_string_atomic::TransferRefToUserResult result =
		scr_string_atomic::TransferRefToUser(
			SL_RefStringWord(refStr), userByte);
	if (result == scr_string_atomic::TransferRefToUserResult::Invalid)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		Com_Error(ERR_DROP, "invalid script string reference transfer");
		return;
	}
	if (result == scr_string_atomic::TransferRefToUserResult::ReleasedDuplicate)
		SL_DebugRemoveRefNoReport(stringValue);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

uint32_t SL_GetStringForVector(const float* v)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char tempString[132];

	snprintf(tempString, sizeof(tempString), "(%g, %g, %g)", *v, v[1], v[2]);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetStringForInt(int i)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char tempString[132]; // [esp+0h] [ebp-88h] BYREF

	snprintf(tempString, sizeof(tempString), "%i", i);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetStringForFloat(float f)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char tempString[132]; // [esp+8h] [ebp-88h] BYREF

	snprintf(tempString, sizeof(tempString), "%g", f);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetString(const char* str, uint32_t user)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	return SL_GetString_(str, user, 6);
}

//char *mt_buffer;  //     scrMemTreePub.mt_buffer = (char*)&scrMemTreeGlob.nodes;


int SL_GetRefStringLen(RefString* refString)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	const uintptr_t refStringAddress =
		reinterpret_cast<uintptr_t>(refString);
	const bool addressValid = memoryBegin != 0
		&& memoryBegin <= (std::numeric_limits<uintptr_t>::max)() - MT_SIZE
		&& refStringAddress > memoryBegin
		&& refStringAddress < memoryBegin + MT_SIZE
		&& (refStringAddress - memoryBegin) % MT_NODE_SIZE == 0;
	RefString* resolvedRefString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = addressValid
		&& SL_TryResolveLegacyTransferTargetNoReport(
			static_cast<uint32_t>(
				(refStringAddress - memoryBegin) / MT_NODE_SIZE),
			&resolvedRefString,
			&byteCount)
		&& resolvedRefString == refString;
	const int length = valid ? static_cast<int>(byteCount - 1) : 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return 0;
	}
	return length;
}

static uint32_t GetLowercaseStringOfSize(
	const char* str,
	uint32_t user,
	uint32_t len,
	int type)
{
	char stra[8192]; // [esp+4Ch] [ebp-2008h] BYREF
	uint32_t i; // [esp+2050h] [ebp-4h]

	PROF_SCOPED("GetLowercaseStringOfSize");
	if (len <= 0x2000)
	{
		for (i = 0; i < len; ++i)
		{
			stra[i] = static_cast<char>(tolower(
				static_cast<unsigned char>(str[i])));
		}
		return SL_GetStringOfSize(stra, user, len, type);
	}
	else
	{
		Com_Error(ERR_DROP, "max string length exceeded: \"%s\"", str);
		return 0;
	}
}

uint32_t SL_GetLowercaseString_(const char* str, uint32_t user, int type)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	return GetLowercaseStringOfSize(str, user, strlen(str) + 1, type);
}
uint32_t SL_GetLowercaseString(const char* str, uint32_t user)
{
	return SL_GetLowercaseString_(str, user, 6);
}

void SL_RemoveRefToString(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	PROF_SCOPED("SL_RemoveRefToString");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	const bool released = SL_TryRemoveRefToStringLockedNoReport(
		stringValue, 0, false);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	(void)released;
	iassert(released);
}

namespace
{
constexpr uint32_t kRefStringHeaderSize =
	static_cast<uint32_t>(offsetof(RefString, str));

struct SL_UnlinkPlan final
{
	uint32_t hash = 0;
	uint32_t targetIndex = 0;
	uint32_t previousIndex = 0;
	uint32_t nextIndex = 0;
	uint32_t freeHead = 0;
};

struct SL_LiveStringInfo final
{
	RefString* refString = nullptr;
	uint32_t packed = 0;
	uint32_t byteCount = 0;
	SL_UnlinkPlan unlink{};
};

enum class SL_ResolveStatus : uint8_t
{
	Success,
	NotAllocatedNoChange,
	UnsafeFailure,
};

bool SL_TryGetBoundedRefStringByteCount(
	RefString* const refString,
	const uint32_t packed,
	const uint32_t allocationCapacity,
	uint32_t* const outByteCount) noexcept
{
	if (!refString || !outByteCount
		|| scr_string_atomic::RefCount(packed) == 0
		|| allocationCapacity <= kRefStringHeaderSize)
		return false;

	const uintptr_t stringBegin =
		reinterpret_cast<uintptr_t>(refString->str);
	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	if (!memoryBegin || memoryBegin > (std::numeric_limits<uintptr_t>::max)() - MT_SIZE)
		return false;
	const uintptr_t memoryEnd = memoryBegin + MT_SIZE;
	if (stringBegin < memoryBegin || stringBegin >= memoryEnd)
		return false;
	const uint32_t stringCapacity =
		allocationCapacity - kRefStringHeaderSize;

	uint32_t terminatorOffset = static_cast<uint8_t>(
		scr_string_atomic::ByteLength(packed) - 1);
	while (terminatorOffset < UINT32_C(65531))
	{
		if (terminatorOffset >= stringCapacity
			|| terminatorOffset >= memoryEnd - stringBegin)
			return false;
		if (refString->str[terminatorOffset] == '\0')
		{
			*outByteCount = terminatorOffset + 1;
			return true;
		}
		if (terminatorOffset > UINT32_C(65530) - UINT32_C(256))
			return false;
		terminatorOffset += UINT32_C(256);
	}
	return false;
}

bool SL_IsExactStringAllocationNoReport(
	const MT_AllocationInfo &allocationInfo,
	const uint32_t byteCount) noexcept
{
	if (allocationInfo.reserved != 0 || byteCount == 0
		|| byteCount > UINT32_C(65531))
	{
		return false;
	}

	const uint32_t requestedBytes = byteCount + kRefStringHeaderSize;
	const uint32_t requestedBuckets =
		(requestedBytes + sizeof(MemoryNode) - 1u)
		/ static_cast<uint32_t>(sizeof(MemoryNode));
	uint32_t capacityBuckets = 1;
	uint8_t expectedSize = 0;
	while (capacityBuckets < requestedBuckets)
	{
		capacityBuckets <<= 1;
		++expectedSize;
	}
	return allocationInfo.size == expectedSize
		&& allocationInfo.capacityBytes
			== capacityBuckets * sizeof(MemoryNode);
}

bool SL_TryRecoverRefStringByteCountNoReport(
	RefString* const refString,
	const uint32_t packed,
	const MT_AllocationInfo &allocationInfo,
	uint32_t* const outByteCount) noexcept
{
	if (!refString || !outByteCount || allocationInfo.reserved != 0)
		return false;

	const uint32_t packedByteCount =
		scr_string_atomic::ByteLength(packed) == 0
		? UINT32_C(256)
		: scr_string_atomic::ByteLength(packed);
	// Legacy explicit-size callers intern compact binary records whose final
	// byte is not a NUL (notably XAnimToXModel). For the first 256 bytes the
	// packed length plus the exact allocator size class recovers their complete
	// length without scanning. Longer report-free strings retain the bounded
	// congruent-terminator recovery below.
	uint32_t byteCount = 0;
	if (SL_IsExactStringAllocationNoReport(
			allocationInfo, packedByteCount))
	{
		byteCount = packedByteCount;
	}
	else if (!SL_TryGetBoundedRefStringByteCount(
			refString,
			packed,
			allocationInfo.capacityBytes,
			&byteCount)
		|| !SL_IsExactStringAllocationNoReport(
			allocationInfo, byteCount))
	{
		return false;
	}

	*outByteCount = byteCount;
	return true;
}

uint8_t sl_hashChainVisited[(STRINGLIST_SIZE + 7) / 8];
uint8_t sl_stringIdVisited[SL_MAX_STRING_INDEX / 8];
uint16_t sl_hashChainVisitedEntries[STRINGLIST_SIZE];
uint16_t sl_stringIdVisitedEntries[STRINGLIST_SIZE];
uint32_t sl_hashChainVisitedCount = 0;
uint32_t sl_stringIdVisitedCount = 0;
uint8_t sl_systemSweepHashEntries[(STRINGLIST_SIZE + 7) / 8];
uint8_t sl_systemSweepStringIds[SL_MAX_STRING_INDEX / 8];
uint8_t sl_freeListVisited[(STRINGLIST_SIZE + 7) / 8];
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
uint32_t sl_completeFreeListValidationCount = 0;
uint32_t sl_hashValidationScratchResetCount = 0;
uint64_t sl_hashValidationScratchResetEntryCount = 0;
#endif

void SL_ResetHashChainValidationNoReport() noexcept
{
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
	++sl_hashValidationScratchResetCount;
	sl_hashValidationScratchResetEntryCount +=
		sl_hashChainVisitedCount + sl_stringIdVisitedCount;
#endif
	for (uint32_t entry = 0; entry < sl_hashChainVisitedCount; ++entry)
	{
		const uint32_t entryIndex = sl_hashChainVisitedEntries[entry];
		sl_hashChainVisited[entryIndex >> 3] &=
			static_cast<uint8_t>(~(1u << (entryIndex & 7u)));
	}
	for (uint32_t entry = 0; entry < sl_stringIdVisitedCount; ++entry)
	{
		const uint32_t stringId = sl_stringIdVisitedEntries[entry];
		sl_stringIdVisited[stringId >> 3] &=
			static_cast<uint8_t>(~(1u << (stringId & 7u)));
	}
	sl_hashChainVisitedCount = 0;
	sl_stringIdVisitedCount = 0;
}

bool SL_TryRecordHashEntryNoReport(const uint32_t entryIndex) noexcept
{
	if (entryIndex >= STRINGLIST_SIZE
		|| sl_hashChainVisitedCount >= STRINGLIST_SIZE)
	{
		return false;
	}
	const uint32_t byteIndex = entryIndex >> 3;
	const uint8_t bitMask =
		static_cast<uint8_t>(1u << (entryIndex & 7u));
	if ((sl_hashChainVisited[byteIndex] & bitMask) != 0)
		return false;
	sl_hashChainVisited[byteIndex] |= bitMask;
	sl_hashChainVisitedEntries[sl_hashChainVisitedCount++] =
		static_cast<uint16_t>(entryIndex);
	return true;
}

bool SL_TryRecordStringIdNoReport(const uint32_t stringId) noexcept
{
	if (!script_string::IsCurrentRuntimeStringId(stringId)
		|| sl_stringIdVisitedCount >= STRINGLIST_SIZE)
		return false;
	const uint32_t byteIndex = stringId >> 3;
	const uint8_t bitMask =
		static_cast<uint8_t>(1u << (stringId & 7u));
	if ((sl_stringIdVisited[byteIndex] & bitMask) != 0)
		return false;
	sl_stringIdVisited[byteIndex] |= bitMask;
	sl_stringIdVisitedEntries[sl_stringIdVisitedCount++] =
		static_cast<uint16_t>(stringId);
	return true;
}

MT_AllocationInfoStatus SL_TryGetAllocationInfoForScopeNoReport(
	const uint32_t stringValue,
	MT_AllocationInfo* const outInfo,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	if (!SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission))
	{
		return MT_AllocationInfoStatus::UnsafeFailure;
	}
	switch (scope)
	{
	case SL_ValidationScope::Complete:
		return validationLease
			? MT_AllocationInfoStatus::UnsafeFailure
			: MT_TryGetAllocationInfo(stringValue, outInfo);
	case SL_ValidationScope::LegacyLocal:
		return validationLease
			? MT_AllocationInfoStatus::UnsafeFailure
			: MT_TryGetAllocationInfoLegacy(stringValue, outInfo);
	case SL_ValidationScope::Leased:
		return MT_TryGetAllocationInfoLeased(
			*validationLease, stringValue, outInfo, *admission);
	}
	return MT_AllocationInfoStatus::UnsafeFailure;
}

bool SL_TryGetAllocatedStringByteCountForScopeNoReport(
	const uint32_t stringValue,
	RefString** const outRefString,
	uint32_t* const outByteCount,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	if (!outRefString || !outByteCount
		|| !script_string::IsCurrentRuntimeStringId(stringValue))
		return false;
	MT_AllocationInfo allocationInfo{};
	const MT_AllocationInfoStatus allocationStatus =
		SL_TryGetAllocationInfoForScopeNoReport(
			stringValue, &allocationInfo, scope, validationLease, admission);
	if (allocationStatus
		!= MT_AllocationInfoStatus::Success
		|| allocationInfo.reserved != 0)
	{
		return false;
	}

	RefString* const refString = SL_GetRefStringNoReport(stringValue);
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(refString));
	if (scr_string_atomic::RefCount(packed)
		< SL_UserReferenceCount(scr_string_atomic::User(packed)))
	{
		return false;
	}
	uint32_t byteCount = 0;
	if (!SL_TryRecoverRefStringByteCountNoReport(
			refString, packed, allocationInfo, &byteCount))
	{
		return false;
	}
	if (!SL_IsDebugOwnershipExactNoReport(stringValue, packed))
	{
		return false;
	}

	*outRefString = refString;
	*outByteCount = byteCount;
	return true;
}

bool SL_TryGetAllocatedStringHashForScopeNoReport(
	const uint32_t stringValue,
	uint32_t* const outHash,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	if (!outHash)
		return false;
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
			stringValue,
			&refString,
			&byteCount,
			scope,
			validationLease,
			admission))
	{
		return false;
	}
	*outHash = GetHashCode(refString->str, byteCount);
	return true;
}

bool SL_IsAllocatedStringEntryValidForScopeNoReport(
	const uint32_t stringValue,
	const uint32_t expectedHash,
	const bool requireExpectedHash,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	uint32_t actualHash = 0;
	return SL_TryGetAllocatedStringHashForScopeNoReport(
			stringValue, &actualHash, scope, validationLease, admission)
		&& (!requireExpectedHash || actualHash == expectedHash);
}

bool SL_IsFreeEntryLocallyLinkedNoReport(const uint32_t index) noexcept
{
	if (index == 0 || index >= STRINGLIST_SIZE)
		return false;

	const HashEntry &entry = scrStringGlob.hashTable[index];
	if (!SL_IsHashEntryEncodingValidNoReport(entry)
		|| (entry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
		return false;
	const uint32_t previous = entry.u.prev;
	const uint32_t next = static_cast<uint16_t>(entry.status_next);
	if (previous >= STRINGLIST_SIZE || next >= STRINGLIST_SIZE
		|| previous == index || next == index)
	{
		return false;
	}

	const HashEntry &previousEntry = scrStringGlob.hashTable[previous];
	const HashEntry &nextEntry = scrStringGlob.hashTable[next];
	return SL_IsHashEntryEncodingValidNoReport(previousEntry)
		&& SL_IsHashEntryEncodingValidNoReport(nextEntry)
		&& (previousEntry.status_next & HASH_STAT_MASK) == HASH_STAT_FREE
		&& (nextEntry.status_next & HASH_STAT_MASK) == HASH_STAT_FREE
		&& static_cast<uint16_t>(previousEntry.status_next) == index
		&& nextEntry.u.prev == index;
}

bool SL_IsFreeListLocallyValidNoReport() noexcept
{
	const HashEntry &sentinel = scrStringGlob.hashTable[0];
	if (!SL_IsHashEntryEncodingValidNoReport(sentinel)
		|| (sentinel.status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
		return false;
	const uint32_t head = static_cast<uint16_t>(sentinel.status_next);
	const uint32_t tail = sentinel.u.prev;
	if (head == 0 || tail == 0)
		return head == 0 && tail == 0;
	if (head >= STRINGLIST_SIZE || tail >= STRINGLIST_SIZE)
		return false;

	const HashEntry &headEntry = scrStringGlob.hashTable[head];
	const HashEntry &tailEntry = scrStringGlob.hashTable[tail];
	if (!SL_IsHashEntryEncodingValidNoReport(headEntry)
		|| !SL_IsHashEntryEncodingValidNoReport(tailEntry)
		|| (headEntry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE
		|| (tailEntry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE
		|| headEntry.u.prev != 0
		|| static_cast<uint16_t>(tailEntry.status_next) != 0)
	{
		return false;
	}

	const uint32_t headNext = static_cast<uint16_t>(headEntry.status_next);
	const uint32_t tailPrevious = tailEntry.u.prev;
	if (headNext >= STRINGLIST_SIZE || tailPrevious >= STRINGLIST_SIZE)
		return false;
	const HashEntry &headNextEntry = scrStringGlob.hashTable[headNext];
	const HashEntry &tailPreviousEntry =
		scrStringGlob.hashTable[tailPrevious];
	return SL_IsHashEntryEncodingValidNoReport(headNextEntry)
		&& SL_IsHashEntryEncodingValidNoReport(tailPreviousEntry)
		&& (headNextEntry.status_next & HASH_STAT_MASK) == HASH_STAT_FREE
		&& (tailPreviousEntry.status_next & HASH_STAT_MASK) == HASH_STAT_FREE
		&& headNextEntry.u.prev == head
		&& static_cast<uint16_t>(tailPreviousEntry.status_next) == tail;
}

bool SL_IsFreeListHeadValidNoReport() noexcept
{
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
	++sl_completeFreeListValidationCount;
#endif
	if (!SL_IsHashEntryEncodingValidNoReport(scrStringGlob.hashTable[0])
		|| (scrStringGlob.hashTable[0].status_next & HASH_STAT_MASK)
		!= HASH_STAT_FREE)
		return false;
	const uint32_t freeHead =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	memset(sl_freeListVisited, 0, sizeof(sl_freeListVisited));
	if (freeHead == 0)
		return scrStringGlob.hashTable[0].u.prev == 0;
	if (freeHead >= STRINGLIST_SIZE)
		return false;

	uint32_t previousIndex = 0;
	uint32_t currentIndex = freeHead;
	for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
	{
		if (currentIndex == 0)
			return scrStringGlob.hashTable[0].u.prev == previousIndex;
		if (currentIndex >= STRINGLIST_SIZE)
			return false;

		const uint32_t visitedByte = currentIndex >> 3;
		const uint8_t visitedBit =
			static_cast<uint8_t>(1u << (currentIndex & 7u));
		if ((sl_freeListVisited[visitedByte] & visitedBit) != 0)
			return false;
		sl_freeListVisited[visitedByte] |= visitedBit;

		const HashEntry &entry = scrStringGlob.hashTable[currentIndex];
		if (!SL_IsHashEntryEncodingValidNoReport(entry)
			|| (entry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE
			|| entry.u.prev != previousIndex)
		{
			return false;
		}
		previousIndex = currentIndex;
		currentIndex = static_cast<uint16_t>(entry.status_next);
	}
	return false;
}

bool SL_IsFreeListCertificateMemberNoReport(
	const uint32_t entryIndex) noexcept
{
	if (entryIndex == 0 || entryIndex >= STRINGLIST_SIZE)
		return false;
	const uint32_t byteIndex = entryIndex >> 3;
	const uint8_t bitMask =
		static_cast<uint8_t>(1u << (entryIndex & 7u));
	return (sl_freeListVisited[byteIndex] & bitMask) != 0;
}

void SL_SetFreeListCertificateMemberNoReport(
	const uint32_t entryIndex,
	const bool member) noexcept
{
	if (entryIndex == 0 || entryIndex >= STRINGLIST_SIZE)
		return;
	const uint32_t byteIndex = entryIndex >> 3;
	const uint8_t bitMask =
		static_cast<uint8_t>(1u << (entryIndex & 7u));
	if (member)
		sl_freeListVisited[byteIndex] |= bitMask;
	else
		sl_freeListVisited[byteIndex] &= static_cast<uint8_t>(~bitMask);
}

bool SL_IsLeasedFreeListLocallyValidNoReport() noexcept
{
	if (!SL_IsFreeListLocallyValidNoReport())
		return false;
	const uint32_t head =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	const uint32_t tail = scrStringGlob.hashTable[0].u.prev;
	return (head == 0 && tail == 0)
		|| (SL_IsFreeListCertificateMemberNoReport(head)
			&& SL_IsFreeListCertificateMemberNoReport(tail));
}

bool SL_IsFreeListValidForScopeNoReport(
	const SL_ValidationScope scope) noexcept
{
	switch (scope)
	{
	case SL_ValidationScope::Complete:
		return SL_IsFreeListHeadValidNoReport();
	case SL_ValidationScope::LegacyLocal:
		return SL_IsFreeListLocallyValidNoReport();
	case SL_ValidationScope::Leased:
		return SL_IsLeasedFreeListLocallyValidNoReport();
	}
	return false;
}

bool SL_IsFreeEntryReachableNoReport(
	const uint32_t targetIndex) noexcept
{
	if (targetIndex == 0 || targetIndex >= STRINGLIST_SIZE)
		return false;

	memset(sl_freeListVisited, 0, sizeof(sl_freeListVisited));
	uint32_t currentIndex = targetIndex;
	for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
	{
		const uint32_t visitedByte = currentIndex >> 3;
		const uint8_t visitedBit =
			static_cast<uint8_t>(1u << (currentIndex & 7u));
		if ((sl_freeListVisited[visitedByte] & visitedBit) != 0)
			return false;
		sl_freeListVisited[visitedByte] |= visitedBit;

		const HashEntry &entry = scrStringGlob.hashTable[currentIndex];
		if (!SL_IsHashEntryEncodingValidNoReport(entry)
			|| (entry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
			return false;
		const uint32_t nextIndex =
			static_cast<uint16_t>(entry.status_next);
		if (nextIndex >= STRINGLIST_SIZE || nextIndex == currentIndex
			|| !SL_IsHashEntryEncodingValidNoReport(
				scrStringGlob.hashTable[nextIndex])
			|| (scrStringGlob.hashTable[nextIndex].status_next
				& HASH_STAT_MASK) != HASH_STAT_FREE
			|| scrStringGlob.hashTable[nextIndex].u.prev != currentIndex)
		{
			return false;
		}

		const uint32_t previousIndex = entry.u.prev;
		if (previousIndex == 0)
		{
			const HashEntry &sentinel = scrStringGlob.hashTable[0];
			return SL_IsHashEntryEncodingValidNoReport(sentinel)
				&& (sentinel.status_next & HASH_STAT_MASK)
					== HASH_STAT_FREE
				&& static_cast<uint16_t>(sentinel.status_next)
					== currentIndex;
		}
		if (previousIndex >= STRINGLIST_SIZE
			|| !SL_IsHashEntryEncodingValidNoReport(
				scrStringGlob.hashTable[previousIndex])
			|| (scrStringGlob.hashTable[previousIndex].status_next
				& HASH_STAT_MASK) != HASH_STAT_FREE
			|| static_cast<uint16_t>(
				scrStringGlob.hashTable[previousIndex].status_next)
				!= currentIndex)
		{
			return false;
		}
		currentIndex = previousIndex;
	}
	return false;
}

bool SL_IsInternHashStateValidForScopeNoReport(
	const uint32_t hash,
	const SL_ValidationScope scope,
	const bool validateFreeList,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	if (hash == 0 || hash >= STRINGLIST_SIZE)
		return false;
	if (!SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission)
		|| (validateFreeList && !SL_IsFreeListValidForScopeNoReport(scope)))
	{
		return false;
	}

	const HashEntry &home = scrStringGlob.hashTable[hash];
	if (!SL_IsHashEntryEncodingValidNoReport(home))
		return false;
	const uint32_t homeStatus = home.status_next & HASH_STAT_MASK;
	if (homeStatus == HASH_STAT_HEAD)
	{
		SL_ResetHashChainValidationNoReport();
		uint32_t entryIndex = hash;
		for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
		{
			if (!SL_TryRecordHashEntryNoReport(entryIndex))
				return false;

			const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
			const uint32_t expectedStatus =
				entryIndex == hash ? HASH_STAT_HEAD : HASH_STAT_MOVABLE;
			if (!SL_IsHashEntryEncodingValidNoReport(entry)
				|| (entry.status_next & HASH_STAT_MASK) != expectedStatus
				|| !SL_IsAllocatedStringEntryValidForScopeNoReport(
					entry.u.prev,
					hash,
					true,
					scope,
					validationLease,
					admission)
				|| !SL_TryRecordStringIdNoReport(entry.u.prev))
				return false;

			const uint32_t nextIndex =
				static_cast<uint16_t>(entry.status_next);
			if (nextIndex == hash)
				return true;
			if (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| !SL_IsHashEntryEncodingValidNoReport(
					scrStringGlob.hashTable[nextIndex])
				|| (scrStringGlob.hashTable[nextIndex].status_next
					& HASH_STAT_MASK) != HASH_STAT_MOVABLE)
				return false;
			entryIndex = nextIndex;
		}
		return false;
	}

	if (homeStatus == HASH_STAT_MOVABLE)
	{
		uint32_t owningHash = 0;
		if (!SL_TryGetAllocatedStringHashForScopeNoReport(
				home.u.prev,
				&owningHash,
				scope,
				validationLease,
				admission)
			|| owningHash == hash || owningHash == 0
			|| owningHash >= STRINGLIST_SIZE
			|| !SL_IsHashEntryEncodingValidNoReport(
				scrStringGlob.hashTable[owningHash])
			|| (scrStringGlob.hashTable[owningHash].status_next
				& HASH_STAT_MASK) != HASH_STAT_HEAD)
		{
			return false;
		}

		SL_ResetHashChainValidationNoReport();
		uint32_t entryIndex = owningHash;
		bool foundHome = false;
		for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
		{
			if (entryIndex == 0 || entryIndex >= STRINGLIST_SIZE)
				return false;
			if (!SL_TryRecordHashEntryNoReport(entryIndex))
				return false;

			const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
			const uint32_t expectedStatus =
				entryIndex == owningHash
				? HASH_STAT_HEAD
				: HASH_STAT_MOVABLE;
			if (!SL_IsHashEntryEncodingValidNoReport(entry)
				|| (entry.status_next & HASH_STAT_MASK) != expectedStatus
				|| !SL_IsAllocatedStringEntryValidForScopeNoReport(
					entry.u.prev,
					owningHash,
					true,
					scope,
					validationLease,
					admission)
				|| !SL_TryRecordStringIdNoReport(entry.u.prev))
			{
				return false;
			}
			if (entryIndex == hash)
				foundHome = true;

			const uint32_t nextIndex =
				static_cast<uint16_t>(entry.status_next);
			if (nextIndex == owningHash)
				return foundHome;
			if (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| !SL_IsHashEntryEncodingValidNoReport(
					scrStringGlob.hashTable[nextIndex]))
				return false;
			entryIndex = nextIndex;
		}
		return false;
	}

	if (homeStatus != HASH_STAT_FREE)
		return false;
	if (!validateFreeList)
		return true;
	if (scope == SL_ValidationScope::Complete)
	{
		if (!SL_IsFreeEntryReachableNoReport(hash))
			return false;
	}
	else if (!SL_IsFreeEntryLocallyLinkedNoReport(hash)
		|| (scope == SL_ValidationScope::Leased
			&& !SL_IsFreeListCertificateMemberNoReport(hash)))
	{
		return false;
	}
	const uint32_t previousFree = home.u.prev;
	const uint32_t nextFree = static_cast<uint16_t>(home.status_next);
	return previousFree < STRINGLIST_SIZE && nextFree < STRINGLIST_SIZE
		&& SL_IsHashEntryEncodingValidNoReport(
			scrStringGlob.hashTable[previousFree])
		&& SL_IsHashEntryEncodingValidNoReport(
			scrStringGlob.hashTable[nextFree])
		&& (scrStringGlob.hashTable[previousFree].status_next
			& HASH_STAT_MASK) == HASH_STAT_FREE
		&& (scrStringGlob.hashTable[nextFree].status_next
			& HASH_STAT_MASK) == HASH_STAT_FREE
		&& static_cast<uint16_t>(
			scrStringGlob.hashTable[previousFree].status_next) == hash
		&& scrStringGlob.hashTable[nextFree].u.prev == hash;
}

bool SL_IsInternHashStateValidNoReport(const uint32_t hash) noexcept
{
	return SL_IsInternHashStateValidForScopeNoReport(
		hash, SL_ValidationScope::Complete, true, nullptr);
}

bool SL_IsLegacyInternHashStateValidNoReport(const uint32_t hash) noexcept
{
	return SL_IsInternHashStateValidForScopeNoReport(
		hash, SL_ValidationScope::LegacyLocal, true, nullptr);
}

bool SL_IsLegacyLookupHashStateValidNoReport(const uint32_t hash) noexcept
{
	return SL_IsInternHashStateValidForScopeNoReport(
		hash, SL_ValidationScope::LegacyLocal, false, nullptr);
}

bool SL_IsCompleteStringStateValidForScopeNoReport(
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	if (scrStringDebugGlob != &scrStringDebugGlobBuf
		|| !scrStringGlob.inited
		|| !SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission)
		|| !SL_IsFreeListHeadValidNoReport())
	{
		return false;
	}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
	++sl_ownershipValidationCounters.completeStringPasses;
#endif

	if (!SL_IsHashEntryEncodingValidNoReport(scrStringGlob.hashTable[0]))
		return false;
	memset(sl_systemSweepHashEntries, 0,
		sizeof(sl_systemSweepHashEntries));
	memset(sl_systemSweepStringIds, 0, sizeof(sl_systemSweepStringIds));
	uint64_t aggregateRefCount = 0;
	for (uint32_t owningHash = 1;
		owningHash < STRINGLIST_SIZE;
		++owningHash)
	{
		if ((scrStringGlob.hashTable[owningHash].status_next & HASH_STAT_MASK)
			!= HASH_STAT_HEAD)
		{
			continue;
		}

		uint32_t entryIndex = owningHash;
		bool terminated = false;
		for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
		{
			if (entryIndex == 0 || entryIndex >= STRINGLIST_SIZE)
				return false;
			const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
			const uint32_t expectedStatus = entryIndex == owningHash
				? HASH_STAT_HEAD
				: HASH_STAT_MOVABLE;
			const uint32_t entryByte = entryIndex >> 3;
			const uint8_t entryMask =
				static_cast<uint8_t>(1u << (entryIndex & 7u));
			if (!SL_IsHashEntryEncodingValidNoReport(entry)
				|| (entry.status_next & HASH_STAT_MASK) != expectedStatus
				|| (sl_freeListVisited[entryByte] & entryMask) != 0
				|| (sl_systemSweepHashEntries[entryByte] & entryMask) != 0)
			{
				return false;
			}
			sl_systemSweepHashEntries[entryByte] |= entryMask;
#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
			++sl_ownershipValidationCounters.stringEntriesVisited;
#endif

			const uint32_t stringValue = entry.u.prev;
			if (!script_string::IsCurrentRuntimeStringId(stringValue))
				return false;
			const uint32_t stringByte = stringValue >> 3;
			const uint8_t stringMask =
				static_cast<uint8_t>(1u << (stringValue & 7u));
			if ((sl_systemSweepStringIds[stringByte] & stringMask) != 0)
				return false;
			sl_systemSweepStringIds[stringByte] |= stringMask;

			RefString* refString = nullptr;
			uint32_t byteCount = 0;
			if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
					stringValue,
					&refString,
					&byteCount,
					scope,
					validationLease,
					admission)
				|| GetHashCode(refString->str, byteCount) != owningHash)
			{
				return false;
			}
			const uint32_t packed =
				scr_string_atomic::Load(SL_RefStringWord(refString));
			if (!SL_IsDebugOwnershipExactNoReport(stringValue, packed))
				return false;
			aggregateRefCount += scr_string_atomic::RefCount(packed);
			if (aggregateRefCount > UINT32_MAX)
				return false;

			const uint32_t nextIndex =
				static_cast<uint16_t>(entry.status_next);
			if (nextIndex == owningHash)
			{
				terminated = true;
				break;
			}
			if (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| (scrStringGlob.hashTable[nextIndex].status_next
					& HASH_STAT_MASK) != HASH_STAT_MOVABLE)
			{
				return false;
			}
			entryIndex = nextIndex;
		}
		if (!terminated)
			return false;
	}

	for (uint32_t hash = 1; hash < STRINGLIST_SIZE; ++hash)
	{
		const HashEntry &entry = scrStringGlob.hashTable[hash];
		if (!SL_IsHashEntryEncodingValidNoReport(entry))
			return false;
		const uint32_t status = entry.status_next & HASH_STAT_MASK;
		const uint8_t mask = static_cast<uint8_t>(1u << (hash & 7u));
		const bool reachableFree =
			(sl_freeListVisited[hash >> 3] & mask) != 0;
		const bool reachableOccupied =
			(sl_systemSweepHashEntries[hash >> 3] & mask) != 0;
		if (status == HASH_STAT_FREE)
		{
			if (!reachableFree || reachableOccupied)
				return false;
		}
		else if (status == HASH_STAT_HEAD || status == HASH_STAT_MOVABLE)
		{
			if (reachableFree || !reachableOccupied)
				return false;
		}
		else
		{
			return false;
		}
	}

	if (!scrStringDebugGlob)
		return true;
	if (Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount)
		!= static_cast<uint32_t>(aggregateRefCount))
	{
		return false;
	}
	if (scope != SL_ValidationScope::Leased)
	{
		for (uint32_t stringValue = 0;
			stringValue < SL_MAX_STRING_INDEX;
			++stringValue)
		{
			const uint8_t visitedMask =
				static_cast<uint8_t>(1u << (stringValue & 7u));
			if ((sl_systemSweepStringIds[stringValue >> 3]
					& visitedMask) == 0
				&& SL_DebugRefCount(stringValue) != 0)
			{
				return false;
			}
		}
	}
	return true;
}

bool SL_IsCompleteSystemSweepStateValidNoReport() noexcept
{
	return MT_TryValidateState()
		&& SL_IsCompleteStringStateValidForScopeNoReport(
			SL_ValidationScope::LegacyLocal, nullptr);
}

bool SL_TryBuildUnlinkPlanForScopeNoReport(
	const uint32_t stringValue,
	RefString* const refString,
	const uint32_t byteCount,
	SL_UnlinkPlan* const outPlan,
	const SL_ValidationScope scope,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	if (!refString || !outPlan || byteCount == 0
		|| byteCount > UINT32_C(65531)
		|| !SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission))
		return false;
	MT_AllocationInfo targetAllocation{};
	const MT_AllocationInfoStatus targetStatus =
		SL_TryGetAllocationInfoForScopeNoReport(
			stringValue,
			&targetAllocation,
			scope,
			validationLease,
			admission);
	if (targetStatus != MT_AllocationInfoStatus::Success
		|| refString != SL_GetRefStringNoReport(stringValue)
		|| !SL_IsExactStringAllocationNoReport(
			targetAllocation, byteCount))
	{
		return false;
	}

	const uint32_t hash = GetHashCode(refString->str, byteCount);
	if (hash == 0 || hash >= STRINGLIST_SIZE)
		return false;

	SL_ResetHashChainValidationNoReport();
	SL_UnlinkPlan plan{};
	plan.hash = hash;
	uint32_t entryIndex = hash;
	uint32_t previousIndex = hash;
	bool foundTarget = false;
	bool terminated = false;
	for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
	{
		if (!SL_TryRecordHashEntryNoReport(entryIndex))
			return false;

		const HashEntry* const entry = &scrStringGlob.hashTable[entryIndex];
		const uint32_t expectedStatus =
			entryIndex == hash ? HASH_STAT_HEAD : HASH_STAT_MOVABLE;
		const bool isTarget = entry->u.prev == stringValue;
		if (!SL_IsHashEntryEncodingValidNoReport(*entry)
			|| (entry->status_next & HASH_STAT_MASK) != expectedStatus
			|| (!isTarget
				&& !SL_IsAllocatedStringEntryValidForScopeNoReport(
					entry->u.prev,
					hash,
					true,
					scope,
					validationLease,
					admission))
			|| !SL_TryRecordStringIdNoReport(entry->u.prev))
			return false;

		const uint32_t nextIndex =
			static_cast<uint16_t>(entry->status_next);
		if (nextIndex != hash
			&& (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| !SL_IsHashEntryEncodingValidNoReport(
					scrStringGlob.hashTable[nextIndex])
				|| (scrStringGlob.hashTable[nextIndex].status_next
					& HASH_STAT_MASK) != HASH_STAT_MOVABLE))
		{
			return false;
		}

		if (isTarget)
		{
			if (foundTarget)
				return false;
			foundTarget = true;
			plan.targetIndex = entryIndex;
			plan.previousIndex = previousIndex;
			plan.nextIndex = nextIndex;
		}

		if (nextIndex == hash)
		{
			terminated = true;
			break;
		}
		previousIndex = entryIndex;
		entryIndex = nextIndex;
	}
	if (!foundTarget || !terminated)
		return false;

	if (!SL_IsFreeListValidForScopeNoReport(scope))
		return false;
	plan.freeHead =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	if (scope == SL_ValidationScope::Leased
		&& plan.freeHead != 0
		&& !SL_IsFreeListCertificateMemberNoReport(plan.freeHead))
	{
		return false;
	}

	*outPlan = plan;
	return true;
}

bool SL_TryBuildUnlinkPlanNoReport(
	const uint32_t stringValue,
	RefString* const refString,
	const uint32_t byteCount,
	SL_UnlinkPlan* const outPlan) noexcept
{
	return SL_TryBuildUnlinkPlanForScopeNoReport(
		stringValue,
		refString,
		byteCount,
		outPlan,
		SL_ValidationScope::Complete,
		nullptr);
}

SL_ResolveStatus SL_TryResolveLiveStringNoReport(
	const uint32_t stringValue,
	SL_LiveStringInfo* const outInfo,
	const SL_ValidationScope scope = SL_ValidationScope::Complete,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	if (!outInfo || !script_string::IsCurrentRuntimeStringId(stringValue)
		|| !SL_IsValidationAuthorityWellFormed(
			scope, validationLease, admission))
		return SL_ResolveStatus::NotAllocatedNoChange;

	MT_AllocationInfo allocationInfo{};
	const MT_AllocationInfoStatus allocationStatus =
		SL_TryGetAllocationInfoForScopeNoReport(
			stringValue,
			&allocationInfo,
			scope,
			validationLease,
			admission);
	if (allocationStatus == MT_AllocationInfoStatus::NotAllocatedNoChange)
		return SL_ResolveStatus::NotAllocatedNoChange;
	if (allocationStatus != MT_AllocationInfoStatus::Success
		|| allocationInfo.reserved != 0)
	{
		return SL_ResolveStatus::UnsafeFailure;
	}

	SL_LiveStringInfo info{};
	info.refString = SL_GetRefStringNoReport(stringValue);
	info.packed = scr_string_atomic::Load(SL_RefStringWord(info.refString));
	if (!SL_TryRecoverRefStringByteCountNoReport(
			info.refString,
			info.packed,
			allocationInfo,
			&info.byteCount)
		|| info.byteCount > allocationInfo.capacityBytes
			- kRefStringHeaderSize
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, info.packed)
		|| !SL_TryBuildUnlinkPlanForScopeNoReport(
			stringValue,
			info.refString,
			info.byteCount,
			&info.unlink,
			scope,
			validationLease,
			admission))
	{
		return SL_ResolveStatus::UnsafeFailure;
	}

	*outInfo = info;
	return SL_ResolveStatus::Success;
}

bool SL_CanDebugRemoveRefNoReport(const uint32_t stringValue) noexcept
{
	return !scrStringDebugGlob
		|| (SL_DebugRefCount(stringValue) != 0
			&& Sys_AtomicLoad(&scrStringDebugGlob->totalRefCount) != 0);
}

void SL_DebugRemoveRefNoReport(const uint32_t stringValue) noexcept
{
	if (!scrStringDebugGlob)
		return;
	Sys_AtomicDecrement(&scrStringDebugGlob->totalRefCount);
	Sys_AtomicDecrement(&scrStringDebugGlob->refCount[stringValue]);

}

void SL_CommitUnlinkPlanNoReport(
	const SL_UnlinkPlan &plan,
	const bool updateCertificate = false) noexcept
{
	uint32_t freedIndex = plan.targetIndex;
	if (plan.targetIndex == plan.hash)
	{
		if (plan.nextIndex != plan.hash)
		{
			scrStringGlob.hashTable[plan.hash].status_next =
				static_cast<uint16_t>(
					scrStringGlob.hashTable[plan.nextIndex].status_next)
				| HASH_STAT_HEAD;
			scrStringGlob.hashTable[plan.hash].u.prev =
				scrStringGlob.hashTable[plan.nextIndex].u.prev;
			scrStringGlob.nextFreeEntry =
				&scrStringGlob.hashTable[plan.hash];
			freedIndex = plan.nextIndex;
		}
	}
	else
	{
		scrStringGlob.hashTable[plan.previousIndex].status_next =
			static_cast<uint16_t>(
				scrStringGlob.hashTable[plan.targetIndex].status_next)
			| (scrStringGlob.hashTable[plan.previousIndex].status_next
				& HASH_STAT_MASK);
	}

	HashEntry &freedEntry = scrStringGlob.hashTable[freedIndex];
	freedEntry.status_next = plan.freeHead;
	freedEntry.u.prev = 0;
	scrStringGlob.hashTable[plan.freeHead].u.prev = freedIndex;
	scrStringGlob.hashTable[0].status_next = freedIndex;
	if (updateCertificate)
		SL_SetFreeListCertificateMemberNoReport(freedIndex, true);
}

bool SL_TryFreeSystemSweepEntryNoReport(
	const uint32_t owningHash,
	const uint32_t targetIndex,
	const uint32_t previousIndex,
	const uint32_t stringValue,
	RefString* const refString,
	const uint32_t byteCount) noexcept
{
	if (SL_HasOwnershipBatchRegistryActivityLocked())
		return false;
	if (owningHash == 0 || owningHash >= STRINGLIST_SIZE
		|| targetIndex == 0 || targetIndex >= STRINGLIST_SIZE
		|| previousIndex == 0 || previousIndex >= STRINGLIST_SIZE
		|| !refString || byteCount == 0
		|| !SL_IsFreeListLocallyValidNoReport())
	{
		return false;
	}

	MT_AllocationInfo allocationInfo{};
	if (MT_TryGetAllocationInfoLegacy(stringValue, &allocationInfo)
			!= MT_AllocationInfoStatus::Success
		|| refString != SL_GetRefStringNoReport(stringValue)
		|| !SL_IsExactStringAllocationNoReport(allocationInfo, byteCount)
		|| GetHashCode(refString->str, byteCount) != owningHash)
	{
		return false;
	}
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(refString));
	if (scr_string_atomic::RefCount(packed) != 0
		|| scr_string_atomic::User(packed) != 0
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, packed))
	{
		return false;
	}

	const HashEntry &target = scrStringGlob.hashTable[targetIndex];
	const uint32_t expectedStatus = targetIndex == owningHash
		? HASH_STAT_HEAD
		: HASH_STAT_MOVABLE;
	const uint32_t nextIndex = static_cast<uint16_t>(target.status_next);
	if (!SL_IsHashEntryEncodingValidNoReport(target)
		|| (target.status_next & HASH_STAT_MASK) != expectedStatus
		|| target.u.prev != stringValue
		|| (targetIndex == owningHash && previousIndex != owningHash)
		|| (targetIndex != owningHash
			&& (!SL_IsHashEntryEncodingValidNoReport(
				scrStringGlob.hashTable[previousIndex])
				|| static_cast<uint16_t>(
					scrStringGlob.hashTable[previousIndex].status_next)
				!= targetIndex
				|| (scrStringGlob.hashTable[previousIndex].status_next
					& HASH_STAT_MASK)
					!= (previousIndex == owningHash
						? HASH_STAT_HEAD
						: HASH_STAT_MOVABLE)))
		|| (nextIndex != owningHash
			&& (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| !SL_IsHashEntryEncodingValidNoReport(
					scrStringGlob.hashTable[nextIndex])
				|| (scrStringGlob.hashTable[nextIndex].status_next
					& HASH_STAT_MASK) != HASH_STAT_MOVABLE)))
	{
		return false;
	}

	SL_UnlinkPlan plan{};
	plan.hash = owningHash;
	plan.targetIndex = targetIndex;
	plan.previousIndex = previousIndex;
	plan.nextIndex = nextIndex;
	plan.freeHead =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	if (MT_TryFreeIndexLegacy(
			stringValue,
			static_cast<int>(byteCount + kRefStringHeaderSize))
		!= MT_FreeIndexStatus::Success)
	{
		return false;
	}

	SL_CommitUnlinkPlanNoReport(plan);
	scrStringGlob.nextFreeEntry = nullptr;
	return true;
}

bool SL_TryFreeResolvedStringNoReport(
	const uint32_t stringValue,
	const SL_LiveStringInfo &info,
	const SL_ValidationScope scope = SL_ValidationScope::Complete,
	MT_ValidationLease* const validationLease = nullptr,
	const MT_ValidationLeaseAdmission* const admission = nullptr) noexcept
{
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(info.refString));
	if (scr_string_atomic::RefCount(packed) != 0
		|| scr_string_atomic::User(packed) != 0
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, packed)
		|| SL_TryFreeStringMemoryNoReport(
			stringValue,
			static_cast<int>(info.byteCount + kRefStringHeaderSize),
			scope,
			validationLease,
			admission)
			!= MT_FreeIndexStatus::Success)
	{
		return false;
	}

	SL_CommitUnlinkPlanNoReport(
		info.unlink, scope == SL_ValidationScope::Leased);
	return true;
}
} // namespace

static bool SL_FreeString(
	const uint32_t stringValue,
	RefString* const refString,
	const uint32_t byteCount)
{
	PROF_SCOPED("SL_FreeString");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return false;
	}
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(refString));
	if (scr_string_atomic::RefCount(packed) != 0)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return true;
	}
	if (scr_string_atomic::User(packed) != 0
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, packed))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return false;
	}

	SL_UnlinkPlan unlink{};
	if (!SL_TryBuildUnlinkPlanForScopeNoReport(
			stringValue,
			refString,
			byteCount,
			&unlink,
			SL_ValidationScope::LegacyLocal)
		|| MT_TryFreeIndexLegacy(
			stringValue,
			static_cast<int>(byteCount + kRefStringHeaderSize))
			!= MT_FreeIndexStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return false;
	}

	SL_CommitUnlinkPlanNoReport(unlink);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return true;
}

namespace script_string
{
namespace
{
[[nodiscard]] bool SL_IsTypedOwnershipAccessAuthorizedLocked(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	return SL_HasOwnershipBatchRegistryActivityLocked()
		? admission != nullptr
			&& SL_IsAuthorizedOwnershipLeaseLocked(validationLease)
		: validationLease == nullptr && admission == nullptr;
}

[[nodiscard]] AcquireResult TryAcquireOrdinaryStringOfSizeInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool authorized =
		SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!authorized)
		return {AcquireStatus::UnsafeFailure, 0};

	if (!bytes || byteCount == 0 || byteCount > UINT32_C(65531)
		|| bytes[byteCount - 1] != '\0'
		|| !SL_IsRepresentableRefStringBytesNoReport(bytes, byteCount))
	{
		return {AcquireStatus::InvalidArgumentNoChange, 0};
	}

	uint32_t stringId = 0;
	const SL_InternStatus status =
		SL_TryInternStringOfSizeWithValidation(
			bytes,
			0,
			byteCount,
			type,
			&stringId,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	switch (status)
	{
	case SL_InternStatus::Success:
		return IsCurrentRuntimeStringId(stringId)
			? AcquireResult{AcquireStatus::Acquired, stringId}
			: AcquireResult{AcquireStatus::UnsafeFailure, 0};
	case SL_InternStatus::PrimaryTableCapacityNoChange:
	case SL_InternStatus::RelocatedTableCapacityNoChange:
	case SL_InternStatus::MemoryCapacityNoChange:
		return {AcquireStatus::CapacityNoChange, 0};
	case SL_InternStatus::RefCountExhaustedNoChange:
		return {AcquireStatus::RefCountExhaustedNoChange, 0};
	case SL_InternStatus::InvalidArgumentNoChange:
		return {AcquireStatus::InvalidArgumentNoChange, 0};
	case SL_InternStatus::UnsafeCleanupFailure:
	case SL_InternStatus::UnsafeFailure:
	default:
		return {AcquireStatus::UnsafeFailure, 0};
	}
}

[[nodiscard]] TransferStatus TryTransferOrdinaryToDatabaseUserInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::UnsafeFailure;
	}
	if (!IsCurrentRuntimeStringId(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::OwnershipMismatchNoChange;
	}
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(
			stringId,
			&info,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	if (resolveStatus != SL_ResolveStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return resolveStatus == SL_ResolveStatus::NotAllocatedNoChange
			? TransferStatus::OwnershipMismatchNoChange
			: TransferStatus::UnsafeFailure;
	}
	const uint16_t refCount = scr_string_atomic::RefCount(info.packed);
	const uint8_t users = scr_string_atomic::User(info.packed);
	if (refCount <= SL_UserReferenceCount(users)
		|| ((users & static_cast<uint8_t>(kDatabaseUserMask)) != 0
			&& !SL_CanDebugRemoveRefNoReport(stringId)))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::OwnershipMismatchNoChange;
	}
	const auto result = scr_string_atomic::TransferRefToUser(
		SL_RefStringWord(info.refString),
		static_cast<uint8_t>(kDatabaseUserMask));
	if (result == scr_string_atomic::TransferRefToUserResult::ReleasedDuplicate)
		SL_DebugRemoveRefNoReport(stringId);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);

	switch (result)
	{
	case scr_string_atomic::TransferRefToUserResult::ClaimedUser:
		return TransferStatus::DatabaseUserClaimed;
	case scr_string_atomic::TransferRefToUserResult::ReleasedDuplicate:
		return TransferStatus::DuplicateReleased;
	case scr_string_atomic::TransferRefToUserResult::Invalid:
	default:
		return TransferStatus::OwnershipMismatchNoChange;
	}
}

[[nodiscard]] ReleaseStatus TryRemoveOrdinaryReferenceInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}
	if (!IsCurrentRuntimeStringId(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(
			stringId,
			&info,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	if (resolveStatus != SL_ResolveStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return resolveStatus == SL_ResolveStatus::NotAllocatedNoChange
			? ReleaseStatus::OwnershipMismatchNoChange
			: ReleaseStatus::UnsafeFailure;
	}
	const uint16_t refCount = scr_string_atomic::RefCount(info.packed);
	if (refCount <= SL_UserReferenceCount(
			scr_string_atomic::User(info.packed))
		|| !SL_CanDebugRemoveRefNoReport(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	const scr_string_atomic::RemoveRefResult result =
		scr_string_atomic::TryRemoveRef(SL_RefStringWord(info.refString));
	if (!result.success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}
	SL_DebugRemoveRefNoReport(stringId);
	const bool validFree = !result.reachedZero
		|| SL_TryFreeResolvedStringNoReport(
			stringId,
			info,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return validFree
		? ReleaseStatus::Success
		: ReleaseStatus::UnsafeFailure;
}

[[nodiscard]] ReleaseStatus TryRemoveDatabaseUserReferenceInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}
	if (!IsCurrentRuntimeStringId(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(
			stringId,
			&info,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	if (resolveStatus != SL_ResolveStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return resolveStatus == SL_ResolveStatus::NotAllocatedNoChange
			? ReleaseStatus::OwnershipMismatchNoChange
			: ReleaseStatus::UnsafeFailure;
	}
	const uint8_t databaseUser = static_cast<uint8_t>(kDatabaseUserMask);
	const uint16_t refCount = scr_string_atomic::RefCount(info.packed);
	const uint8_t users = scr_string_atomic::User(info.packed);
	const uint8_t userReferenceCount = SL_UserReferenceCount(users);
	if (refCount == 0
		|| (users & databaseUser) == 0)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	if (refCount < userReferenceCount
		|| !SL_CanDebugRemoveRefNoReport(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}

	const scr_string_atomic::RemoveUserRefResult result =
		scr_string_atomic::RemoveUserRef(
			SL_RefStringWord(info.refString), databaseUser);
	if (result.status != scr_string_atomic::RemoveUserRefStatus::Removed)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}
	SL_DebugRemoveRefNoReport(stringId);
	const bool validFree = !result.reachedZero
		|| SL_TryFreeResolvedStringNoReport(
			stringId,
			info,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return validFree
		? ReleaseStatus::Success
		: ReleaseStatus::UnsafeFailure;
}

[[nodiscard]] DatabaseUserAddStatus TryAddDatabaseUser4ReferenceInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseUserAddStatus::UnsafeFailure;
	}
	if (!IsCurrentRuntimeStringId(stringId) || !scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseUserAddStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus = SL_TryResolveLiveStringNoReport(
		stringId,
		&info,
		validationLease
			? SL_ValidationScope::Leased
			: SL_ValidationScope::Complete,
		validationLease,
		admission);
	if (resolveStatus != SL_ResolveStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return resolveStatus == SL_ResolveStatus::NotAllocatedNoChange
			? DatabaseUserAddStatus::OwnershipMismatchNoChange
			: DatabaseUserAddStatus::UnsafeFailure;
	}

	const uint8_t databaseUser = static_cast<uint8_t>(kDatabaseUserMask);
	const uint16_t refCount = scr_string_atomic::RefCount(info.packed);
	const uint8_t users = scr_string_atomic::User(info.packed);
	if ((users & databaseUser) != 0)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseUserAddStatus::AlreadyOwnedNoChange;
	}
	if (refCount == scr_string_atomic::kMaxRefCount
		|| !SL_CanDebugAddRefNoReport(stringId))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseUserAddStatus::RefCountExhaustedNoChange;
	}

	const scr_string_atomic::AddUserRefResult result =
		scr_string_atomic::AddUserRef(
			SL_RefStringWord(info.refString), databaseUser);
	if (result == scr_string_atomic::AddUserRefResult::Added)
		SL_DebugAddRefNoReport(stringId);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return result == scr_string_atomic::AddUserRefResult::Added
		? DatabaseUserAddStatus::Added
		: DatabaseUserAddStatus::UnsafeFailure;
}

[[nodiscard]] DatabaseNameResult TryInternDatabaseUser4NameInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool authorized = SL_IsTypedOwnershipAccessAuthorizedLocked(
		validationLease, admission);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!authorized)
		return {DatabaseNameStatus::UnsafeFailure, 0, nullptr};
	if (!bytes || byteCount == 0 || byteCount > UINT32_C(65531)
		|| bytes[byteCount - 1] != '\0'
		|| !SL_IsRepresentableRefStringBytesNoReport(bytes, byteCount))
	{
		return {DatabaseNameStatus::InvalidArgumentNoChange, 0, nullptr};
	}

	uint32_t stringId = 0;
	const SL_InternStatus internStatus =
		SL_TryInternStringOfSizeWithValidation(
			bytes,
			kDatabaseUserMask,
			byteCount,
			type,
			&stringId,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission);
	switch (internStatus)
	{
	case SL_InternStatus::PrimaryTableCapacityNoChange:
	case SL_InternStatus::RelocatedTableCapacityNoChange:
	case SL_InternStatus::MemoryCapacityNoChange:
		return {DatabaseNameStatus::CapacityNoChange, 0, nullptr};
	case SL_InternStatus::RefCountExhaustedNoChange:
		return {
			DatabaseNameStatus::RefCountExhaustedNoChange, 0, nullptr};
	case SL_InternStatus::InvalidArgumentNoChange:
		return {DatabaseNameStatus::InvalidArgumentNoChange, 0, nullptr};
	case SL_InternStatus::UnsafeCleanupFailure:
	case SL_InternStatus::UnsafeFailure:
		return {DatabaseNameStatus::UnsafeFailure, 0, nullptr};
	case SL_InternStatus::Success:
		break;
	default:
		return {DatabaseNameStatus::UnsafeFailure, 0, nullptr};
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString* canonical = nullptr;
	uint32_t verifiedByteCount = 0;
	const bool resolved =
		IsCurrentRuntimeStringId(stringId)
		&& SL_TryGetAllocatedStringByteCountForScopeNoReport(
			stringId,
			&canonical,
			&verifiedByteCount,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission)
		&& verifiedByteCount == byteCount
		&& (scr_string_atomic::User(
				scr_string_atomic::Load(SL_RefStringWord(canonical)))
			& static_cast<uint8_t>(kDatabaseUserMask)) != 0;
	const char* const canonicalName = resolved ? canonical->str : nullptr;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return resolved
		? DatabaseNameResult{
			DatabaseNameStatus::Success, stringId, canonicalName}
		: DatabaseNameResult{
			DatabaseNameStatus::UnsafeFailure, 0, nullptr};
}

[[nodiscard]] DatabaseNameStatus TryReAddRetainedDatabaseNameInternal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission,
	const char* const retainedName) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission)
		|| !retainedName || !scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return retainedName
			? DatabaseNameStatus::UnsafeFailure
			: DatabaseNameStatus::InvalidArgumentNoChange;
	}

	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	const uintptr_t nameAddress =
		reinterpret_cast<uintptr_t>(retainedName);
	constexpr uintptr_t payloadOffset = offsetof(RefString, str);
	const bool addressValid = memoryBegin != 0
		&& memoryBegin <= (std::numeric_limits<uintptr_t>::max)() - MT_SIZE
		&& nameAddress >= memoryBegin + payloadOffset
		&& nameAddress < memoryBegin + MT_SIZE
		&& (nameAddress - memoryBegin - payloadOffset) % MT_NODE_SIZE == 0;
	const uint32_t stringId = addressValid
		? static_cast<uint32_t>(
			(nameAddress - memoryBegin - payloadOffset) / MT_NODE_SIZE)
		: 0;
	RefString* retained = nullptr;
	uint32_t byteCount = 0;
	const bool retainedValid = addressValid
		&& IsCurrentRuntimeStringId(stringId)
		&& SL_TryGetAllocatedStringByteCountForScopeNoReport(
			stringId,
			&retained,
			&byteCount,
			validationLease
				? SL_ValidationScope::Leased
				: SL_ValidationScope::Complete,
			validationLease,
			admission)
		&& retained->str == retainedName
		&& (scr_string_atomic::User(
				scr_string_atomic::Load(SL_RefStringWord(retained)))
			& static_cast<uint8_t>(kRetainedDatabaseUserMask)) != 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!retainedValid)
		return DatabaseNameStatus::OwnershipMismatchNoChange;

	const DatabaseNameResult result = TryInternDatabaseUser4NameInternal(
		validationLease, admission, retainedName, byteCount, 6);
	if (result.status != DatabaseNameStatus::Success)
		return result.status;
	return result.stringId == stringId
		&& result.canonicalName == retainedName
		? DatabaseNameStatus::Success
		: DatabaseNameStatus::UnsafeFailure;
}

[[nodiscard]] DatabaseSweepStatus TryTransferDatabaseUsers4To8Internal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission)
		|| !validationLease || !admission
		|| !SL_IsCompleteStringStateValidForScopeNoReport(
			SL_ValidationScope::Leased, validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseSweepStatus::UnsafeFailure;
	}

	const uint8_t from = static_cast<uint8_t>(kDatabaseUserMask);
	const uint8_t to = static_cast<uint8_t>(kRetainedDatabaseUserMask);
	for (uint32_t hash = 1; hash < STRINGLIST_SIZE; ++hash)
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) == 0)
			continue;
		const uint32_t stringId = scrStringGlob.hashTable[hash].u.prev;
		RefString* refString = nullptr;
		uint32_t byteCount = 0;
		if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
				stringId,
				&refString,
				&byteCount,
				SL_ValidationScope::Leased,
				validationLease,
				admission))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
		(void)byteCount;
		const uint32_t packed =
			scr_string_atomic::Load(SL_RefStringWord(refString));
		const uint8_t users = scr_string_atomic::User(packed);
		if ((users & from) != 0 && (users & to) != 0
			&& (scr_string_atomic::RefCount(packed) <= 1
				|| !SL_CanDebugRemoveRefNoReport(stringId)))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
	}

	for (uint32_t hash = 1; hash < STRINGLIST_SIZE; ++hash)
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) == 0)
			continue;
		const uint32_t stringId = scrStringGlob.hashTable[hash].u.prev;
		RefString* const refString = SL_GetRefStringNoReport(stringId);
		const scr_string_atomic::TransferUserResult result =
			scr_string_atomic::TransferUser(
				SL_RefStringWord(refString), from, to);
		if (result == scr_string_atomic::TransferUserResult::ReleasedDuplicate)
			SL_DebugRemoveRefNoReport(stringId);
		else if (result == scr_string_atomic::TransferUserResult::Invalid)
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return DatabaseSweepStatus::Success;
}

[[nodiscard]] DatabaseSweepStatus TryShutdownDatabaseUser8Internal(
	MT_ValidationLease* const validationLease,
	const MT_ValidationLeaseAdmission* const admission) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsTypedOwnershipAccessAuthorizedLocked(
			validationLease, admission)
		|| !validationLease || !admission
		|| !SL_IsCompleteStringStateValidForScopeNoReport(
			SL_ValidationScope::Leased, validationLease, admission))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseSweepStatus::UnsafeFailure;
	}

	const uint8_t retainedUser =
		static_cast<uint8_t>(kRetainedDatabaseUserMask);
	uint32_t snapshotCount = 0;
	uint32_t requiredMemoryMutations = 0;
	for (uint32_t hash = 1; hash < STRINGLIST_SIZE; ++hash)
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) == 0)
			continue;
		const uint32_t stringId = scrStringGlob.hashTable[hash].u.prev;
		RefString* refString = nullptr;
		uint32_t byteCount = 0;
		if (!SL_TryGetAllocatedStringByteCountForScopeNoReport(
				stringId,
				&refString,
				&byteCount,
				SL_ValidationScope::Leased,
				validationLease,
				admission))
		{
			SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
		(void)byteCount;
		const uint32_t packed =
			scr_string_atomic::Load(SL_RefStringWord(refString));
		if ((scr_string_atomic::User(packed) & retainedUser) == 0)
			continue;
		if (snapshotCount >= STRINGLIST_SIZE
			|| !SL_CanDebugRemoveRefNoReport(stringId))
		{
			SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
		sl_registryOwnershipSweepIds[snapshotCount++] =
			static_cast<uint16_t>(stringId);
		if (scr_string_atomic::RefCount(packed) == 1)
			++requiredMemoryMutations;
	}
	if (validationLease->mutationCount()
		> UINT32_MAX - requiredMemoryMutations)
	{
		SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseSweepStatus::CapacityNoChange;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);

	for (uint32_t index = 0; index < snapshotCount; ++index)
	{
		const uint32_t stringId = sl_registryOwnershipSweepIds[index];
		Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
		SL_LiveStringInfo info{};
		const SL_ResolveStatus resolveStatus = SL_TryResolveLiveStringNoReport(
			stringId,
			&info,
			SL_ValidationScope::Leased,
			validationLease,
			admission);
		if (resolveStatus != SL_ResolveStatus::Success
			|| (scr_string_atomic::User(info.packed) & retainedUser) == 0
			|| !SL_CanDebugRemoveRefNoReport(stringId))
		{
			SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
		const scr_string_atomic::RemoveUserRefResult result =
			scr_string_atomic::RemoveUserRef(
				SL_RefStringWord(info.refString), retainedUser);
		if (result.status
			!= scr_string_atomic::RemoveUserRefStatus::Removed)
		{
			SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return DatabaseSweepStatus::UnsafeFailure;
		}
		SL_DebugRemoveRefNoReport(stringId);
		const bool validFree = !result.reachedZero
			|| SL_TryFreeResolvedStringNoReport(
				stringId,
				info,
				SL_ValidationScope::Leased,
				validationLease,
				admission);
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		if (!validFree)
		{
			SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
			return DatabaseSweepStatus::UnsafeFailure;
		}
	}
	SL_ScrubRegistryOwnershipSweepIdsNoReport(snapshotCount);
	return DatabaseSweepStatus::Success;
}
} // namespace

const MT_ValidationLeaseAdmission &
OwnershipBatch::MakeMemoryTreeLeaseAdmission() noexcept
{
    return MT_ValidationLeaseAdmission::Canonical();
}

bool OwnershipBatch::isCanonicalClearNoLock() const noexcept
{
	return serial_ == 0 && operationCount_ == 0
		&& !active_ && !poisoned_
		&& reserved_[0] == 0 && reserved_[1] == 0;
}

bool OwnershipBatch::ownsRegistryNoLock() const noexcept
{
	const std::uintptr_t address =
		reinterpret_cast<std::uintptr_t>(this);
	if (sl_activeOwnershipBatchAddress != address
		|| sl_activeOwnershipBatchAddressMirror != address
		|| !SL_IsOwnershipBatchRegistryConsistentLocked())
	{
		return false;
	}

	// Only after both by-value outer authorities name this explicit live
	// object may the token or its nested member be read.
	const std::uintptr_t nestedLeaseAddress =
		reinterpret_cast<std::uintptr_t>(&memoryTreeLease_);
	return sl_activeOwnershipBatchNestedLeaseAddress == nestedLeaseAddress
		&& sl_activeOwnershipBatchNestedLeaseAddressMirror
			== nestedLeaseAddress
		&& active_ && serial_ != 0
		&& serial_ == sl_activeOwnershipBatchSerial
		&& reserved_[0] == 0 && reserved_[1] == 0;
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
bool OwnershipBatch::registryNamesStorageNoLock() const noexcept
{
	if (SL_IsOwnershipBatchBoundaryFrozenLocked())
		return false;
	const std::uintptr_t address =
		reinterpret_cast<std::uintptr_t>(this);
	if (sl_activeOwnershipBatchAddress != address
		|| sl_activeOwnershipBatchAddressMirror != address
		|| !SL_IsOwnershipBatchRegistryConsistentLocked())
	{
		return false;
	}
	const std::uintptr_t nestedLeaseAddress =
		reinterpret_cast<std::uintptr_t>(&memoryTreeLease_);
	return sl_activeOwnershipBatchNestedLeaseAddress == nestedLeaseAddress
		&& sl_activeOwnershipBatchNestedLeaseAddressMirror
			== nestedLeaseAddress;
}
#endif

void OwnershipBatch::activateNoLock(const std::uint64_t serial) noexcept
{
	serial_ = serial;
	operationCount_ = 0;
	active_ = true;
	poisoned_ = false;
	reserved_[0] = 0;
	reserved_[1] = 0;
}

void OwnershipBatch::poisonNoLock() noexcept
{
	poisoned_ = true;
}

void OwnershipBatch::clearNoLock() noexcept
{
	serial_ = 0;
	operationCount_ = 0;
	active_ = false;
	poisoned_ = false;
	reserved_[0] = 0;
	reserved_[1] = 0;
}

void OwnershipBatch::poisonBoundaryLocked() noexcept
{
	if (!SL_HasOwnershipBatchRegistryActivityLocked()
		|| SL_IsOwnershipBatchBoundaryFrozenLocked())
	{
		return;
	}

	// Torn authority can poison pointer-free lifecycle state, but only a fully
	// authenticated explicit live token may be written through.
	if (ownsRegistryNoLock())
		poisonNoLock();
	sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Poisoned;
	sl_ownershipBatchLifecycleMirror =
		SL_OwnershipBatchLifecycle::Poisoned;
}

bool OwnershipBatch::canOperateNoLock() const noexcept
{
	return sl_ownershipBatchLifecycle
			== SL_OwnershipBatchLifecycle::Active
		&& sl_ownershipBatchLifecycleMirror
			== SL_OwnershipBatchLifecycle::Active
		&& !poisoned_ && operationCount_ != UINT32_MAX
		&& memoryTreeLease_.active()
		&& !memoryTreeLease_.poisoned()
		&& memoryTreeLease_.mutationCount() != UINT32_MAX;
}

bool OwnershipBatch::tryAuthenticateOperationLocked() noexcept
{
	const bool authenticated = ownsRegistryNoLock()
		&& canOperateNoLock();
	if (!authenticated)
		poisonBoundaryLocked();
	return authenticated;
}

OwnershipBatch::~OwnershipBatch() noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const std::uintptr_t address =
		reinterpret_cast<std::uintptr_t>(this);
	const bool boundaryMentionsThis =
		sl_activeOwnershipBatchAddress == address
		|| sl_activeOwnershipBatchAddressMirror == address
		|| sl_retainedOwnershipBatchAddress == address
		|| sl_retainedOwnershipBatchAddressMirror == address;

	// A never-admitted object, a successfully finished object, and malformed
	// unrelated storage have no authority to revoke the live owner. Do not read
	// any member until by-value boundary state names this explicit live object.
	if (!boundaryMentionsThis)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}

	const bool ownsRetainedScriptAcquisition =
		ownsRegistryNoLock();
	bool releasedNestedAcquisition = false;
	if (ownsRetainedScriptAcquisition)
	{
		// Publish terminal state and remove every global stack identity before
		// either inner or outer retained acquisition can be released.
		SL_FreezeOwnershipBatchBoundaryLocked();
		releasedNestedAcquisition =
			MT_ValidationLease::AbandonFromOwnershipBatch(
			memoryTreeLease_);
		if (releasedNestedAcquisition)
		{
			SL_ClearRetainedOwnershipBatchAuthenticationLocked();
			clearNoLock();
		}
		else
		{
			poisonNoLock();
		}
	}
	else
	{
		SL_FreezeOwnershipBatchBoundaryLocked();
	}

	// Drop this destructor's recursive acquisition first. Only exact global,
	// TLS, local, and nested-address authority proves Begin's retained script
	// acquisition and permits the second release.
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (ownsRetainedScriptAcquisition && releasedNestedAcquisition)
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

bool OwnershipBatch::active() const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool value = ownsRegistryNoLock() && active_;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

bool OwnershipBatch::poisoned() const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool readable = ownsRegistryNoLock();
	const bool value = readable
		? (poisoned_
			|| sl_ownershipBatchLifecycle
				== SL_OwnershipBatchLifecycle::Poisoned
			|| memoryTreeLease_.poisoned())
		: SL_HasOwnershipBatchRegistryActivityLocked();
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

std::uint64_t OwnershipBatch::serial() const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const std::uint64_t value = ownsRegistryNoLock()
		? serial_
		: 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

std::uint32_t OwnershipBatch::operationCount() const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const std::uint32_t value = ownsRegistryNoLock()
		? operationCount_
		: 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

bool OwnershipBatch::canonicalInactive() const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool value = !SL_HasOwnershipBatchRegistryActivityLocked()
		&& isCanonicalClearNoLock();
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

bool OwnershipBatch::authenticates(const std::uint64_t serial) const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool value = serial != 0
		&& ownsRegistryNoLock()
		&& serial_ == serial;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

bool OwnershipBatch::ownsMemoryTreeLease(
	const MT_ValidationLease* const lease) const noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool value = lease
		&& ownsRegistryNoLock()
		&& reinterpret_cast<std::uintptr_t>(lease)
			== sl_activeOwnershipBatchNestedLeaseAddress
		&& reinterpret_cast<std::uintptr_t>(lease)
			== sl_activeOwnershipBatchNestedLeaseAddressMirror;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
MT_ValidationLease &OwnershipBatch::MemoryTreeLeaseForTesting() noexcept
{
	return memoryTreeLease_;
}

void OwnershipBatch::ActivateForTesting(
	const std::uint64_t serial) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_HasOwnershipBatchRegistryActivityLocked())
		activateNoLock(serial);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void OwnershipBatch::SetAuthenticationFieldsForTesting(
	const std::uint64_t serial,
	const std::uint8_t reserved0,
	const std::uint8_t reserved1) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_HasOwnershipBatchRegistryActivityLocked()
		|| registryNamesStorageNoLock())
	{
		serial_ = serial;
		reserved_[0] = reserved0;
		reserved_[1] = reserved1;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void OwnershipBatch::SetOperationCountForTesting(
	const std::uint32_t operationCount) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_HasOwnershipBatchRegistryActivityLocked()
		|| registryNamesStorageNoLock())
	{
		operationCount_ = operationCount;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void OwnershipBatch::SetMemoryTreeMutationCountForTesting(
	const std::uint32_t mutationCount) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_HasOwnershipBatchRegistryActivityLocked()
		|| registryNamesStorageNoLock())
	{
		memoryTreeLease_.SetMutationCountForTesting(mutationCount);
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void ResetOwnershipValidationCountersForTesting() noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	sl_ownershipValidationCounters = {};
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

OwnershipValidationCounters
GetOwnershipValidationCountersForTesting() noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const OwnershipValidationCounters value =
		sl_ownershipValidationCounters;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return value;
}

void SetNextOwnershipBatchSerialForTesting(
	const std::uint64_t serial) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_HasOwnershipBatchRegistryActivityLocked())
		sl_nextOwnershipBatchSerial = serial;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SetOwnershipBatchRegistryMirrorsForTesting(
	const std::uintptr_t address,
	const std::uint64_t serial,
	const std::uintptr_t nestedLeaseAddress,
	const std::uintptr_t addressMirror,
	const std::uint64_t serialMirror,
	const std::uintptr_t nestedLeaseAddressMirror) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	sl_activeOwnershipBatchAddress = address;
	sl_activeOwnershipBatchSerial = serial;
	sl_activeOwnershipBatchNestedLeaseAddress = nestedLeaseAddress;
	sl_activeOwnershipBatchAddressMirror = addressMirror;
	sl_activeOwnershipBatchSerialMirror = serialMirror;
	sl_activeOwnershipBatchNestedLeaseAddressMirror =
		nestedLeaseAddressMirror;
	const SL_OwnershipBatchLifecycle lifecycle =
		address != 0 || serial != 0 || nestedLeaseAddress != 0
			|| addressMirror != 0 || serialMirror != 0
			|| nestedLeaseAddressMirror != 0
		? SL_OwnershipBatchLifecycle::Active
		: SL_OwnershipBatchLifecycle::Idle;
	sl_ownershipBatchLifecycle = lifecycle;
	sl_ownershipBatchLifecycleMirror = lifecycle;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SetOwnershipBatchLifecycleForTesting(
	const std::uint8_t lifecycle,
	const std::uint8_t lifecycleMirror) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	sl_ownershipBatchLifecycle =
		static_cast<SL_OwnershipBatchLifecycle>(lifecycle);
	sl_ownershipBatchLifecycleMirror =
		static_cast<SL_OwnershipBatchLifecycle>(lifecycleMirror);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SetRetainedOwnershipBatchAuthenticationForTesting(
	const std::uintptr_t address,
	const std::uint64_t serial,
	const std::uintptr_t nestedLeaseAddress,
	const std::uintptr_t addressMirror,
	const std::uint64_t serialMirror,
	const std::uintptr_t nestedLeaseAddressMirror) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	sl_retainedOwnershipBatchAddress = address;
	sl_retainedOwnershipBatchSerial = serial;
	sl_retainedOwnershipBatchNestedLeaseAddress = nestedLeaseAddress;
	sl_retainedOwnershipBatchAddressMirror = addressMirror;
	sl_retainedOwnershipBatchSerialMirror = serialMirror;
	sl_retainedOwnershipBatchNestedLeaseAddressMirror =
		nestedLeaseAddressMirror;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void ResetAbandonedOwnershipBatchForTesting(
	const bool releaseRetainedScriptAcquisition,
	const bool releaseRetainedMemoryTreeAcquisition) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	const bool retainedActivity =
		SL_HasRetainedOwnershipBatchAuthenticationLocked();
	const bool retainedAuthenticated =
		sl_retainedOwnershipBatchAddress != 0
		&& sl_retainedOwnershipBatchAddress
			== sl_retainedOwnershipBatchAddressMirror
		&& sl_retainedOwnershipBatchSerial != 0
		&& sl_retainedOwnershipBatchSerial
			== sl_retainedOwnershipBatchSerialMirror
		&& sl_retainedOwnershipBatchNestedLeaseAddress != 0
		&& sl_retainedOwnershipBatchNestedLeaseAddress
			== sl_retainedOwnershipBatchNestedLeaseAddressMirror;
	if ((releaseRetainedScriptAcquisition && !retainedAuthenticated)
		|| (!releaseRetainedScriptAcquisition && retainedActivity))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}

	MT_ResetAbandonedValidationLeaseForTesting(
		releaseRetainedMemoryTreeAcquisition);
	sl_abandonedOwnershipBatchPoison = 0;
	sl_abandonedOwnershipBatchPoisonMirror = 0;
	SL_ClearOwnershipBatchRegistryLocked();
	SL_ClearRetainedOwnershipBatchAuthenticationLocked();
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (releaseRetainedScriptAcquisition)
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}
#endif

OwnershipBatchStatus TryBeginOwnershipBatch(
	OwnershipBatch* const batch) noexcept
{
	if (!batch)
		return OwnershipBatchStatus::InvalidArgument;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		const bool registryValid =
			SL_IsOwnershipBatchRegistryConsistentLocked();
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return registryValid
			? OwnershipBatchStatus::Busy
			: OwnershipBatchStatus::UnsafeFailure;
	}
	// Registry/frozen state is checked before the first supplied-token member
	// read, so a terminal boundary rejects a stale pointer value without
	// dereferencing it. Callers must still honor OwnershipBatch's storage-
	// lifetime contract.
	if (!batch->isCanonicalClearNoLock())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::InvalidToken;
	}
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::InvalidState;
	}
	if (sl_nextOwnershipBatchSerial == UINT64_MAX)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::UnsafeFailure;
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	MT_ValidationLease &memoryTreeLease = batch->memoryTreeLease_;
	const MT_ValidationLeaseStatus memoryStatus =
		MT_TryBeginValidationLease(&memoryTreeLease, admission);
	if (memoryStatus != MT_ValidationLeaseStatus::Success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		switch (memoryStatus)
		{
		case MT_ValidationLeaseStatus::Busy:
			return OwnershipBatchStatus::Busy;
		case MT_ValidationLeaseStatus::InvalidArgument:
		case MT_ValidationLeaseStatus::InvalidToken:
			return OwnershipBatchStatus::InvalidToken;
		case MT_ValidationLeaseStatus::UnsafeFailure:
		default:
			return OwnershipBatchStatus::UnsafeFailure;
		}
	}

	if (!SL_IsCompleteStringStateValidForScopeNoReport(
			SL_ValidationScope::Leased, &memoryTreeLease, &admission))
	{
		(void)MT_FinishValidationLease(&memoryTreeLease, admission);
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::UnsafeFailure;
	}

	const std::uint64_t serial = ++sl_nextOwnershipBatchSerial;
	batch->activateNoLock(serial);
	const std::uintptr_t address =
		reinterpret_cast<std::uintptr_t>(batch);
	const std::uintptr_t nestedLeaseAddress =
		reinterpret_cast<std::uintptr_t>(&memoryTreeLease);
	sl_activeOwnershipBatchAddress = address;
	sl_activeOwnershipBatchSerial = serial;
	sl_activeOwnershipBatchNestedLeaseAddress = nestedLeaseAddress;
	sl_activeOwnershipBatchAddressMirror = address;
	sl_activeOwnershipBatchSerialMirror = serial;
	sl_activeOwnershipBatchNestedLeaseAddressMirror = nestedLeaseAddress;
	sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Active;
	sl_ownershipBatchLifecycleMirror = SL_OwnershipBatchLifecycle::Active;
	sl_retainedOwnershipBatchAddress = address;
	sl_retainedOwnershipBatchSerial = serial;
	sl_retainedOwnershipBatchNestedLeaseAddress = nestedLeaseAddress;
	sl_retainedOwnershipBatchAddressMirror = address;
	sl_retainedOwnershipBatchSerialMirror = serial;
	sl_retainedOwnershipBatchNestedLeaseAddressMirror = nestedLeaseAddress;
	// Retain SCRIPT_STRING and the nested MEMORY_TREE lease until Finish.
	return OwnershipBatchStatus::Success;
}

OwnershipBatchStatus FinishOwnershipBatch(
	OwnershipBatch* const batch) noexcept
{
	if (!batch)
		return OwnershipBatchStatus::InvalidArgument;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!SL_IsOwnershipBatchRegistryConsistentLocked()
		|| !batch->ownsRegistryNoLock())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::InvalidToken;
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const bool stringStateValid =
		SL_IsCompleteStringStateValidForScopeNoReport(
			SL_ValidationScope::Leased,
			&batch->memoryTreeLease_,
			&admission);
	const bool poisoned = batch->poisoned_
		|| sl_ownershipBatchLifecycle
			== SL_OwnershipBatchLifecycle::Poisoned
		|| !stringStateValid;
	const MT_ValidationLeaseStatus memoryStatus =
		MT_FinishValidationLease(
			&batch->memoryTreeLease_, admission);
	if (memoryStatus == MT_ValidationLeaseStatus::InvalidArgument
		|| memoryStatus == MT_ValidationLeaseStatus::InvalidToken
		|| memoryStatus == MT_ValidationLeaseStatus::Busy)
	{
		// The nested retained acquisition was not released. Preserve the outer
		// registry and base SCRIPT_STRING acquisition for explicit recovery.
		batch->poisonBoundaryLocked();
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return OwnershipBatchStatus::InvalidToken;
	}

	SL_ClearOwnershipBatchRegistryLocked();
	SL_ClearRetainedOwnershipBatchAuthenticationLocked();
	batch->clearNoLock();
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return memoryStatus == MT_ValidationLeaseStatus::Success && !poisoned
		? OwnershipBatchStatus::Success
		: OwnershipBatchStatus::UnsafeFailure;
}

AcquireResult TryAcquireOrdinaryStringOfSize(
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	return TryAcquireOrdinaryStringOfSizeInternal(
		nullptr, nullptr, bytes, byteCount, type);
}

AcquireResult TryAcquireOrdinaryStringOfSize(
	OwnershipBatch &batch,
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return {AcquireStatus::UnsafeFailure, 0};
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const AcquireResult result = TryAcquireOrdinaryStringOfSizeInternal(
		&batch.memoryTreeLease_, &admission, bytes, byteCount, type);
	if (result.status == AcquireStatus::Acquired)
		++batch.operationCount_;
	else if (result.status == AcquireStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return result;
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
	const uint32_t stringId) noexcept
{
	return TryTransferOrdinaryToDatabaseUserInternal(
		nullptr, nullptr, stringId);
}

TransferStatus TryTransferOrdinaryToDatabaseUser(
	OwnershipBatch &batch,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::UnsafeFailure;
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const TransferStatus status =
		TryTransferOrdinaryToDatabaseUserInternal(
			&batch.memoryTreeLease_, &admission, stringId);
	if (status == TransferStatus::DatabaseUserClaimed
		|| status == TransferStatus::DuplicateReleased)
	{
		++batch.operationCount_;
	}
	else if (status == TransferStatus::UnsafeFailure)
	{
		batch.poisoned_ = true;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

ReleaseStatus TryRemoveOrdinaryReference(
	const uint32_t stringId) noexcept
{
	return TryRemoveOrdinaryReferenceInternal(nullptr, nullptr, stringId);
}

ReleaseStatus TryRemoveOrdinaryReference(
	OwnershipBatch &batch,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const ReleaseStatus status = TryRemoveOrdinaryReferenceInternal(
		&batch.memoryTreeLease_, &admission, stringId);
	if (status == ReleaseStatus::Success)
		++batch.operationCount_;
	else if (status == ReleaseStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

ReleaseStatus TryRemoveDatabaseUserReference(
	const uint32_t stringId) noexcept
{
	return TryRemoveDatabaseUserReferenceInternal(
		nullptr, nullptr, stringId);
}

ReleaseStatus TryRemoveDatabaseUserReference(
	OwnershipBatch &batch,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::UnsafeFailure;
	}

	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const ReleaseStatus status = TryRemoveDatabaseUserReferenceInternal(
		&batch.memoryTreeLease_, &admission, stringId);
	if (status == ReleaseStatus::Success)
		++batch.operationCount_;
	else if (status == ReleaseStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

DatabaseUserAddStatus TryAddDatabaseUser4Reference(
	OwnershipBatch &batch,
	const uint32_t stringId) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseUserAddStatus::UnsafeFailure;
	}
	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const DatabaseUserAddStatus status =
		TryAddDatabaseUser4ReferenceInternal(
			&batch.memoryTreeLease_, &admission, stringId);
	if (status == DatabaseUserAddStatus::Added)
		++batch.operationCount_;
	else if (status == DatabaseUserAddStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

DatabaseNameResult TryInternDatabaseUser4Name(
	OwnershipBatch &batch,
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return {DatabaseNameStatus::UnsafeFailure, 0, nullptr};
	}
	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const DatabaseNameResult result = TryInternDatabaseUser4NameInternal(
		&batch.memoryTreeLease_, &admission, bytes, byteCount, type);
	if (result.status == DatabaseNameStatus::Success)
		++batch.operationCount_;
	else if (result.status == DatabaseNameStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return result;
}

DatabaseNameStatus TryReAddRetainedDatabaseName(
	OwnershipBatch &batch,
	const char* const retainedName) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseNameStatus::UnsafeFailure;
	}
	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const DatabaseNameStatus status =
		TryReAddRetainedDatabaseNameInternal(
			&batch.memoryTreeLease_, &admission, retainedName);
	if (status == DatabaseNameStatus::Success)
		++batch.operationCount_;
	else if (status == DatabaseNameStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

DatabaseSweepStatus TryTransferDatabaseUsers4To8(
	OwnershipBatch &batch) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseSweepStatus::UnsafeFailure;
	}
	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const DatabaseSweepStatus status =
		TryTransferDatabaseUsers4To8Internal(
			&batch.memoryTreeLease_, &admission);
	if (status == DatabaseSweepStatus::Success)
		++batch.operationCount_;
	else if (status == DatabaseSweepStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}

DatabaseSweepStatus TryShutdownDatabaseUser8(
	OwnershipBatch &batch) noexcept
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!batch.tryAuthenticateOperationLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return DatabaseSweepStatus::UnsafeFailure;
	}
	const MT_ValidationLeaseAdmission &admission =
		OwnershipBatch::MakeMemoryTreeLeaseAdmission();
	const DatabaseSweepStatus status = TryShutdownDatabaseUser8Internal(
		&batch.memoryTreeLease_, &admission);
	if (status == DatabaseSweepStatus::Success)
		++batch.operationCount_;
	else if (status == DatabaseSweepStatus::UnsafeFailure)
		batch.poisoned_ = true;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return status;
}
} // namespace script_string

const char* __cdecl SL_DebugConvertToString(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return "<UNAVAILABLE>";
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return "<UNAVAILABLE>";
	}
	if (!script_string::IsCurrentRuntimeStringId(stringValue))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return "<NULL>";
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refString, &byteCount))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return "<UNAVAILABLE>";
	}
	for (uint32_t index = 0; index + 1 < byteCount; ++index)
	{
		if (!isprint(static_cast<unsigned char>(refString->str[index])))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return "<BINARY>";
		}
	}
	const char* const result = refString->str;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return result;
}

uint32_t SL_ConvertFromString(const char* str)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	const uintptr_t memoryBegin =
		reinterpret_cast<uintptr_t>(scrMemTreePub.mt_buffer);
	const uintptr_t stringAddress = reinterpret_cast<uintptr_t>(str);
	const bool addressValid = str != nullptr && memoryBegin != 0
		&& memoryBegin <= (std::numeric_limits<uintptr_t>::max)() - MT_SIZE
		&& stringAddress >= memoryBegin + offsetof(RefString, str)
		&& stringAddress < memoryBegin + MT_SIZE
		&& (stringAddress - memoryBegin - offsetof(RefString, str))
			% MT_NODE_SIZE == 0;
	const uint32_t candidate = addressValid
		? static_cast<uint32_t>(
			(stringAddress - memoryBegin - offsetof(RefString, str))
			/ MT_NODE_SIZE)
		: 0;
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = addressValid
		&& SL_TryResolveLegacyTransferTargetNoReport(
			candidate, &refString, &byteCount)
		&& refString->str == str;
	(void)byteCount;
	const uint32_t result = valid ? candidate : 0;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return 0;
	}
	return result;
}

uint32_t SL_FindLowercaseString(const char* str)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char stra[8196]; // [esp+5Ch] [ebp-2010h] BYREF
	uint32_t len; // [esp+2064h] [ebp-8h]
	signed int i; // [esp+2068h] [ebp-4h]

	PROF_SCOPED("SL_FindLowercaseString");
	len = strlen(str) + 1;
	if ((int)len <= 0x2000)
	{
		for (i = 0; i < (int)len; ++i)
		{
			stra[i] = static_cast<char>(tolower(
				static_cast<unsigned char>(str[i])));
		}
		return FindStringOfSize(stra, len);
	}
	else
	{
		return 0;
	}
}

namespace
{
bool SL_TryRemoveRefToStringLockedNoReport(
	const uint32_t stringValue,
	const uint32_t expectedByteCount,
	const bool enforceExpectedByteCount) noexcept
{
	if (SL_HasOwnershipBatchRegistryActivityLocked())
		return false;
	RefString* refStr = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refStr, &byteCount)
		|| (enforceExpectedByteCount
			&& byteCount != expectedByteCount))
	{
		return false;
	}
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(refStr));
	if (scr_string_atomic::RefCount(packed)
		<= SL_UserReferenceCount(scr_string_atomic::User(packed)))
		return false;
	const bool debugOwnershipExact =
		SL_IsDebugOwnershipExactNoReport(stringValue, packed);
	if (!debugOwnershipExact)
		return false;

	const scr_string_atomic::RemoveRefResult result =
		scr_string_atomic::TryRemoveRef(SL_RefStringWord(refStr));
	if (!result.success)
		return false;

	// Account the old owner before a zero transition can return the memory-tree
	// slot to the allocator and let a new string reuse the same debug index.
	SL_DebugRemoveRefNoReport(stringValue);
	const bool validFree =
		!result.reachedZero
		|| SL_FreeString(stringValue, refStr, byteCount);
	if (!validFree)
	{
		// SL_FreeString rejects before mutation unless both allocator free and
		// hash unlink can commit. Roll back the preceding ownership transition
		// while this outer script-string lock still makes the entry exclusive.
		Sys_AtomicStore(SL_RefStringWord(refStr), packed);
		SL_DebugAddRefNoReport(stringValue);
	}
	return validFree;
}
} // namespace

void SL_RemoveRefToStringOfSize(uint32_t stringValue, uint32_t len)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	PROF_SCOPED("SL_RemoveRefToStringOfSize");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	const bool released = SL_TryRemoveRefToStringLockedNoReport(
		stringValue, len, true);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	(void)released;
	iassert(released);
}

void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return;
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool added = SL_IsValidUserMask(user, true)
		&& SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refString, &byteCount)
		&& SL_TryAddUserInternalNoReport(refString, user);
	(void)byteCount;
	if (!added)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		Com_Error(ERR_DROP, "invalid script string reference increment");
		return;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void __cdecl Scr_SetString(uint16_t *to, uint32_t from)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	if (from)
		SL_AddRefToString(from);
	if (*to)
		SL_RemoveRefToString(*to);
	*to = from;
}

uint32_t __cdecl SL_ConvertToLowercase(uint32_t stringValue, uint32_t user, int type)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char str[8192]; // [esp+50h] [ebp-2010h] BYREF

	PROF_SCOPED("SL_ConvertToLowercase");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refString, &byteCount))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	if (byteCount > sizeof(str))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return stringValue;
	}
	for (uint32_t index = 0; index < byteCount; ++index)
	{
		str[index] = static_cast<char>(tolower(
			static_cast<unsigned char>(refString->str[index])));
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);

	const uint32_t stringOfSize =
		SL_GetStringOfSize(str, user, byteCount, type);
	SL_RemoveRefToString(stringValue);
	return stringOfSize;
}

void __cdecl CreateCanonicalFilename(char *newFilename, const char *filename, int count)
{
	uint32_t c; // [esp+0h] [ebp-4h]
	const int oldCount = count; // addition because the old assert was broken, lol

	iassert(count);
	do
	{
		do
		{
			do
				c = static_cast<unsigned char>(*filename++);
			while (c == '\\');
		} while (c == '/');
		while (c >= ' ')
		{
			*newFilename++ = static_cast<char>(tolower(
				static_cast<unsigned char>(c)));
			if (!--count)
				Com_Error(ERR_DROP, "Filename %s exceeds maximum length of %d", filename, oldCount);
			if (c == '/')
				break;
			c = static_cast<unsigned char>(*filename++);
			if (c == '\\')
				c = '/';
		}
	} while (c);
	*newFilename = 0;
}

uint32_t __cdecl Scr_CreateCanonicalFilename(const char *filename)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	char newFilename[1028]; // [esp+0h] [ebp-408h] BYREF

	CreateCanonicalFilename(newFilename, filename, 1024);
	return SL_GetString_(newFilename, 0, 7);
}

void Scr_SetStringFromCharString(uint16_t *to, const char *from)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return;
	uint32_t v4; // r3
	const char *v5; // r11

	v4 = *to;
	if (v4)
		SL_RemoveRefToString(v4);
	v5 = from;
	while (*(uint8_t *)v5++)
		;
	*to = SL_GetStringOfSize(from, 0, v5 - from, 6);
}

uint32_t SL_GetUser(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return 0;
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (SL_HasOwnershipBatchRegistryActivityLocked()
		|| !SL_TryResolveLegacyTransferTargetNoReport(
			stringValue, &refString, &byteCount))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}
	const uint32_t user = scr_string_atomic::User(
		scr_string_atomic::Load(
			SL_RefStringWord(refString)));
	(void)byteCount;
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return user;
}

const char *SL_ConvertToStringSafe(uint32_t stringValue)
{
	if (SL_IsOwnershipBatchActiveNoReport())
		return "<UNAVAILABLE>";
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (SL_HasOwnershipBatchRegistryActivityLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return "<UNAVAILABLE>";
	}
	if (!stringValue)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return "(NULL)";
	}

	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	const bool valid = SL_TryResolveLegacyTransferTargetNoReport(
		stringValue, &refString, &byteCount);
	(void)byteCount;
	const char* const result = valid ? refString->str : "(NULL)";
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!valid)
	{
		iassert(valid);
		return "(NULL)";
	}
	return result;
}
