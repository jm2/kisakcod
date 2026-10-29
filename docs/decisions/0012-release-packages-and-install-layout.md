# ADR-0012: 0.1.0 release packages and install layout

Status: accepted
Date: 2026-10-29

## Context

On 2026-10-29 the owner ordered the 0.1.0 packaging. The 0.1.0 packaging
follows jm2/tributary and jm2/balun: each target gets installers and a
portable archive, and macOS gets a `.dmg` with an `.app`. Linux distro
packages use `/opt` as the prefix, with `/opt/kisakcod` as the core game
folder.

Retail derives both `fs_basepath` and `fs_homepath` from the working
directory, and writes profiles under `fs_basepath/players`, so a game folder
the player can't write to (`/opt`, `Program Files`, a signed `.app`) breaks
it.

## Decision

**Targets.** The five release targets, each shipping the MP client and the
headless dedicated server together: Windows amd64 and arm64, Linux amd64
and arm64, macOS arm64. Windows x86 and single-player are not release
targets (CHARTER).

**Artifacts per target.**

| Target | Installer | Portable |
| --- | --- | --- |
| Windows amd64, arm64 | Inno Setup `-setup.exe` (balun's `build-aux/inno` shape) | `.zip` |
| Linux amd64, arm64 | `.deb`, `.rpm` and an Arch `.pkg.tar.zst`, all into `/opt/kisakcod` | `.tar.xz` (keeps exec bits) |
| macOS arm64 | `.dmg` holding `KisakCOD.app` (client, [ADR-0011](0011-macos-client-bundles-its-runtime.md)) and the dedicated server | the same `.dmg` |

Names are `kisakcod-<version>-<os>-<arch>[-setup].<ext>`. The release adds
`SHA256SUMS`, as the headless legs do today.

**Contents.** Packages hold only binaries we build, a README, our license,
and every bundled component's license text in `THIRD-PARTY-NOTICES` (#263).
They contain no retail data, no Steamworks, and no Miles or Bink binaries.

- Linux builds its runtime from pinned sources, as macOS does (ADR-0011):
  SDL3, OpenAL Soft, a Bink-only LGPL FFmpeg and dxvk-native, from
  `scripts/platform/linux/runtime`. Every Linux format carries the runtime in
  the game folder's `lib/`, found through the client's `$ORIGIN/lib` runpath.
  A package then depends only on glibc, libstdc++ and the Vulkan loader, one
  build serves `.deb`, `.rpm`, Arch and the portable archive, and the notices
  cover everything shipped.
- Windows bundles OpenAL Soft and FFmpeg beside the executables.

**Game folder.** The install folder is the game folder, and `fs_basepath`
points at it. The player's Steam 1.8 data (`main/`, `zone/` and the rest) can
live there, or in the per-user path below. The file system searches both, the
per-user path first, so a read-only install never needs data copied into it.

- **Windows:** the installer defaults to the Steam CoD4 folder it finds in
  the registry, so it installs beside `iw3mp.exe`. Otherwise it uses
  `{autopf}\KisakCOD`.
- **Linux:** `/opt/kisakcod`. `/usr/bin/kisakcod` and `/usr/bin/kisakcod-dedi`
  are launchers that set the paths. `/opt` is read-only, so the server
  launcher also points the server's `fs_homepath` at the per-user path, which
  holds its `server.cfg` and logs. A desktop entry and an icon are
  included.
- **macOS:** the signed `.app` stays read-only, so the data goes in the
  per-user path.

**Per-user path.** A writable secondary path holds retail data if the
player puts it there, plus profiles, `config_mp.cfg`, logs and downloads.
It is `$XDG_CONFIG_HOME/kisakcod` (default `~/.config/kisakcod`) on Linux,
at the owner's choice on 2026-10-29. On macOS it is
`~/Library/Application Support/KisakCOD`, and on Windows
`%APPDATA%\KisakCOD`. On every platform the client's `fs_homepath`
defaults to it, and user configs and settings always go there, even when the
game folder is writable. The owner chose this on 2026-10-29.

- Profiles move from `fs_basepath/players` to `fs_homepath/players`.
- A profile already in the game folder is still read.
- The dedicated server keeps retail's game-folder default, so `server.cfg`
  and its logs stay where operators expect them.

This deviates from retail on purpose, at the owner's request (an exception to
ADR-0009): retail's layout can't run from a read-only game folder.
An explicit `+set fs_homepath` still wins.
The file system already searches `fs_homepath/main`. Fast files under `zone/`
load only from `fs_basepath` today (`DB_GetFastFileBasePath`), so the zone
lookup tries the per-user path first, then the game folder.

## Consequences

- NOW rows 70-74 carry the work:
  - 70: the engine paths;
  - 71: Windows;
  - 72: Linux;
  - 73: macOS, which extends row 66;
  - 74: the release workflow, with client legs, installers and checksums.
- `release.yml` builds client legs as well as headless ones. The manifest
  client cells it reads must reach `links` or above.
- Done-test for each target:
  - install from the installer;
  - add the Steam 1.8 data to the per-user path, and separately to the game
    folder;
  - start the client as a normal user, which writes its profile under the
    per-user path, not the game folder;
  - join a LAN server;
  - uninstall, leaving the game data and per-user state behind;
  - the portable archive does the same from an unpacked folder.
