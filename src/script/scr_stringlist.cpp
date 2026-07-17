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

scrStringDebugGlob_t* scrStringDebugGlob;
static scrStringDebugGlob_t scrStringDebugGlobBuf;
static scrStringGlob_t scrStringGlob; // 0x244E300

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
	iassert(sys == SCR_SYS_GAME);
	return SL_GetString(s, 1);
}

void SL_Init()
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	iassert(!scrStringGlob.inited);

	MT_Init();

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
	iassert(!scrStringDebugGlob);

	Com_Memset(&scrStringDebugGlobBuf, 0, sizeof(scrStringDebugGlobBuf));
	scrStringDebugGlob = &scrStringDebugGlobBuf;
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
	const uint32_t user) noexcept
{
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

bool SL_AddUserInternal(RefString* refStr, uint32_t user)
{
	const uint8_t userByte = static_cast<uint8_t>(user);
	iassert(SL_IsValidUserMask(user, true));
	if (!SL_IsValidUserMask(user, true))
		return false;

	const scr_string_atomic::AddUserRefResult result =
		scr_string_atomic::AddUserRef(SL_RefStringWord(refStr), userByte);
	if (result == scr_string_atomic::AddUserRefResult::Invalid)
		return false;
	if (result == scr_string_atomic::AddUserRefResult::Added)
		SL_DebugAddRef(SL_ConvertFromRefString(refStr));
	return true;
}

void SL_AddRefToString(uint32_t stringValue)
{
	PROF_SCOPED("SL_AddRefToString");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString* refStr = GetRefString(stringValue);
	if (!scr_string_atomic::TryAddRef(SL_RefStringWord(refStr)))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		Com_Error(ERR_DROP, "invalid script string reference increment");
		return;
	}
	SL_DebugAddRef(stringValue);
	iassert(scr_string_atomic::RefCount(
		scr_string_atomic::Load(SL_RefStringWord(refStr))) != 0);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SL_CheckExists(uint32_t stringValue)
{
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	iassert(!scrStringDebugGlob || SL_DebugRefCount(stringValue));
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

static void SL_CheckLeaks()
{
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
	if (scrStringGlob.inited)
	{
		scrStringGlob.inited = 0;
		SL_CheckLeaks();
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void SL_ShutdownSystem(uint32_t user)
{
	iassert(SL_IsValidUserMask(user, false));
	const uint8_t userByte = static_cast<uint8_t>(user);
	if (!SL_IsValidUserMask(user, false))
	{
		Com_Error(ERR_DROP, "invalid script string shutdown user mask");
		return;
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);

	bool invalidTransition = false;
	for (uint32_t hash = 1;
		hash < STRINGLIST_SIZE && !invalidTransition;
		++hash)
	{
		do
		{
			if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) == 0)
				break;

			const uint32_t stringValue = scrStringGlob.hashTable[hash].u.prev;
			RefString* refStr = GetRefString(stringValue);
			const uint32_t len = static_cast<uint32_t>(SL_GetRefStringLen(refStr) + 1);
			const scr_string_atomic::RemoveUserRefResult result =
				scr_string_atomic::RemoveUserRef(
					SL_RefStringWord(refStr), userByte);
			if (result.status == scr_string_atomic::RemoveUserRefStatus::NotPresent)
				break;
			if (result.status == scr_string_atomic::RemoveUserRefStatus::Invalid)
			{
				invalidTransition = true;
				break;
			}

			scrStringGlob.nextFreeEntry = 0;
			SL_DebugRemoveRef(stringValue);
			if (result.reachedZero && !SL_FreeString(stringValue, refStr, len))
				invalidTransition = true;
		} while (scrStringGlob.nextFreeEntry);
	}

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	if (invalidTransition)
		Com_Error(ERR_DROP, "invalid script string user-reference removal");
}

int SL_IsLowercaseString(uint32_t stringValue)
{
	iassert(stringValue);

	for (const char* str = SL_ConvertToString(stringValue); *str; ++str)
	{
		int cmp = *str;
		if (cmp != (char)tolower(cmp))
		{
			return 0;
		}
	}

	return 1;
}

void SL_TransferSystem(uint32_t from, uint32_t to)
{
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

	bool invalidTransition = false;
	for (uint32_t hash = 1;
		hash < STRINGLIST_SIZE && !invalidTransition;
		++hash)
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) != 0)
		{
			const uint32_t stringValue = scrStringGlob.hashTable[hash].u.prev;
			RefString* refStr = GetRefString(stringValue);
			const scr_string_atomic::TransferUserResult result =
				scr_string_atomic::TransferUser(
				SL_RefStringWord(refStr), fromByte, toByte);
			if (result == scr_string_atomic::TransferUserResult::ReleasedDuplicate)
				SL_DebugRemoveRef(stringValue);
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
	return SL_GetStringOfSize(str, user, strlen(str) + 1, type);
}

namespace
{
bool SL_IsInternHashStateValidNoReport(uint32_t hash) noexcept;
bool SL_TryGetAllocatedStringByteCountNoReport(
	uint32_t stringValue,
	RefString** outRefString,
	uint32_t* outByteCount) noexcept;

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

SL_InternStatus SL_TryInternStringOfSize(
	const char* str,
	uint32_t user,
	uint32_t len,
	int type,
	uint32_t* outStringValue) noexcept
{
	PROF_SCOPED("SL_GetStringOfSize");

	const uint8_t userByte = static_cast<uint8_t>(user);
	if (!str || !outStringValue || len == 0 || len > UINT32_C(65531)
		|| type <= 0 || type >= 22
		|| !SL_IsValidUserMask(user, true))
		return SL_InternStatus::InvalidArgumentNoChange;

	uint32_t hash = GetHashCode(str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return SL_InternStatus::InvalidArgumentNoChange;
	}
	if (!SL_IsInternHashStateValidNoReport(hash))
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
		if (!SL_TryGetAllocatedStringByteCountNoReport(
				entry->u.prev, &refStr, &candidateByteCount))
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return SL_InternStatus::UnsafeFailure;
		}

		// Check if this string is already stored, if it matches the string at this particular hash lookup, and return existing entry if so.
		if (candidateByteCount == len
			&& !memcmp(refStr->str, str, len))
		{
			if (!SL_TryAddUserInternalNoReport(refStr, user))
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
			if (!SL_TryGetAllocatedStringByteCountNoReport(
					newEntry->u.prev, &refStr, &candidateByteCount))
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
				if (!SL_TryAddUserInternalNoReport(refStr, user))
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
		const MT_AllocIndexStatus allocationStatus = MT_TryAllocIndex(
			static_cast<int>(len + 4), type, &allocatedIndex);
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
				MT_TryFreeIndex(stringValue, static_cast<int>(len + 4));
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
			const MT_AllocIndexStatus allocationStatus = MT_TryAllocIndex(
				static_cast<int>(len + 4), type, &allocatedIndex);
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
					MT_TryFreeIndex(
						stringValue, static_cast<int>(len + 4));
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
		}
		else
		{
			uint16_t allocatedIndex = 0;
			const MT_AllocIndexStatus allocationStatus = MT_TryAllocIndex(
				static_cast<int>(len + 4), type, &allocatedIndex);
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
					MT_TryFreeIndex(
						stringValue, static_cast<int>(len + 4));
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
} // namespace

uint32_t SL_GetStringOfSize(const char* str, uint32_t user, uint32_t len, int type)
{
	iassert(str);
	iassert(SL_IsValidUserMask(user, true));
	if (!SL_IsValidUserMask(user, true))
	{
		Com_Error(ERR_DROP, "script string user mask exceeds 8 bits");
		return 0;
	}

	uint32_t stringValue = 0;
	const SL_InternStatus status =
		SL_TryInternStringOfSize(str, user, len, type, &stringValue);
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
	iassert(!stringValue || !scrStringDebugGlob || SL_DebugRefCount(stringValue));

	if (stringValue)
	{
		return GetRefString(stringValue)->str;
	}
	else
	{
		return NULL;
	}
}

RefString* GetRefString(uint32_t stringValue)
{
	iassert(stringValue);
	iassert(stringValue * MT_NODE_SIZE < MT_SIZE);

	return (RefString*)(&scrMemTreePub.mt_buffer[MT_NODE_SIZE * stringValue]);
}
RefString* GetRefString(const char* str)
{
	iassert(str >= scrMemTreePub.mt_buffer && str < scrMemTreePub.mt_buffer + MT_SIZE);

	return (RefString*)(str - 4);
}

int SL_GetStringLen(uint32_t stringValue)
{
	iassert(stringValue);
	RefString* refString = GetRefString(stringValue);
	return SL_GetRefStringLen(refString);
}

static uint32_t FindStringOfSize(const char* str, uint32_t len)
{
	uint32_t stringValue = 0;

	PROF_SCOPED("FindStringOfSize");

	iassert(str);
	uint32_t hash = GetHashCode(str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!scrStringGlob.inited || !SL_IsInternHashStateValidNoReport(hash))
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
	if (!SL_TryGetAllocatedStringByteCountNoReport(
			entry->u.prev, &refStr, &candidateByteCount))
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
			iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_MOVABLE);
			if (!SL_TryGetAllocatedStringByteCountNoReport(
					newEntry->u.prev, &refStr, &candidateByteCount))
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

				iassert((newEntry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
				iassert((entry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
				iassert(refStr->str == SL_ConvertToString(stringValue));
		
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return stringValue;
			}
			prev = newIndex;
			newIndex = (uint16_t)newEntry->status_next;
		} // for()
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	} //memcmp

	iassert((entry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);

	stringValue = entry->u.prev;
	iassert(refStr->str == SL_ConvertToString(stringValue));

	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return stringValue;
}

uint32_t SL_FindString(const char* str)
{
	return FindStringOfSize(str, strlen(str) + 1);
}

void __cdecl SL_TransferRefToUser(uint32_t stringValue, uint32_t user)
{
	PROF_SCOPED("SL_TransferRefToUser");

	const uint8_t userByte = static_cast<uint8_t>(user);
	iassert(SL_IsValidUserMask(user, false));
	if (!SL_IsValidUserMask(user, false))
	{
		Com_Error(ERR_DROP, "invalid script string transfer user mask");
		return;
	}

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString *const refStr = GetRefString(stringValue);
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
		SL_DebugRemoveRef(stringValue);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

uint32_t SL_GetStringForVector(const float* v)
{
	char tempString[132];

	snprintf(tempString, sizeof(tempString), "(%g, %g, %g)", *v, v[1], v[2]);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetStringForInt(int i)
{
	char tempString[132]; // [esp+0h] [ebp-88h] BYREF

	snprintf(tempString, sizeof(tempString), "%i", i);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetStringForFloat(float f)
{
	char tempString[132]; // [esp+8h] [ebp-88h] BYREF

	snprintf(tempString, sizeof(tempString), "%g", f);
	return SL_GetString_(tempString, 0, 15);
}

uint32_t SL_GetString(const char* str, uint32_t user)
{
	return SL_GetString_(str, user, 6);
}

//char *mt_buffer;  //     scrMemTreePub.mt_buffer = (char*)&scrMemTreeGlob.nodes;


int SL_GetRefStringLen(RefString* refString)
{
	const uint8_t byteLength = scr_string_atomic::ByteLength(
		scr_string_atomic::Load(SL_RefStringWord(refString)));
	int len = static_cast<uint8_t>(byteLength - 1);

	while (refString->str[len])
		len += 256;

	// lwss add some asserts for sanity
	iassert((uintptr_t)refString->str >= (uintptr_t)&scrMemTreeGlob.nodes[0] && (uintptr_t)refString->str < (uintptr_t)&scrMemTreeGlob.nodes[MEMORY_NODE_COUNT]);
	iassert((uintptr_t)&refString->str[len + 1] >= (uintptr_t)&scrMemTreeGlob.nodes[0] && (uintptr_t)&refString->str[len + 1] < (uintptr_t)&scrMemTreeGlob.nodes[MEMORY_NODE_COUNT]);

	return len;
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
			stra[i] = tolower(str[i]);
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
	return GetLowercaseStringOfSize(str, user, strlen(str) + 1, type);
}
uint32_t SL_GetLowercaseString(const char* str, uint32_t user)
{
	return SL_GetLowercaseString_(str, user, 6);
}

void SL_RemoveRefToString(uint32_t stringValue)
{
	RefString* refStr; // [esp+30h] [ebp-8h]
	int len; // [esp+34h] [ebp-4h]

	PROF_SCOPED("SL_RemoveRefToString");

	refStr = GetRefString(stringValue);
	len = SL_GetRefStringLen(refStr) + 1;
	SL_RemoveRefToStringOfSize(stringValue, len);
}

static bool SL_FreeString(uint32_t stringValue, RefString* refStr, uint32_t len)
{
	PROF_SCOPED("SL_FreeString");

	uint32_t index = GetHashCode(refStr->str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);

	const uint32_t packed = scr_string_atomic::Load(SL_RefStringWord(refStr));
	if (scr_string_atomic::RefCount(packed) != 0)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return true;
	}
	else
	{
		HashEntry *entry = &scrStringGlob.hashTable[index];

		const uint8_t user = scr_string_atomic::User(packed);
		iassert(user == 0);
		if (user != 0)
		{
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return false;
		}

		MT_FreeIndex(stringValue, len + 4);

		iassert(((entry->status_next & HASH_STAT_MASK) == HASH_STAT_HEAD));

		uint32_t newIndex = (uint16_t)entry->status_next;
		HashEntry* newEntry = &scrStringGlob.hashTable[newIndex];

		if (entry->u.prev == stringValue)
		{
			if (newEntry == entry)
			{
				newEntry = entry;
				newIndex = index;
			}
			else
			{
				entry->status_next = (uint16_t)newEntry->status_next | HASH_STAT_HEAD;
				entry->u.prev = newEntry->u.prev;
				scrStringGlob.nextFreeEntry = entry;
			}
		}
		else
		{
			uint32_t prev = index;
			while (1)
			{
				iassert(newEntry != entry);
				iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_MOVABLE);

				if (newEntry->u.prev == stringValue)
					break;

				prev = newIndex;
				newIndex = (uint16_t)newEntry->status_next;
				newEntry = &scrStringGlob.hashTable[newIndex];
			}
			scrStringGlob.hashTable[prev].status_next = (uint16_t)newEntry->status_next | (scrStringGlob.hashTable[prev].status_next & HASH_STAT_MASK);
		}

		iassert((newEntry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
		uint32_t newNext = scrStringGlob.hashTable[0].status_next;
		iassert((newNext & HASH_STAT_MASK) == HASH_STAT_FREE);

		newEntry->status_next = newNext;
		newEntry->u.prev = 0;
		scrStringGlob.hashTable[newNext].u.prev = newIndex;
		scrStringGlob.hashTable[0].status_next = newIndex;
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return true;
	}
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

uint8_t sl_hashChainVisited[(STRINGLIST_SIZE + 7) / 8];
uint8_t sl_stringIdVisited[SL_MAX_STRING_INDEX / 8];
uint8_t sl_freeListVisited[(STRINGLIST_SIZE + 7) / 8];

void SL_ResetHashChainValidationNoReport() noexcept
{
	memset(sl_hashChainVisited, 0, sizeof(sl_hashChainVisited));
	memset(sl_stringIdVisited, 0, sizeof(sl_stringIdVisited));
}

bool SL_TryRecordStringIdNoReport(const uint32_t stringId) noexcept
{
	if (!script_string::IsCurrentRuntimeStringId(stringId))
		return false;
	const uint32_t byteIndex = stringId >> 3;
	const uint8_t bitMask =
		static_cast<uint8_t>(1u << (stringId & 7u));
	if ((sl_stringIdVisited[byteIndex] & bitMask) != 0)
		return false;
	sl_stringIdVisited[byteIndex] |= bitMask;
	return true;
}

bool SL_TryGetAllocatedStringByteCountNoReport(
	const uint32_t stringValue,
	RefString** const outRefString,
	uint32_t* const outByteCount) noexcept
{
	if (!outRefString || !outByteCount
		|| !script_string::IsCurrentRuntimeStringId(stringValue))
		return false;
	MT_AllocationInfo allocationInfo{};
	if (MT_TryGetAllocationInfo(stringValue, &allocationInfo)
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
	const uint32_t packedByteCount =
		scr_string_atomic::ByteLength(packed) == 0
		? UINT32_C(256)
		: scr_string_atomic::ByteLength(packed);
	// Legacy explicit-size callers intern compact binary records whose final
	// byte is not a NUL (notably XAnimToXModel). For the first 256 bytes the
	// packed length plus the exact allocator size class recovers their complete
	// length without scanning. Longer report-free strings retain the bounded
	// congruent-terminator recovery below.
	if (SL_IsExactStringAllocationNoReport(allocationInfo, packedByteCount))
	{
		byteCount = packedByteCount;
	}
	else if (!SL_TryGetBoundedRefStringByteCount(
			refString, packed, allocationInfo.capacityBytes, &byteCount))
	{
		return false;
	}
	if (!SL_IsExactStringAllocationNoReport(allocationInfo, byteCount)
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, packed))
	{
		return false;
	}

	*outRefString = refString;
	*outByteCount = byteCount;
	return true;
}

bool SL_TryGetAllocatedStringHashNoReport(
	const uint32_t stringValue,
	uint32_t* const outHash) noexcept
{
	if (!outHash)
		return false;
	RefString* refString = nullptr;
	uint32_t byteCount = 0;
	if (!SL_TryGetAllocatedStringByteCountNoReport(
			stringValue, &refString, &byteCount))
	{
		return false;
	}
	*outHash = GetHashCode(refString->str, byteCount);
	return true;
}

bool SL_IsAllocatedStringEntryValidNoReport(
	const uint32_t stringValue,
	const uint32_t expectedHash,
	const bool requireExpectedHash) noexcept
{
	uint32_t actualHash = 0;
	return SL_TryGetAllocatedStringHashNoReport(stringValue, &actualHash)
		&& (!requireExpectedHash || actualHash == expectedHash);
}

bool SL_IsFreeListHeadValidNoReport() noexcept
{
	if ((scrStringGlob.hashTable[0].status_next & HASH_STAT_MASK)
		!= HASH_STAT_FREE)
		return false;
	const uint32_t freeHead =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	if (freeHead == 0)
		return scrStringGlob.hashTable[0].u.prev == 0;
	if (freeHead >= STRINGLIST_SIZE
		|| (scrStringGlob.hashTable[freeHead].status_next
			& HASH_STAT_MASK) != HASH_STAT_FREE
		|| scrStringGlob.hashTable[freeHead].u.prev != 0)
	{
		return false;
	}
	const uint32_t nextFree = static_cast<uint16_t>(
		scrStringGlob.hashTable[freeHead].status_next);
	return nextFree < STRINGLIST_SIZE && nextFree != freeHead
		&& (scrStringGlob.hashTable[nextFree].status_next
			& HASH_STAT_MASK) == HASH_STAT_FREE
		&& scrStringGlob.hashTable[nextFree].u.prev == freeHead;
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
		if ((entry.status_next & HASH_STAT_MASK) != HASH_STAT_FREE)
			return false;
		const uint32_t nextIndex =
			static_cast<uint16_t>(entry.status_next);
		if (nextIndex >= STRINGLIST_SIZE || nextIndex == currentIndex
			|| (scrStringGlob.hashTable[nextIndex].status_next
				& HASH_STAT_MASK) != HASH_STAT_FREE
			|| scrStringGlob.hashTable[nextIndex].u.prev != currentIndex)
		{
			return false;
		}

		const uint32_t previousIndex = entry.u.prev;
		if (previousIndex == 0)
		{
			return static_cast<uint16_t>(
				scrStringGlob.hashTable[0].status_next) == currentIndex;
		}
		if (previousIndex >= STRINGLIST_SIZE
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

bool SL_IsInternHashStateValidNoReport(const uint32_t hash) noexcept
{
	if (hash == 0 || hash >= STRINGLIST_SIZE
		|| !SL_IsFreeListHeadValidNoReport())
		return false;

	const HashEntry &home = scrStringGlob.hashTable[hash];
	const uint32_t homeStatus = home.status_next & HASH_STAT_MASK;
	if (homeStatus == HASH_STAT_HEAD)
	{
		SL_ResetHashChainValidationNoReport();
		uint32_t entryIndex = hash;
		for (uint32_t visited = 0; visited < STRINGLIST_SIZE; ++visited)
		{
			const uint32_t visitedByte = entryIndex >> 3;
			const uint8_t visitedBit =
				static_cast<uint8_t>(1u << (entryIndex & 7u));
			if ((sl_hashChainVisited[visitedByte] & visitedBit) != 0)
				return false;
			sl_hashChainVisited[visitedByte] |= visitedBit;

			const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
			const uint32_t expectedStatus =
				entryIndex == hash ? HASH_STAT_HEAD : HASH_STAT_MOVABLE;
			if ((entry.status_next & HASH_STAT_MASK) != expectedStatus
				|| !SL_IsAllocatedStringEntryValidNoReport(
					entry.u.prev, hash, true)
				|| !SL_TryRecordStringIdNoReport(entry.u.prev))
				return false;

			const uint32_t nextIndex =
				static_cast<uint16_t>(entry.status_next);
			if (nextIndex == hash)
				return true;
			if (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
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
		if (!SL_TryGetAllocatedStringHashNoReport(
				home.u.prev, &owningHash)
			|| owningHash == hash || owningHash == 0
			|| owningHash >= STRINGLIST_SIZE
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
			const uint32_t visitedByte = entryIndex >> 3;
			const uint8_t visitedBit =
				static_cast<uint8_t>(1u << (entryIndex & 7u));
			if ((sl_hashChainVisited[visitedByte] & visitedBit) != 0)
				return false;
			sl_hashChainVisited[visitedByte] |= visitedBit;

			const HashEntry &entry = scrStringGlob.hashTable[entryIndex];
			const uint32_t expectedStatus =
				entryIndex == owningHash
				? HASH_STAT_HEAD
				: HASH_STAT_MOVABLE;
			if ((entry.status_next & HASH_STAT_MASK) != expectedStatus
				|| !SL_IsAllocatedStringEntryValidNoReport(
					entry.u.prev, owningHash, true)
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
			if (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE)
				return false;
			entryIndex = nextIndex;
		}
		return false;
	}

	if (homeStatus != HASH_STAT_FREE)
		return false;
	if (!SL_IsFreeEntryReachableNoReport(hash))
		return false;
	const uint32_t previousFree = home.u.prev;
	const uint32_t nextFree = static_cast<uint16_t>(home.status_next);
	return previousFree < STRINGLIST_SIZE && nextFree < STRINGLIST_SIZE
		&& (scrStringGlob.hashTable[previousFree].status_next
			& HASH_STAT_MASK) == HASH_STAT_FREE
		&& (scrStringGlob.hashTable[nextFree].status_next
			& HASH_STAT_MASK) == HASH_STAT_FREE
		&& static_cast<uint16_t>(
			scrStringGlob.hashTable[previousFree].status_next) == hash
		&& scrStringGlob.hashTable[nextFree].u.prev == hash;
}

bool SL_TryBuildUnlinkPlanNoReport(
	const uint32_t stringValue,
	RefString* const refString,
	const uint32_t byteCount,
	SL_UnlinkPlan* const outPlan) noexcept
{
	if (!refString || !outPlan || byteCount == 0
		|| byteCount > UINT32_C(65531))
		return false;

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
		const uint32_t visitedByte = entryIndex >> 3;
		const uint8_t visitedBit =
			static_cast<uint8_t>(1u << (entryIndex & 7u));
		if ((sl_hashChainVisited[visitedByte] & visitedBit) != 0)
			return false;
		sl_hashChainVisited[visitedByte] |= visitedBit;

		const HashEntry* const entry = &scrStringGlob.hashTable[entryIndex];
		const uint32_t expectedStatus =
			entryIndex == hash ? HASH_STAT_HEAD : HASH_STAT_MOVABLE;
		if ((entry->status_next & HASH_STAT_MASK) != expectedStatus
			|| !SL_IsAllocatedStringEntryValidNoReport(
				entry->u.prev, hash, true)
			|| !SL_TryRecordStringIdNoReport(entry->u.prev))
			return false;

		const uint32_t nextIndex =
			static_cast<uint16_t>(entry->status_next);
		if (nextIndex != hash
			&& (nextIndex == 0 || nextIndex >= STRINGLIST_SIZE
				|| (scrStringGlob.hashTable[nextIndex].status_next
					& HASH_STAT_MASK) != HASH_STAT_MOVABLE))
		{
			return false;
		}

		if (entry->u.prev == stringValue)
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

	if ((scrStringGlob.hashTable[0].status_next & HASH_STAT_MASK)
		!= HASH_STAT_FREE)
		return false;
	plan.freeHead =
		static_cast<uint16_t>(scrStringGlob.hashTable[0].status_next);
	if (plan.freeHead >= STRINGLIST_SIZE
		|| (scrStringGlob.hashTable[plan.freeHead].status_next
			& HASH_STAT_MASK) != HASH_STAT_FREE
		|| scrStringGlob.hashTable[plan.freeHead].u.prev != 0)
	{
		return false;
	}

	*outPlan = plan;
	return true;
}

SL_ResolveStatus SL_TryResolveLiveStringNoReport(
	const uint32_t stringValue,
	SL_LiveStringInfo* const outInfo) noexcept
{
	if (!outInfo || !script_string::IsCurrentRuntimeStringId(stringValue))
		return SL_ResolveStatus::NotAllocatedNoChange;

	MT_AllocationInfo allocationInfo{};
	const MT_AllocationInfoStatus allocationStatus =
		MT_TryGetAllocationInfo(stringValue, &allocationInfo);
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
	if (!SL_TryGetBoundedRefStringByteCount(
			info.refString,
			info.packed,
			allocationInfo.capacityBytes,
			&info.byteCount)
		|| info.byteCount > allocationInfo.capacityBytes
			- kRefStringHeaderSize
		|| !SL_IsExactStringAllocationNoReport(
			allocationInfo, info.byteCount)
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, info.packed)
		|| !SL_TryBuildUnlinkPlanNoReport(
			stringValue,
			info.refString,
			info.byteCount,
			&info.unlink))
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

bool SL_TryFreeResolvedStringNoReport(
	const uint32_t stringValue,
	const SL_LiveStringInfo &info) noexcept
{
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(info.refString));
	if (scr_string_atomic::RefCount(packed) != 0
		|| scr_string_atomic::User(packed) != 0
		|| !SL_IsDebugOwnershipExactNoReport(stringValue, packed)
		|| MT_TryFreeIndex(
			stringValue,
			static_cast<int>(info.byteCount + kRefStringHeaderSize))
			!= MT_FreeIndexStatus::Success)
	{
		return false;
	}

	const SL_UnlinkPlan &plan = info.unlink;
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
	return true;
}
} // namespace

namespace script_string
{
AcquireResult TryAcquireOrdinaryStringOfSize(
	const char* const bytes,
	const uint32_t byteCount,
	const int type) noexcept
{
	if (!bytes || byteCount == 0 || byteCount > UINT32_C(65531)
		|| bytes[byteCount - 1] != '\0'
		|| !SL_IsRepresentableRefStringBytesNoReport(bytes, byteCount))
	{
		return {AcquireStatus::InvalidArgumentNoChange, 0};
	}

	uint32_t stringId = 0;
	const SL_InternStatus status =
		SL_TryInternStringOfSize(bytes, 0, byteCount, type, &stringId);
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

TransferStatus TryTransferOrdinaryToDatabaseUser(
	const uint32_t stringId) noexcept
{
	if (!IsCurrentRuntimeStringId(stringId))
		return TransferStatus::OwnershipMismatchNoChange;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return TransferStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(stringId, &info);
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

ReleaseStatus TryRemoveOrdinaryReference(
	const uint32_t stringId) noexcept
{
	if (!IsCurrentRuntimeStringId(stringId))
		return ReleaseStatus::OwnershipMismatchNoChange;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(stringId, &info);
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
		|| SL_TryFreeResolvedStringNoReport(stringId, info);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return validFree
		? ReleaseStatus::Success
		: ReleaseStatus::UnsafeFailure;
}

ReleaseStatus TryRemoveDatabaseUserReference(
	const uint32_t stringId) noexcept
{
	if (!IsCurrentRuntimeStringId(stringId))
		return ReleaseStatus::OwnershipMismatchNoChange;

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	if (!scrStringGlob.inited)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return ReleaseStatus::OwnershipMismatchNoChange;
	}

	SL_LiveStringInfo info{};
	const SL_ResolveStatus resolveStatus =
		SL_TryResolveLiveStringNoReport(stringId, &info);
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
		|| SL_TryFreeResolvedStringNoReport(stringId, info);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return validFree
		? ReleaseStatus::Success
		: ReleaseStatus::UnsafeFailure;
}
} // namespace script_string

const char* __cdecl SL_DebugConvertToString(uint32_t stringValue)
{
	int len; // [esp+0h] [ebp-10h]
	int i; // [esp+8h] [ebp-8h]
	RefString* refString; // [esp+Ch] [ebp-4h]

	if (!stringValue)
		return "<NULL>";
	refString = GetRefString(stringValue);
	const uint8_t byteLength = scr_string_atomic::ByteLength(
		scr_string_atomic::Load(SL_RefStringWord(refString)));
	len = static_cast<uint8_t>(byteLength - 1);
	if (refString->str[len])
		return "<BINARY>";
	for (i = 0; i < len; ++i)
	{
		if (!isprint((uint8_t)refString->str[i]))
			return "<BINARY>";
	}
	return refString->str;
}

uint32_t SL_ConvertFromString(const char* str)
{
	iassert(str);
	RefString* refStr = GetRefString(str);
	return SL_ConvertFromRefString(refStr);
}

uint32_t SL_FindLowercaseString(const char* str)
{
	char stra[8196]; // [esp+5Ch] [ebp-2010h] BYREF
	uint32_t len; // [esp+2064h] [ebp-8h]
	signed int i; // [esp+2068h] [ebp-4h]

	PROF_SCOPED("SL_FindLowercaseString");
	len = strlen(str) + 1;
	if ((int)len <= 0x2000)
	{
		for (i = 0; i < (int)len; ++i)
			stra[i] = tolower(str[i]);
		return FindStringOfSize(stra, len);
	}
	else
	{
		return 0;
	}
}

void SL_RemoveRefToStringOfSize(uint32_t stringValue, uint32_t len)
{
	PROF_SCOPED("SL_RemoveRefToStringOfSize");

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString* refStr = GetRefString(stringValue);
	const uint32_t packed =
		scr_string_atomic::Load(SL_RefStringWord(refStr));
	if (scr_string_atomic::RefCount(packed)
		<= SL_UserReferenceCount(scr_string_atomic::User(packed)))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		iassert(scr_string_atomic::RefCount(packed)
			> SL_UserReferenceCount(scr_string_atomic::User(packed)));
		return;
	}

	const scr_string_atomic::RemoveRefResult result =
		scr_string_atomic::TryRemoveRef(SL_RefStringWord(refStr));
	if (!result.success)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		iassert(result.success);
		return;
	}

	// Account the old owner before a zero transition can return the memory-tree
	// slot to the allocator and let a new string reuse the same debug index.
	SL_DebugRemoveRef(stringValue);
	const bool validFree =
		!result.reachedZero || SL_FreeString(stringValue, refStr, len);
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	(void)validFree;
	iassert(validFree);
}

void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user)
{
	RefString *RefString; // eax

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
	RefString = GetRefString(stringValue);
	if (!SL_AddUserInternal(RefString, user))
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		Com_Error(ERR_DROP, "invalid script string reference increment");
		return;
	}
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
}

void __cdecl Scr_SetString(uint16_t *to, uint32_t from)
{
	if (from)
		SL_AddRefToString(from);
	if (*to)
		SL_RemoveRefToString(*to);
	*to = from;
}

uint32_t __cdecl SL_ConvertToLowercase(uint32_t stringValue, uint32_t user, int type)
{
	const char *v4; // [esp+4Ch] [ebp-2014h]
	char str[8192]; // [esp+50h] [ebp-2010h] BYREF
	uint32_t stringOfSize; // [esp+2054h] [ebp-Ch]
	uint32_t len; // [esp+2058h] [ebp-8h]
	uint32_t i; // [esp+205Ch] [ebp-4h]

	PROF_SCOPED("SL_ConvertToLowercase");

	len = SL_GetStringLen(stringValue) + 1;
	if (len <= 0x2000)
	{
		v4 = SL_ConvertToString(stringValue);
		for (i = 0; i < len; ++i)
			str[i] = tolower(v4[i]);
		stringOfSize = SL_GetStringOfSize(str, user, len, type);
		SL_RemoveRefToString(stringValue);
		return stringOfSize;
	}
	else
	{
		return stringValue;
	}
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
				c = *filename++;
			while (c == '\\');
		} while (c == '/');
		while (c >= ' ')
		{
			*newFilename++ = tolower(c);
			if (!--count)
				Com_Error(ERR_DROP, "Filename %s exceeds maximum length of %d", filename, oldCount);
			if (c == '/')
				break;
			c = *filename++;
			if (c == '\\')
				c = '/';
		}
	} while (c);
	*newFilename = 0;
}

uint32_t __cdecl Scr_CreateCanonicalFilename(const char *filename)
{
	char newFilename[1028]; // [esp+0h] [ebp-408h] BYREF

	CreateCanonicalFilename(newFilename, filename, 1024);
	return SL_GetString_(newFilename, 0, 7);
}

void Scr_SetStringFromCharString(uint16_t *to, const char *from)
{
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
	RefString *const refString = GetRefString(stringValue);
	return scr_string_atomic::User(
		scr_string_atomic::Load(SL_RefStringWord(refString)));
}

const char *SL_ConvertToStringSafe(uint32_t stringValue)
{
	if (!stringValue)
		return "(NULL)";

	if (scrStringDebugGlob)
	{
		iassert(!stringValue || SL_DebugRefCount(stringValue));
	}

	return GetRefString(stringValue)->str;
}
