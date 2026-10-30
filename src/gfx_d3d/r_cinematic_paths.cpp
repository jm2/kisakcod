#include "r_cinematic_paths.h"

#include <cstdio>
#include <cstring>

namespace
{
bool MoviePath(std::array<char, 256> &path, const char *root, const char *folder, const char *filename, char separator)
{
    const int length = std::snprintf(
        path.data(), path.size(), "%s%c%s%cvideo%c%s.bik", root, separator, folder, separator, separator, filename);
    return length >= 0 && length < (int)path.size();
}
}

int R_CinematicMoviePaths(
    const char *homePath,
    const char *basePath,
    const char *filename,
    char separator,
    CinematicMoviePaths &paths)
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
