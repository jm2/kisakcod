#pragma once

// The files R_Cinematic_BinkOpen tries for a movie, in order: the home path's
// main/video first, as the file system searches fs_homepath before the game
// folder (ADR-0012), then the game folder's main/video and raw/video, which is
// all retail reads. The home path is skipped when it is empty or is the game
// folder. Returns the number of paths written, or 0 if one is longer than a
// path slot.
int R_CinematicMoviePaths(
    const char *homePath,
    const char *basePath,
    const char *filename,
    char separator,
    char (*paths)[256]);
