#include "r_cinematic_paths.h"

#include <cstdio>
#include <cstring>

namespace
{
bool MoviePath(char *path, const char *root, const char *folder, const char *filename, char separator)
{
    const int length = std::snprintf(
        path, 256, "%s%c%s%cvideo%c%s.bik", root, separator, folder, separator, separator, filename);
    return length >= 0 && length < 256;
}
}

int R_CinematicMoviePaths(
    const char *homePath,
    const char *basePath,
    const char *filename,
    char separator,
    char (*paths)[256])
{
    int count = 0;
    if (homePath && *homePath && std::strcmp(homePath, basePath) != 0)
    {
        if (!MoviePath(paths[count++], homePath, "main", filename, separator))
            return 0;
    }
    if (!MoviePath(paths[count++], basePath, "main", filename, separator))
        return 0;
    if (!MoviePath(paths[count++], basePath, "raw", filename, separator))
        return 0;
    return count;
}
