// R_CinematicMoviePaths (gfx_d3d/r_cinematic_paths.cpp): the files
// R_Cinematic_BinkOpen tries for a movie. With the game data only in the
// per-user home path (ADR-0012, row 70), the intro movies failed: the client
// looked only in the game folder. Here a movie exists only under a temporary
// home path, and opening the paths in order, as R_Cinematic_BinkOpen does,
// must find it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <gfx_d3d/r_cinematic_paths.h>

namespace
{
int g_failures = 0;

void Expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void ExpectPath(const char *got, const std::string &want, const char *what)
{
    if (want != got)
    {
        std::printf("FAIL: %s: got \"%s\", want \"%s\"\n", what, got, want.c_str());
        ++g_failures;
    }
}

// The first path that opens, as R_Cinematic_BinkOpen's loop takes it.
const char *FirstOpened(const CinematicMoviePaths &paths, int count)
{
    for (int index = 0; index < count; ++index)
    {
        if (std::ifstream(paths[index].data(), std::ios::binary))
            return paths[index].data();
    }
    return nullptr;
}
}

int main()
{
    namespace fs = std::filesystem;
    const char sep = (char)fs::path::preferred_separator;
    const fs::path root = fs::temp_directory_path() / "kisakcod-r-cinematic-paths";
    fs::remove_all(root);
    const fs::path home = root / "home";
    const fs::path base = root / "game";
    fs::create_directories(home / "main" / "video");
    fs::create_directories(base / "main" / "video");
    const fs::path movie = home / "main" / "video" / "atvi.bik";
    std::ofstream(movie, std::ios::binary) << "BIKi";

    CinematicMoviePaths paths{};
    const std::string homeText = home.string();
    const std::string baseText = base.string();
    int count = R_CinematicMoviePaths(homeText.c_str(), baseText.c_str(), "atvi", sep, paths);
    Expect(count == 3, "home, game main and game raw paths");
    const char *opened = FirstOpened(paths, count);
    Expect(opened != nullptr, "a movie only under the home path is found");
    if (opened)
        ExpectPath(opened, movie.string(), "the home path's movie opens");
    if (count == 3)
    {
        ExpectPath(paths[0].data(), (home / "main" / "video" / "atvi.bik").string(), "home path first");
        ExpectPath(paths[1].data(), (base / "main" / "video" / "atvi.bik").string(), "then the game folder's main");
        ExpectPath(paths[2].data(), (base / "raw" / "video" / "atvi.bik").string(), "then the game folder's raw");
    }

    count = R_CinematicMoviePaths("", "/game", "atvi", '/', paths);
    Expect(count == 2, "an empty home path is skipped");
    if (count == 2)
        ExpectPath(paths[0].data(), "/game/main/video/atvi.bik", "retail's path without a home path");

    count = R_CinematicMoviePaths("/game", "/game", "atvi", '/', paths);
    Expect(count == 2, "a home path that is the game folder is skipped");

    const std::string longName(250, 'x');
    Expect(R_CinematicMoviePaths("/home", "/game", longName.c_str(), '/', paths) == 0, "a path too long fails");

    fs::remove_all(root);
    if (g_failures)
        return 1;
    std::printf("r-cinematic-paths: all checks passed\n");
    return 0;
}
