# KisakCOD

## About the project
An open source fully-buildable reimplementation of Call of Duty 4's Multi-Player .exe

Aimed towards mod developers and COD4 enthusiasts.

![licimg](./GPLv3_Logo.png)

### Development Blog
Learn about the Development of KisakCOD here: [https://lwss.github.io/Duty-Of-Kisak/](https://lwss.github.io/Duty-Of-Kisak/)

## Current build support

The engine currently produces Windows x86 multiplayer client and dedicated
server binaries. Native 64-bit builds for Windows amd64, Windows ARM64, Linux
amd64/arm64 and macOS arm64 are the port's targets. The headless server
builds and smoke-runs in CI on all five. The 64-bit clients are in development:
all five 64-bit clients reach in-game on retail data, through an opt-in
loader path ([NOW](docs/NOW.md)). The network target is the original **Steam release of Call of Duty 4,
version 1.8** (`1.8.13620`): native clients must join unmodified Steam 1.8
servers, and unmodified Steam 1.8 clients must join KisakCOD servers.

- [Charter](docs/CHARTER.md): goals, targets, non-goals and what counts as
  evidence.
- [Roadmap](docs/ROADMAP.md): the testing gates G0-G6 and the workstreams.
- [Now](docs/NOW.md): the KPIs and the ranked work queue.

## Current Requirements

| Platform | You install | The build script installs (`-InstallDeps` / `--install-deps`) |
|---|---|---|
| Windows 10/11 (x86, x64, ARM64) | Visual Studio 2026 with "Desktop development with C++" | CMake (winget) and the DirectX SDK headers and libraries (the pinned `Microsoft.DXSDK.D3DX` NuGet package, into `.deps\`) |
| Linux amd64/arm64 (apt, dnf or pacman) | `sudo` access | CMake, clang, pkg-config, FFmpeg, OpenAL Soft, SDL3, meson, ninja, glslang, and the pinned dxvk-native (built into `.deps/`) |
| macOS 26+ on Apple silicon | [Homebrew](https://brew.sh), Xcode Command Line Tools | CMake, pkgconf, meson, ninja, FFmpeg, openal-soft, mesa (KosmicKrisp Vulkan) |

All platforms also need a licensed copy of
[Call of Duty 4](https://store.steampowered.com/app/7940/Call_of_Duty_4_Modern_Warfare_2007/)
to play. The [June 2010 DirectX SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
still works on Windows: set `DXSDK_DIR` and the script uses it instead of the
NuGet package.

## How to build

Run the build script from the repository root. The first run with the install
flag fetches what's missing; later runs just build. Binaries land in `bin\<Config>`
on Windows and `bin/` on Linux and macOS.

### Windows (`build-win.ps1`)

```powershell
.\build-win.ps1 -InstallDeps          # first time: CMake and the DirectX SDK
.\build-win.ps1                       # Win32 Debug client and server (as build-win.bat)
.\build-win.ps1 -Config Release -Clean
.\build-win.ps1 -Arch x64 -Experimental64Bit -Targets KisakCOD-mp   # 64-bit client (or -Arch ARM64)
.\build-win.ps1 -Targets KisakCOD-dedi -HeadlessDedi                # headless server
.\build-win.ps1 -Run -RunArgs '+set','fs_basepath','D:\cod4-copy'   # build, then launch
```

| Option | Effect |
|---|---|
| `-Config Debug\|Release` | Build configuration (default Debug) |
| `-Arch Win32\|x64\|ARM64` | Target platform (default Win32); x64 and ARM64 need `-Experimental64Bit` and build in `build-<Arch>\` |
| `-Targets ...` | `KisakCOD-mp`, `KisakCOD-dedi`, `KisakCOD-sp` (default mp and dedi; sp is incomplete) |
| `-HeadlessDedi` | The dependency-free dedicated server profile used by the native port |
| `-Clean`, `-ConfigureOnly`, `-NoConfigure`, `-Jobs N` | Fresh configure; configure only (like `scripts\mksln.bat`); build only; parallelism |
| `-InstallDeps` | Install CMake, fetch the DirectX SDK NuGet package, and fetch vcpkg for OpenAL Soft and FFmpeg |
| `-Run`, `-RunArgs ...` | Launch the first built game with these arguments |

`Get-Help .\build-win.ps1 -Detailed` has the full reference. The script runs
under PowerShell 5.1 or 7 and in ConstrainedLanguage mode, and copies the OpenAL
Soft, FFmpeg, Steam and D3DX runtime DLLs beside the binaries. 64-bit builds link
no Steam.

### Linux and macOS (`build.sh`)

```sh
./build.sh --install-deps            # first time: packages, plus dxvk-native on Linux
./build.sh                           # 64-bit client and headless server
./build.sh server                    # just the headless server
./build.sh tests                     # portable tests, then ctest
./build.sh client --run -- +set fs_basepath ~/cod4-copy +devmap mp_crash
```

| Option | Effect |
|---|---|
| `client`, `server`, `tests` | What to build (default `client server`) |
| `--config Debug\|Release\|RelWithDebInfo` | Build configuration (default Release) |
| `--clean`, `--configure-only`, `--no-configure`, `--jobs N` | Fresh configure; configure only; build only; parallelism |
| `--install-deps` | Install packages (Homebrew, apt, dnf or pacman) and, on Linux, build dxvk-native into `.deps/` |
| `--dxvk-prefix DIR` | Linux: use an existing dxvk-native install |
| `--run [-- args]` | Launch the client (or the server, if it's the only component) with its runtime environment |
| `--dry-run` | Print the commands without running them |

`./build.sh --help` has the full reference. The script picks the
`CMakePresets.json` preset for the host (`linux-<arch>-client`,
`linux-<arch>-dedi`, `macos-arm64-client`, `macos-arm64-dedi`, and the test
presets `linux-<arch>-mp` and `macos-arm64-mp`), so a manual
`cmake --preset <name>` gives the same build. The `windows-x86-*` presets cover
hosted Win32 builds.

### Game data

Point the game at your install with `+set fs_basepath <dir>`, or copy the
complete install (including support files such as `localization.txt`) beside
the binaries. The 64-bit clients are experimental and write profiles under
`fs_basepath`, so run them against a copy-on-write clone of the install
(`cp -cR` on APFS, `cp --reflink` elsewhere), never the original
([CLIENT](docs/design/CLIENT.md)). The headless server compiles and links in CI
without DirectX, Miles, Bink or Steam; licensed map startup and network checks
remain a manual workflow.


```
Keep in Mind: This is a ~20 year old game with some known exploits. We will try to fix these as we become aware of them. Report a vulnerability privately, as [SECURITY.md](SECURITY.md) describes, not in a public issue.
However, there is a non-zero chance of some type of binary exploitation when playing online. Use a sandbox (Sandboxie?) for peace of mind. 
```

## Known Issues
Use the **[fork issues](https://github.com/jm2/kisakcod/issues)** for this fork's
implementation and delivery backlog; upstream reports remain available in
[SwagSoftware/KisakCOD](https://github.com/SwagSoftware/KisakCOD/issues).
Each gate in the [roadmap](docs/ROADMAP.md) has a `gate` issue, and the
current queue and KPIs live in [NOW](docs/NOW.md). Changes that touch the wire
must keep Steam 1.8 compatibility
([NET_STEAM18](docs/design/NET_STEAM18.md)). Contributors and agents start
with [AGENTS.md](AGENTS.md).

## Troubleshooting
- ***Can't Connect to Dedicated Server*** :
  -  Check `net_ip` and `net_port`, the server will increment the port if the preferred one isn't available but the client won't sweep upwards.
 - ***DLL Error upon launch*** :
   - You didn't copy over the necessary runtime DLL's

## FAQ
- Can we use AI in this project?
  - Yes you can, but you're still responsible for whatever you commit. In general, you should have the AI be assisting you, and not carrying you. We have started using AI to help de-bug, and it's been extremely helpful.

## Credits and Special Thanks
- ***All Original COD4 Developers (for creating one of the best games of all time)***
- https://github.com/PJayB/jk3src (Jedi Academy fork with .sln)
- https://github.com/voron00/CoD2rev_Server - Useful yacc code for the gsc scripting here
- https://github.com/shiversoftdev/BO3Enhanced - Viewed as reference code for some of the Steam API Auth
- [RAD Game Tools](https://www.radgametools.com/) for the Bink and Miles Sound System APIs the game was written against.
- [OpenAL Soft](https://openal-soft.org/) and [FFmpeg](https://ffmpeg.org/), which now play its sound and Bink cinematics.
- [ODE Physics](https://www.ode.org/) COD4 uses a modified version of this physics engine. Its license texts are in `src/physics/ode/` (`LICENSE.TXT`, LGPL-2.1+, and `LICENSE-BSD.TXT`); the ODE source headers offer the two as alternatives.


## Discord
[Join the KisakCOD Discord](https://discord.gg/9uqntRWMA3)
