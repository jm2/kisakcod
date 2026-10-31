#pragma once

// Whether a modal dialog can reach the player (posix_desktop.cpp). On macOS a
// dialog in a session that isn't on the console (SSH) or whose screen is
// locked waits for a click nobody can make, so a fatal error would hang
// instead of exiting. Elsewhere SDL fails fast when there is no display.
bool Posix_DialogCanShow();
