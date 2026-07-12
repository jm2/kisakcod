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
#include "scr_variable.h"

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
	iassert(!scrStringGlob.inited);

	MT_Init();

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);

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

	RefString* refStr = GetRefString(stringValue);
	if (!scr_string_atomic::TryAddRef(SL_RefStringWord(refStr)))
	{
		Com_Error(ERR_DROP, "invalid script string reference increment");
		return;
	}
	SL_DebugAddRef(stringValue);
	iassert(scr_string_atomic::RefCount(
		scr_string_atomic::Load(SL_RefStringWord(refStr))) != 0);
}

void SL_CheckExists(uint32_t stringValue)
{
	iassert(!scrStringDebugGlob || SL_DebugRefCount(stringValue));
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
	if (scrStringGlob.inited)
	{
		scrStringGlob.inited = 0;
		SL_CheckLeaks();
	}
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

uint32_t SL_GetStringOfSize(const char* str, uint32_t user, uint32_t len, int type)
{
	PROF_SCOPED("SL_GetStringOfSize");

	iassert(str);
	const uint8_t userByte = static_cast<uint8_t>(user);
	iassert(SL_IsValidUserMask(user, true));
	if (!SL_IsValidUserMask(user, true))
	{
		Com_Error(ERR_DROP, "script string user mask exceeds 8 bits");
		return 0;
	}

	uint32_t hash = GetHashCode(str, len);

	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);

	RefString* refStr = NULL;

	uint32_t stringValue = 0;

	uint32_t prev;
	uint32_t next;
	uint32_t newIndex;

	HashEntry *entry = &scrStringGlob.hashTable[hash];
	HashEntry *newEntry;

	if ((entry->status_next & HASH_STAT_MASK) == HASH_STAT_HEAD)
	{
		refStr = GetRefString(entry->u.prev);

		// Check if this string is already stored, if it matches the string at this particular hash lookup, and return existing entry if so.
		if (scr_string_atomic::ByteLength(
				scr_string_atomic::Load(SL_RefStringWord(refStr))) == static_cast<uint8_t>(len)
			&& !memcmp(refStr->str, str, len))
		{
			if (!SL_AddUserInternal(refStr, user))
			{
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				Com_Error(ERR_DROP, "invalid script string reference increment");
				return 0;
			}

			iassert((entry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);

			stringValue = entry->u.prev;

			iassert(refStr->str == SL_ConvertToString(stringValue));
			
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			return stringValue;
		}

		prev = hash;
		newIndex = (uint16_t)entry->status_next;

		for (newEntry = &scrStringGlob.hashTable[newIndex]; newEntry != entry; newEntry = &scrStringGlob.hashTable[newIndex])
		{
			iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_MOVABLE);

			refStr = GetRefString(newEntry->u.prev);

			if (scr_string_atomic::ByteLength(
					scr_string_atomic::Load(SL_RefStringWord(refStr))) == static_cast<uint8_t>(len)
				&& !memcmp(refStr->str, str, len))
			{
				scrStringGlob.hashTable[prev].status_next = (uint16_t)newEntry->status_next | scrStringGlob.hashTable[prev].status_next & HASH_STAT_MASK;
				newEntry->status_next = (uint16_t)entry->status_next | newEntry->status_next & HASH_STAT_MASK;
				entry->status_next = newIndex | entry->status_next & HASH_STAT_MASK;
				stringValue = newEntry->u.prev;
				newEntry->u.prev = entry->u.prev;
				entry->u.prev = stringValue;
				if (!SL_AddUserInternal(refStr, user))
				{
					Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
					Com_Error(ERR_DROP, "invalid script string reference increment");
					return 0;
				}

				iassert((newEntry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
				iassert((entry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
				iassert(refStr->str == SL_ConvertToString(stringValue));

				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				return stringValue;
			}
			prev = newIndex;
			newIndex = (uint16_t)newEntry->status_next;
		} //for()

		newIndex = scrStringGlob.hashTable[0].status_next;

		if (!newIndex)
		{
			// KISAKTODO?
			//Scr_DumpScriptThreads();
			//Scr_DumpScriptVariablesDefault();
			Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
			Com_Error(ERR_DROP, "exceeded maximum number of script strings (increase STRINGLIST_SIZE)");
			return 0;
		}

		stringValue = MT_AllocIndex(len + 4, type);
		newEntry = &scrStringGlob.hashTable[newIndex];
		iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_FREE);

		uint32_t newNext = (uint16_t)newEntry->status_next;

		scrStringGlob.hashTable[0].status_next = newNext;
		scrStringGlob.hashTable[newNext].u.prev = 0;
		newEntry->status_next = (uint16_t)entry->status_next | HASH_STAT_MOVABLE;
		entry->status_next = (uint16_t)newIndex | entry->status_next & HASH_STAT_MASK;
		newEntry->u.prev = entry->u.prev;
	}
	else
	{
		if ((scrStringGlob.hashTable[hash].status_next & HASH_STAT_MASK) != 0)
		{
			iassert((entry->status_next & HASH_STAT_MASK) == HASH_STAT_MOVABLE);
			
			next = (uint16_t)entry->status_next;

			for (prev = next;
				(uint16_t)scrStringGlob.hashTable[prev].status_next != hash;
				prev = (uint16_t)scrStringGlob.hashTable[prev].status_next)
			{
				;
			}

			iassert(prev);

			newIndex = scrStringGlob.hashTable[0].status_next;

			if (!newIndex)
			{
				// KISAKTODO?
				//Scr_DumpScriptThreads();
				//Scr_DumpScriptVariablesDefault();
				Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
				Com_Error(ERR_DROP, "exceeded maximum number of script strings");
				return 0;
			}

			stringValue = MT_AllocIndex(len + 4, type);
			newEntry = &scrStringGlob.hashTable[newIndex];

			iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_FREE);

			uint32_t newNext = (uint16_t)newEntry->status_next;

			scrStringGlob.hashTable[0].status_next = newNext;
			scrStringGlob.hashTable[newNext].u.prev = 0;
			scrStringGlob.hashTable[prev].status_next = newIndex | scrStringGlob.hashTable[prev].status_next & HASH_STAT_MASK;
			newEntry->status_next = next | HASH_STAT_MOVABLE;
			newEntry->u.prev = entry->u.prev;
		}
		else
		{
			stringValue = MT_AllocIndex(len + 4, type);
			prev = entry->u.prev;
			next = (uint16_t)entry->status_next;

			scrStringGlob.hashTable[prev].status_next = next | scrStringGlob.hashTable[prev].status_next & HASH_STAT_MASK;
			scrStringGlob.hashTable[next].u.prev = prev;
		}
		iassert(!(hash & HASH_STAT_MASK));
		entry->status_next = hash | HASH_STAT_HEAD;
	}
	iassert(stringValue);
	entry->u.prev = stringValue;

	refStr = GetRefString(stringValue);
	memcpy((uint8_t*)refStr->str, (uint8_t*)str, len);
	Sys_AtomicStore(
		SL_RefStringWord(refStr),
		scr_string_atomic::Pack(1, userByte, static_cast<uint8_t>(len)));
	iassert(scr_string_atomic::User(
		scr_string_atomic::Load(SL_RefStringWord(refStr))) == userByte);
	SL_DebugAddRef(stringValue);

	iassert((entry->status_next & HASH_STAT_MASK) != HASH_STAT_FREE);
	iassert(refStr->str == SL_ConvertToString(stringValue));

//END_CLEANUP:
	Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
	return stringValue;
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

	HashEntry *entry = &scrStringGlob.hashTable[hash];

	if ((entry->status_next & HASH_STAT_MASK) != HASH_STAT_HEAD)
	{
		Sys_LeaveCriticalSection(CRITSECT_SCRIPT_STRING);
		return 0;
	}

	RefString* refStr = GetRefString(entry->u.prev);

	if (scr_string_atomic::ByteLength(
			scr_string_atomic::Load(SL_RefStringWord(refStr))) != static_cast<uint8_t>(len)
		|| memcmp(refStr->str, str, len))
	{
		uint32_t prev = hash;
		uint32_t newIndex = (uint16_t)entry->status_next;

		for (HashEntry* newEntry = &scrStringGlob.hashTable[newIndex];
			newEntry != entry;
			newEntry = &scrStringGlob.hashTable[newIndex])
		{
			iassert((newEntry->status_next & HASH_STAT_MASK) == HASH_STAT_MOVABLE);
			refStr = GetRefString(newEntry->u.prev);

			if (scr_string_atomic::ByteLength(
					scr_string_atomic::Load(SL_RefStringWord(refStr))) == static_cast<uint8_t>(len)
				&& !memcmp(refStr->str, str, len))
			{
				scrStringGlob.hashTable[prev].status_next = (uint16_t)newEntry->status_next | scrStringGlob.hashTable[prev].status_next & HASH_STAT_MASK;
				newEntry->status_next = (uint16_t)entry->status_next | newEntry->status_next & HASH_STAT_MASK;
				entry->status_next = newIndex | entry->status_next & HASH_STAT_MASK;
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

	RefString *const refStr = GetRefString(stringValue);
	const scr_string_atomic::TransferRefToUserResult result =
		scr_string_atomic::TransferRefToUser(
			SL_RefStringWord(refStr), userByte);
	if (result == scr_string_atomic::TransferRefToUserResult::Invalid)
	{
		Com_Error(ERR_DROP, "invalid script string reference transfer");
		return;
	}
	if (result == scr_string_atomic::TransferRefToUserResult::ReleasedDuplicate)
		SL_DebugRemoveRef(stringValue);
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

	RefString* refStr = GetRefString(stringValue);
	const scr_string_atomic::RemoveRefAttempt attempt =
		scr_string_atomic::TryRemoveRefUnlessLast(SL_RefStringWord(refStr));
	if (attempt == scr_string_atomic::RemoveRefAttempt::Removed)
	{
		SL_DebugRemoveRef(stringValue);
		return;
	}
	if (attempt == scr_string_atomic::RemoveRefAttempt::Invalid)
	{
		iassert(attempt != scr_string_atomic::RemoveRefAttempt::Invalid);
		return;
	}

	// Serialize the last decrement with hash lookup/unlink. Once zero is
	// published, interning cannot observe the entry until it has been removed.
	Sys_EnterCriticalSection(CRITSECT_SCRIPT_STRING);
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
	iassert(validFree);
}

void __cdecl SL_AddUser(uint32_t stringValue, uint32_t user)
{
	RefString *RefString; // eax

	RefString = GetRefString(stringValue);
	if (!SL_AddUserInternal(RefString, user))
		Com_Error(ERR_DROP, "invalid script string reference increment");
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
