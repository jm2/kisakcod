# ADR-0012: 0.1.0 release packages and install layout

Status: accepted · 2026-10-29 (owner)

## Decision

- **Targets:** Windows amd64/arm64, Linux amd64/arm64 and macOS arm64, each
  with client and server.
- **Artifacts:**
  - Windows: Inno Setup `-setup.exe` and a `.zip`.
  - Linux: `.deb`, `.rpm` and an Arch package, plus a `.tar.xz`.
  - macOS: a `.dmg` with `KisakCOD.app` (ADR-0011) and the server.
- **Contents:**
  - our binaries, README, LICENSE, generated `THIRD-PARTY-NOTICES`; no
    retail data, Steamworks, Miles or Bink.
  - Linux bundles a pinned runtime (SDL3, OpenAL Soft, Bink-only FFmpeg,
    dxvk-native) in `lib/`, as macOS does.
- **Game folder:** the install folder, used as `fs_basepath`.
  - Windows: the Steam CoD4 folder if found, else `{autopf}\KisakCOD`.
  - Linux: game files in `/opt/kisakcod`; launchers in `/usr/bin`; desktop
    entry and icon in `/usr/share`.
  - macOS: the read-only `.app`.
- **Per-user path:**
  - Paths: `~/.config/kisakcod` on Linux, `~/Library/Application
    Support/KisakCOD` on macOS, `%APPDATA%\KisakCOD` on Windows.
  - The client's default `fs_homepath`, even for a writable game folder:
    profiles, configs, logs, downloads.
  - It may also hold retail data, and is searched before the game folder.
  - The server keeps retail's `fs_homepath` = `fs_basepath`. Linux's
    `kisakcod-dedi` launcher sets it to the per-user path, because `/opt` is
    read-only.
  - A deliberate ADR-0009 exception.

## Done-test (NOW rows 70-74), per target
- install;
- add retail data to the per-user path;
- play LAN as a normal user; nothing is written to the game folder;
- uninstall, keeping data and state;
- repeat from the portable archive.
