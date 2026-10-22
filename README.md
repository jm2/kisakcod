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
- Windows: Visual Studio 2026 with the C++ workload, CMake >= 4.2 and the
  DirectX SDK (`.\build-win.ps1 -InstallDeps` fetches CMake and the SDK's
  NuGet package; the [June 2010 SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812) also works)
- Linux (amd64, arm64) or macOS (Apple silicon, macOS 26+ for the client):
  `./build.sh --install-deps` installs the rest through apt, dnf, pacman or
  Homebrew
- Steam with a copy of [Call of Duty 4](https://store.steampowered.com/app/7940/Call_of_Duty_4_Modern_Warfare_2007/)


## How to build

### Windows

1. Install the requirements and clone the repository.
2. From PowerShell in the repository root, run `.\build-win.ps1`
   (`-InstallDeps` the first time).
3. Copy the complete licensed COD4 data installation beside the binaries in
   `bin\Debug` or set up the equivalent game base path. Do not omit support
   files such as `localization.txt`.
4. Run `KisakCOD-mp.exe` or `KisakCOD-dedi.exe`, or add `-Run` to the build.

The build copies the required Miles and Steam runtime DLLs automatically.
Use `Get-Help .\build-win.ps1 -Detailed` for configuration, target, and clean
build options. Single-player is excluded by default because it is incomplete.
The dedicated server still defaults to the legacy client-backed source profile;
`.\build-win.ps1 -Targets KisakCOD-dedi -HeadlessDedi` opts into the
dependency-free headless profile used for the native port. That profile now
compiles and links in CI without DirectX, Miles, Bink, or Steam; licensed map
startup and network-response validation remain a protected manual workflow.

### Linux and macOS

```sh
./build.sh --install-deps            # first time: packages (and dxvk-native on Linux)
./build.sh                           # the 64-bit client and headless server, into bin/
./build.sh tests                     # the portable tests, then ctest
./build.sh client --run -- +set fs_basepath <copy of your install> +devmap mp_crash
```

`./build.sh --help` lists the options. The script picks the
`CMakePresets.json` preset for the host (`linux-<arch>-client`,
`linux-<arch>-dedi`, `macos-arm64-client`, `macos-arm64-dedi`, and the
test presets `linux-<arch>-mp`/`macos-arm64-mp`), so `cmake --preset` gives the
same build. The clients are experimental; run them against a copy-on-write
clone of your install, never the original ([CLIENT](docs/design/CLIENT.md)).
The `windows-x86-*` presets cover hosted Win32 builds.


```
Keep in Mind: This is a ~20 year old game with some known exploits. We will try to fix these as we become aware of them.
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
- [RAD Game Tools](https://www.radgametools.com/) for their Bink and Miles Sound System libraries.
- [ODE Physics](https://www.ode.org/) COD4 uses a modified version of this physics engine.


## Discord
[Join the KisakCOD Discord](https://discord.gg/9uqntRWMA3)
