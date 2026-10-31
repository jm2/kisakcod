#pragma once

struct LocalizationData // sizeof=0x8
{                                       // ...
    char* language;                     // ...
    char* strings;                      // ...
};

char* __cdecl Win_CopyLocalizationString(const char* string);
char* __cdecl Win_GetLanguage();
int __cdecl Win_InitLocalization();
// Win_InitLocalization's work: localization.txt from the working directory,
// else under the command line's "+set fs_basepath", else in exeDir. Either
// may be null.
int __cdecl Win_InitLocalizationFrom(const char* commandLine, const char* exeDir);
// The path the command line's last "+set fs_basepath <path>" sets, as
// Com_StartupVariable will apply it; false if it sets none.
bool __cdecl Win_CommandLineBasePath(const char* commandLine, char* path, int pathSize);
char* __cdecl Win_LocalizeRef(const char* ref);
void __cdecl Win_ShutdownLocalization();