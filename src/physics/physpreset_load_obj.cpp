#include "phys_local.h"
#include <universal/com_memory.h>
#include <qcommon/qcommon.h>
#include <universal/com_files.h>

void *(__cdecl *physAlloc)(int);

// Parse-side view of the physics preset fields (LWSS). The field table below
// derives every offset from this struct, so it tracks the real in-memory
// layout on every target: the old hard-coded 28/32/36 offsets were the ILP32
// ones, and at 64-bit piecesSpreadFraction landed on the upper half of
// sndAliasPrefix and shifted every later field.
struct PhysPresetLite
{
    float mass;
    float bounce;
    float friction;
    int isFrictionInfinity;
    float bulletForceScale;
    float explosiveForceScale;
    const char *sndAliasPrefix;
    float piecesSpreadFraction;
    float piecesUpwardVelocity;
    bool tempDefaultToCylinder;
};

cspField_t physPresetFields[10] =
{
  { "mass", offsetof(PhysPresetLite, mass), 6 },
  { "bounce", offsetof(PhysPresetLite, bounce), 6 },
  { "friction", offsetof(PhysPresetLite, friction), 6 },
  { "isFrictionInfinity", offsetof(PhysPresetLite, isFrictionInfinity), 5 },
  { "bulletForceScale", offsetof(PhysPresetLite, bulletForceScale), 6 },
  { "explosiveForceScale", offsetof(PhysPresetLite, explosiveForceScale), 6 },
  { "sndAliasPrefix", offsetof(PhysPresetLite, sndAliasPrefix), 0 },
  { "piecesSpreadFraction", offsetof(PhysPresetLite, piecesSpreadFraction), 6 },
  { "piecesUpwardVelocity", offsetof(PhysPresetLite, piecesUpwardVelocity), 6 },
  { "tempDefaultToCylinder", offsetof(PhysPresetLite, tempDefaultToCylinder), 5 }
}; // idb

void __cdecl PhysPreset_Strcpy(uint8_t *member, const char *keyValue)
{
    char v2; // [esp+3h] [ebp-25h]
    char *v3; // [esp+8h] [ebp-20h]
    const char *v4; // [esp+Ch] [ebp-1Ch]
    char *buf; // [esp+20h] [ebp-8h]
    // The target member is a `const char *` field of PhysPresetLite; store the
    // pointer at its native width. The old `*(_DWORD *)member = (_DWORD)buf`
    // truncated it to 32 bits and was a hard error on 64-bit clang/GCC.
    const char **slot = reinterpret_cast<const char **>(member);

    if (*keyValue)
    {
        // strnlen with the parser token bound instead of strlen (CWE-126 /
        // unbounded-string-scan findings). keyValue comes from Info_ValueForKey,
        // which NUL-terminates its value1[][][8192] buffer, and the *keyValue
        // read above has the same termination precondition; for every such
        // input strnlen equals strlen, so the allocation and the copy below are
        // unchanged.
        buf = static_cast<char *>(physAlloc(static_cast<int>(strnlen(keyValue, 8192) + 1)));
        v4 = keyValue;
        v3 = buf;
        do
        {
            v2 = *v4;
            *v3++ = *v4++;
        } while (v2);
        *slot = buf;
    }
    else
    {
        *slot = "";
    }
}

PhysPreset *__cdecl PhysPresetLoadFile(const char *name, void *(__cdecl *Alloc)(int))
{
    char dest[64]; // [esp+24h] [ebp-2080h] BYREF
    char buffer[8192]; // [esp+64h] [ebp-2040h] BYREF
    PhysPresetLite pStruct;
    PhysPreset *physPreset; // [esp+2090h] [ebp-14h]
    char *last; // [esp+2094h] [ebp-10h]
    signed int filelen; // [esp+2098h] [ebp-Ch]
    int f; // [esp+209Ch] [ebp-8h] BYREF
    int len; // [esp+20A0h] [ebp-4h]

    last = (char*)"PHYSIC";
    len = strlen("PHYSIC");
    if (!strlen(name))
        return 0;
    if (Com_sprintf(dest, 0x40u, "physic/%s", name) >= 0)
    {
        filelen = FS_FOpenFileByMode(dest, &f, FS_READ);
        if (filelen >= 0)
        {
            FS_Read((uint8_t *)buffer, len, f);
            buffer[len] = 0;
            if (!strncmp(buffer, last, len))
            {
                if (filelen - len < 0x2000)
                {
                    FS_Read((uint8_t *)buffer, filelen - len, f);
                    buffer[filelen - len] = 0;
                    FS_FCloseFile(f);
                    if (Info_Validate(buffer))
                    {
                        memset(&pStruct, 0, sizeof(pStruct));
                        pStruct.sndAliasPrefix = "";
                        physAlloc = Alloc;
                        if (ParseConfigStringToStruct((unsigned char*)&pStruct, physPresetFields, 10, buffer, 0, 0, PhysPreset_Strcpy))
                        {
                            iassert(sizeof(PhysPreset) == 44);

                            physPreset = (PhysPreset *)Alloc(sizeof(PhysPreset));

                            iassert(physPreset);

                            physPreset->mass = pStruct.mass;
                            physPreset->bounce = pStruct.bounce;

                            if (pStruct.isFrictionInfinity)
                                physPreset->friction = FLT_MAX;
                            else
                                physPreset->friction = pStruct.friction;

                            physPreset->bulletForceScale = pStruct.bulletForceScale;
                            physPreset->explosiveForceScale = pStruct.explosiveForceScale;
                            physPreset->sndAliasPrefix = pStruct.sndAliasPrefix;
                            physPreset->piecesSpreadFraction = pStruct.piecesSpreadFraction;
                            physPreset->piecesUpwardVelocity = pStruct.piecesUpwardVelocity;
                            physPreset->tempDefaultToCylinder = pStruct.tempDefaultToCylinder;
                            return physPreset;
                        }
                        else
                        {
                            return 0;
                        }
                    }
                    else
                    {
                        Com_PrintError(20, "ERROR: physics preset file [%s] is not valid\n", name);
                        return 0;
                    }
                }
                else
                {
                    Com_PrintError(20, "ERROR: physics preset file [%s] is to big\n", name);
                    FS_FCloseFile(f);
                    return 0;
                }
            }
            else
            {
                Com_PrintError(20, "ERROR: file [%s] is not a physics preset file\n", name);
                FS_FCloseFile(f);
                return 0;
            }
        }
        else
        {
            Com_PrintError(20, "ERROR: physics preset '%s' not found\n", name);
            return 0;
        }
    }
    else
    {
        Com_PrintError(20, "ERROR: filename '%s' too long\n", dest);
        return 0;
    }
}

PhysPreset *__cdecl PhysPresetPrecache(const char *name, void *(__cdecl *Alloc)(int))
{
    PhysPreset *physPreset; // [esp+0h] [ebp-4h]
    PhysPreset *physPreseta; // [esp+0h] [ebp-4h]

    if (!name)
        MyAssertHandler(".\\physics\\physpreset_load_obj.cpp", 146, 0, "%s", "name");
    if (!*name)
        MyAssertHandler(".\\physics\\physpreset_load_obj.cpp", 147, 0, "%s", "name[0]");
    physPreset = (PhysPreset *)Hunk_FindDataForFile(7, name);
    if (physPreset)
        return physPreset;
    physPreseta = PhysPresetLoadFile(name, Alloc);
    if (physPreseta)
    {
        physPreseta->name = Hunk_SetDataForFile(7, name, physPreseta, Alloc);
        return physPreseta;
    }
    else
    {
        Com_PrintError(20, "ERROR: Cannot find physics preset '%s'.\n", name);
        return 0;
    }
}


