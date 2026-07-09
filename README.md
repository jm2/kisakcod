# KisakCOD

## About the project
An open source fully-buildable reimplementation of Call of Duty 4's Multi-Player .exe

Aimed towards mod developers and COD4 enthusiasts.

![licimg](./GPLv3_Logo.png)

### Development Blog
Learn about the Development of KisakCOD here: [https://lwss.github.io/Duty-Of-Kisak/](https://lwss.github.io/Duty-Of-Kisak/)

## Current build support

The engine currently produces Windows x86 multiplayer client and dedicated
server binaries. Win64, Windows ARM64, Linux amd64/arm64, and macOS arm64 are
active port targets; they are not yet runnable engine builds. See
[the porting plan](docs/PORTING.md) and [codebase audit](docs/CODEBASE_AUDIT.md).

## Current Requirements
- Windows OS
- Visual Studio 2022
- CMake >= 3.16
- [DirectX SDK 2010](https://www.microsoft.com/en-us/download/details.aspx?id=6812)
- Steam with a copy of [Call of Duty 4](https://store.steampowered.com/app/7940/Call_of_Duty_4_Modern_Warfare_2007/)


## How to build

1. Install the requirements and clone the repository.
2. From PowerShell in the repository root, run `.\build-win.ps1`.
3. Copy the complete licensed COD4 data installation beside the binaries in
   `bin\Debug` or set up the equivalent game base path. Do not omit support
   files such as `localization.txt`.
4. Run `KisakCOD-mp.exe` or `KisakCOD-dedi.exe`.

The build copies the required Miles and Steam runtime DLLs automatically.
Use `Get-Help .\build-win.ps1 -Detailed` for configuration, target, and clean
build options. Single-player is excluded by default because it is incomplete.
The dedicated server still defaults to the legacy client-backed source profile;
`.\build-win.ps1 -Targets KisakCOD-dedi -HeadlessDedi` opts into the
experimental headless profile used for the native port burn-down.

Portable utility tests can be built on Linux without licensed game data:

```sh
cmake -S . -B build-tests \
  -DKISAK_BUILD_MP=OFF -DKISAK_BUILD_DEDICATED=OFF -DKISAK_BUILD_SP=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```


```
Keep in Mind: This is a ~20 year old game with some known exploits. We will try to fix these as we become aware of them.
However, there is a non-zero chance of some type of binary exploitation when playing online. Use a sandbox (Sandboxie?) for peace of mind. 
```

## Known Issues
(Use the **[issues](https://github.com/SwagSoftware/KisakCOD/issues)** section)

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
