#include "win_localize.h"
#include <qcommon/qcommon.h>
#include <universal/q_shared.h>
#include <universal/q_parse.h>
#include <stringed/stringed_hooks.h>
#include <qcommon/com_fileaccess.h>

#ifdef _WIN32
#include <Windows.h>
#endif

static LocalizationData localization;

#define LANGUAGE_BUF_SIZE 0x1000
static char language_buffer[LANGUAGE_BUF_SIZE];

char* __cdecl Win_CopyLocalizationString(const char* string)
{
	return va("%s", string);
}

char* __cdecl Win_GetLanguage()
{
	if (!localization.language)
		MyAssertHandler(".\\win32\\win_localize.cpp", 145, 0, "%s", "localization.language");
	return localization.language;
}

static bool Win_IsCommandLineSpace(char c)
{
    return c == ' ' || c == '\t';
}

// Whether c ends the token: the closing quote of a quoted token, else
// whitespace.
static bool Win_EndsCommandLineToken(char c, bool quoted)
{
    return quoted ? c == '"' : Win_IsCommandLineSpace(c);
}

// The next token of a command line, split as Cmd_TokenizeString splits it:
// whitespace separates tokens and quotes group them. Empty at the end.
static const char *Win_NextCommandLineToken(const char *line, char *token, int tokenSize)
{
    int length = 0;
    while (Win_IsCommandLineSpace(*line))
        ++line;
    const bool quoted = *line == '"';
    if (quoted)
        ++line;
    for (; *line && !Win_EndsCommandLineToken(*line, quoted); ++line)
    {
        if (length < tokenSize - 1)
            token[length++] = *line;
    }
    if (*line == '"')
        ++line;
    token[length] = 0;
    return line;
}

bool __cdecl Win_CommandLineBasePath(const char *commandLine, char *path, int pathSize)
{
    char line[1024];
    char token[MAX_OSPATH];
    bool found = false;

    // Com_ParseCommandLine starts a line at each '+' or newline, and
    // Com_StartupVariable applies "set" or "seta" (case-insensitive) to a
    // dvar named exactly fs_basepath; a later line wins.
    while (*commandLine)
    {
        int length = 0;
        while (*commandLine && *commandLine != '+' && *commandLine != '\n')
        {
            if (length < static_cast<int>(sizeof(line)) - 1)
                line[length++] = *commandLine;
            ++commandLine;
        }
        line[length] = 0;
        if (*commandLine)
            ++commandLine;

        const char *next = Win_NextCommandLineToken(line, token, sizeof(token));
        if (I_stricmp(token, "set") && I_stricmp(token, "seta"))
            continue;
        next = Win_NextCommandLineToken(next, token, sizeof(token));
        if (strcmp(token, "fs_basepath"))
            continue;
        Win_NextCommandLineToken(next, token, sizeof(token));
        I_strncpyz(path, token, pathSize);
        found = true;
    }
    return found;
}

// localization.txt from the working directory, as retail reads it, then from
// the command line's fs_basepath and the exe's folder (#982).
static FILE *Win_OpenLocalizationFile(const char *commandLine, const char *exeDir)
{
    FILE *fp = FS_FileOpenReadText("localization.txt");
    char dir[MAX_OSPATH];
    char path[MAX_OSPATH];
    if (!fp && commandLine && Win_CommandLineBasePath(commandLine, dir, sizeof(dir)) && dir[0])
    {
        Com_sprintf(path, sizeof(path), "%s/localization.txt", dir);
        fp = FS_FileOpenReadText(path);
    }
    if (!fp && exeDir && exeDir[0])
    {
        Com_sprintf(path, sizeof(path), "%s/localization.txt", exeDir);
        fp = FS_FileOpenReadText(path);
    }
    return fp;
}

int __cdecl Win_InitLocalization()
{
#ifdef _WIN32
    char exeDir[MAX_OSPATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    if (length && length < sizeof(exeDir))
    {
        if (char *slash = strrchr(exeDir, '\\'))
            *slash = 0;
    }
    else
    {
        exeDir[0] = 0;
    }
    return Win_InitLocalizationFrom(GetCommandLineA(), exeDir);
#else
    return Win_InitLocalizationFrom(nullptr, nullptr);
#endif
}

int __cdecl Win_InitLocalizationFrom(const char *commandLine, const char *exeDir)
{
    signed int size; // [esp+0h] [ebp-10h]
    int sizea; // [esp+0h] [ebp-10h]
    FILE* fp; // [esp+4h] [ebp-Ch]
    int i; // [esp+8h] [ebp-8h]
    int lang; // [esp+Ch] [ebp-4h] BYREF

    localization.language = 0;
    localization.strings = 0;
    fp = Win_OpenLocalizationFile(commandLine, exeDir);

    if (!fp)
    {
#ifdef KISAK_DEDI_HEADLESS
        // localization.txt sits in the install root and is read from the
        // working directory. A headless server started anywhere else has none;
        // like the POSIX server it then loads the English zones, instead of
        // looking for them under zone\(null)\.
        I_strncpyz(language_buffer, "english", LANGUAGE_BUF_SIZE);
        localization.language = language_buffer;
#else
        // The working directory need not be the install root (a shortcut, a
        // test harness or a preset build sets its own), and a null language
        // turns every zone path into zone\(null)\. Assume English, as the
        // headless build and the POSIX client do (#829).
        Com_PrintWarning(1, "localization.txt is not in the working directory, fs_basepath or the exe's folder; assuming English\n");
        I_strncpyz(language_buffer, "english", LANGUAGE_BUF_SIZE);
        localization.language = language_buffer;
#endif
        return 0;
    }

    size = FS_FileGetFileSize(fp);

    if (size >= LANGUAGE_BUF_SIZE)
        MyAssertHandler(".\\win32\\win_localize.cpp", 44, 0, "%s", "size < LANGUAGE_BUF_SIZE");

    localization.language = language_buffer;
    sizea = FS_FileRead(language_buffer, size, fp);
    FS_FileClose(fp);
    if (sizea)
    {
        localization.language[sizea] = 0;
        lang = 0;
        for (i = 0; localization.language[i]; ++i)
        {
            if (localization.language[i] == 10)
            {
                localization.language[i] = 0;
                localization.strings = &localization.language[i + 1];
                SEH_GetLanguageIndexForName(localization.language, &lang);
                return lang;
            }
        }
        return lang;
    }
    else
    {
        localization.language = 0;
        return 0;
    }
}

char* __cdecl Win_LocalizeRef(const char* ref)
{
    const char* v1; // eax
    const char* v3; // eax
    const char* strings; // [esp+14h] [ebp-Ch] BYREF
    int useRef; // [esp+18h] [ebp-8h]
    const char* token; // [esp+1Ch] [ebp-4h]

    Com_BeginParseSession("localization");
    strings = localization.strings;
    do
    {
        token = (const char*)Com_Parse(&strings);
        if (!*token)
        {
            Com_EndParseSession();
            if (!alwaysfails)
            {
                v1 = va("unlocalized: %s", ref);
                MyAssertHandler(".\\win32\\win_localize.cpp", 117, 0, v1);
            }
            return Win_CopyLocalizationString(ref);
        }
        useRef = strcmp(token, ref) == 0;
        token = (const char*)Com_Parse(&strings);
        if (!*token)
        {
            Com_EndParseSession();
            if (!alwaysfails)
            {
                v3 = va("missing value: %s", ref);
                MyAssertHandler(".\\win32\\win_localize.cpp", 126, 0, v3);
            }
            return Win_CopyLocalizationString(ref);
        }
    } while (!useRef);
    Com_EndParseSession();
    return Win_CopyLocalizationString(token);
}

void __cdecl Win_ShutdownLocalization()
{
    localization.language = 0;
    localization.strings = 0;
}