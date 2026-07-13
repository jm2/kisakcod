#include <win32/win_local.h>

#include <qcommon/qcommon.h>
#include <qcommon/sys_filesystem.h>

#include <direct.h>
#include <io.h>
#include <string>
#include <vector>
#include "com_memory.h"

// *(_DWORD *)(*(_DWORD *)(*((_DWORD *)NtCurrentTeb()->ThreadLocalStoragePointer + _tls_index) + 4)

void __cdecl Sys_Mkdir(const char *path)
{
    (void)Sys_FileSystemCreateDirectory(path);
}

BOOL __cdecl Sys_RemoveDirTree(const char *path)
{
    if (!path || !path[0])
        return 0;

    bool v2; // [esp+8h] [ebp-250h]
    int handle; // [esp+1Ch] [ebp-23Ch]
    char childPath[256]; // [esp+20h] [ebp-238h] BYREF
    _finddata64i32_t find; // [esp+120h] [ebp-138h] BYREF
    bool hasError; // [esp+252h] [ebp-6h]
    bool hasTrailingSeparater; // [esp+253h] [ebp-5h]
    int length; // [esp+254h] [ebp-4h]

    length = strlen(path);
    v2 = path[length - 1] == 92 || path[length - 1] == 47;
    hasTrailingSeparater = v2;
    if (v2)
        Com_sprintf(childPath, 0x100u, "%s*", path);
    else
        Com_sprintf(childPath, 0x100u, "%s\\*", path);
    handle = _findfirst64i32(childPath, &find);
    if (handle == -1)
        return _rmdir(path) != -1;
    hasError = 0;
    do
    {
        if (find.name[0] != 46 || find.name[1] && (find.name[1] != 46 || find.name[2]))
        {
            if (hasTrailingSeparater)
                Com_sprintf(childPath, 0x100u, "%s%s", path, find.name);
            else
                Com_sprintf(childPath, 0x100u, "%s\\%s", path, find.name);
            if ((find.attrib & 0x10) != 0)
                hasError = !Sys_RemoveDirTree(childPath);
            else
                hasError = remove(childPath) == -1;
        }
    } while (!hasError && _findnext64i32(handle, &find) != -1);
    _findclose(handle);
    return !hasError && _rmdir(path) != -1;
}

void __cdecl Sys_ListFilteredFiles(
    HunkUser *user,
    const char *basedir,
    const char *subdirs,
    const char *filter,
    char **list,
    int *numfiles)
{
    char filename[256]; // [esp+10h] [ebp-338h] BYREF
    _finddata64i32_t findinfo; // [esp+110h] [ebp-238h] BYREF
    int findhandle; // [esp+23Ch] [ebp-10Ch]
    char search[260]; // [esp+240h] [ebp-108h] BYREF

    if (*numfiles < 0x1FFF)
    {
        if (strlen(subdirs))
            Com_sprintf(search, 0x100u, "%s\\%s\\*", basedir, subdirs);
        else
            Com_sprintf(search, 0x100u, "%s\\*", basedir);
        findhandle = _findfirst64i32(search, &findinfo);
        if (findhandle != -1)
        {
            do
            {
                if ((findinfo.attrib & 0x10) == 0
                    || I_stricmp(findinfo.name, ".") && I_stricmp(findinfo.name, "..") && I_stricmp(findinfo.name, "CVS"))
                {
                    if (*numfiles >= 0x1FFF)
                        break;
                    if (subdirs)
                        Com_sprintf(filename, 0x100u, "%s\\%s", subdirs, findinfo.name);
                    else
                        Com_sprintf(filename, 0x100u, "%s", findinfo.name);
                    if (Com_FilterPath(filter, filename, 0))
                        list[(*numfiles)++] = Hunk_CopyString(user, filename);
                }
            } while (_findnext64i32(findhandle, &findinfo) != -1);
            _findclose(findhandle);
        }
    }
}

BOOL __cdecl HasFileExtension(const char *name, const char *extension)
{
    char search[260]; // [esp+0h] [ebp-108h] BYREF

    Com_sprintf(search, 0x100u, "*.%s", extension);
    return I_stricmpwild(search, name) == 0;
}

int __cdecl Sys_CountFileList(char **list)
{
    int i; // [esp+0h] [ebp-4h]

    i = 0;
    if (list)
    {
        while (*list)
        {
            ++list;
            ++i;
        }
    }
    return i;
}

char **__cdecl Sys_ListFiles(
    const char *directory,
    const char *extension,
    const char *filter,
    int *numfiles,
    int wantsubs)
{
    char *v6; // eax
    char **v7; // [esp+4h] [ebp-264h]
    _finddata64i32_t findinfo; // [esp+18h] [ebp-250h] BYREF
    int flag; // [esp+140h] [ebp-128h]
    char **listCopy; // [esp+144h] [ebp-124h]
    int findhandle; // [esp+148h] [ebp-120h]
    char *(*list)[8192]; // [esp+14Ch] [ebp-11Ch]
    int nfiles; // [esp+150h] [ebp-118h] BYREF
    HunkUser *user; // [esp+154h] [ebp-114h]
    char search[256]; // [esp+160h] [ebp-108h] BYREF
    int i; // [esp+264h] [ebp-4h]

    LargeLocal list_large_local(0x8000); // [esp+158h] [ebp-110h] BYREF
    //LargeLocal::LargeLocal(&list_large_local, 0x8000);
    //list = (char *(*)[8192])LargeLocal::GetBuf(&list_large_local);
    list = (char *(*)[8192])list_large_local.GetBuf();
    if (filter)
    {
        user = Hunk_UserCreate(0x20000, "Sys_ListFiles", 0, 0, 3);
        nfiles = 0;
        Sys_ListFilteredFiles(user, directory, "", filter, (char **)list, &nfiles);
        (*list)[nfiles] = 0;
        *numfiles = nfiles;
        if (nfiles)
        {
            listCopy = (char **)Hunk_UserAlloc(user, 4 * nfiles + 8, 4);
            *listCopy++ = (char *)user;
            for (i = 0; i < nfiles; ++i)
                listCopy[i] = (*list)[i];
            listCopy[i] = 0;
            //LargeLocal::~LargeLocal(&list_large_local);
            return listCopy;
        }
        else
        {
            Hunk_UserDestroy(user);
            //LargeLocal::~LargeLocal(&list_large_local);
            return 0;
        }
    }
    else
    {
        if (!extension)
            extension = "";
        if (*extension != 47 || extension[1])
        {
            flag = 16;
        }
        else
        {
            extension = "";
            flag = 0;
        }
        if (*extension)
            Com_sprintf(search, 0x100u, "%s\\*.%s", directory, extension);
        else
            Com_sprintf(search, 0x100u, "%s\\*", directory);
        nfiles = 0;
        findhandle = _findfirst64i32(search, &findinfo);
        if (findhandle == -1)
        {
            *numfiles = 0;
            //LargeLocal::~LargeLocal(&list_large_local);
            return 0;
        }
        else
        {
            user = Hunk_UserCreate(0x20000, "Sys_ListFiles", 0, 0, 3);
            do
            {
                if ((!wantsubs && flag != (findinfo.attrib & 0x10) || wantsubs && (findinfo.attrib & 0x10) != 0)
                    && ((findinfo.attrib & 0x10) == 0
                        || I_stricmp(findinfo.name, ".") && I_stricmp(findinfo.name, "..") && I_stricmp(findinfo.name, "CVS"))
                    && (!*extension || HasFileExtension(findinfo.name, extension)))
                {
                    v6 = Hunk_CopyString(user, findinfo.name);
                    (*list)[nfiles++] = v6;
                    if (nfiles == 0x1FFF)
                        break;
                }
            } while (_findnext64i32(findhandle, &findinfo) != -1);
            (*list)[nfiles] = 0;
            _findclose(findhandle);
            *numfiles = nfiles;
            if (nfiles)
            {
                listCopy = (char **)Hunk_UserAlloc(user, 4 * nfiles + 8, 4);
                *listCopy++ = (char *)user;
                for (i = 0; i < nfiles; ++i)
                    listCopy[i] = (*list)[i];
                listCopy[i] = 0;
                v7 = listCopy;
                //LargeLocal::~LargeLocal(&list_large_local);
                return v7;
            }
            else
            {
                Hunk_UserDestroy(user);
                //LargeLocal::~LargeLocal(&list_large_local);
                return 0;
            }
        }
    }
}


namespace
{
using PathQuery = bool (KISAK_CDECL *)(char *, std::size_t);

bool ReadDynamicPath(const PathQuery query, std::vector<char> *const output)
{
    if (!query || !output)
        return false;
    for (std::size_t capacity = 256; capacity <= 1024 * 1024; capacity *= 2)
    {
        output->assign(capacity, '\0');
        if (query(output->data(), output->size()))
        {
            output->resize(strlen(output->data()) + 1);
            return true;
        }
    }
    output->assign(1, '\0');
    return false;
}

}

char *__cdecl Sys_Cwd()
{
    thread_local std::vector<char> cwd(1, '\0');
    (void)ReadDynamicPath(Sys_FileSystemGetCurrentDirectory, &cwd);
    return cwd.data();
}

const char *__cdecl Sys_DefaultCDPath()
{
    return "";
}

char *__cdecl Sys_DefaultInstallPath()
{
    static std::vector<char> exePath = [] {
        std::vector<char> path;
        if (IsDebuggerPresent())
        {
            if (!ReadDynamicPath(Sys_FileSystemGetCurrentDirectory, &path))
                path.assign(1, '\0');
        }
        else
        {
            if (!ReadDynamicPath(Sys_FileSystemGetExecutablePath, &path))
                path.assign(1, '\0');
            const std::string fullPath(path.data());
            const std::size_t parentLength =
                Sys_FileSystemParentPathLength(fullPath.c_str());
            path.assign(fullPath.begin(), fullPath.begin() + parentLength);
            path.push_back('\0');
        }
        return path;
    }();
    return exePath.data();
}
