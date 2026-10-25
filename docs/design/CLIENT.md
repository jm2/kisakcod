# Client

WS-6 · Gate G5 (Vulkan: G6) · Cap 15 KB · Related: [CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md), [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)

## Route

| Step | Renderer | Targets | Status |
|---|---|---|---|
| 1 | Existing D3D9 renderer, 64-bit | Windows amd64, arm64 | Interim vehicle |
| 2 | D3D9 via dxvk-native | Linux amd64/arm64; macOS arm64 on KosmicKrisp (Homebrew mesa; client macOS 26+, server 13+), jm2/dxvk fork via `KISAK_DXVK_MACOS` | Interim vehicle |
| 3 | Native Vulkan; MoltenVK on macOS (KosmicKrisp if the renderer needs more than the portability subset) | All five | Release (G6) |

Interim vehicles are test milestones, not deliverables.

### Windows 64-bit (step 1)

D3DX9 has no ARM64 library, so `KISAK_USE_D3DX` (set by `pre_build.cmake` for 32-bit Windows clients only) keeps D3DX on x86. Changes under the switch must leave x86 Release objects byte-identical.

- **Replacements:** 6 sites (`r_material_load_obj.cpp` ×4, `binktextures.cpp` ×2) use `gfx_d3d/r_shader_reflect` (CTAB constant table, semantics) and `gfx_d3d/r_shader_compile` (`ShaderBuffer`, D3DCompile).
- **Native D3DX9_43 rules the reflection matches:** ps_2_x reads `COLOR1` as COLOR0, as retail did (don't fix it); ps_1_x always outputs COLOR0; an SM1 CTAB sits only in the comments after the version token; pixel outputs are `oC#`/`oDepth`.
- **Dependencies:** no `d3dx9.lib`, but the Microsoft.DXSDK.D3DX 9.29.952.8 NuGet supplies the headers.
- **ARM64 build:** VS 2026 `-A ARM64` (CMake ≥ 4.2) with `-DKISAK_ALLOW_UNSUPPORTED_64BIT=ON -DKISAK_ENABLE_STEAM=OFF -DCICD=ON -DDXSDK_DIR=…`; check with `scripts/ci/verify-artifact-arch.ps1 -Expected ARM64`. MSVC defines `_M_ARM64`, not `__aarch64__`; `Sys_CycleCounter` reads CNTVCT_EL0. OpenAL Soft 1.25.x Release arm64 hangs in `alcDestroyContext` at shutdown, so vcpkg pins 1.24.3 until an arm64 Release re-test (overrides need a full vcpkg clone). An arm64 host also builds and runs the Win32 x86 targets under emulation, so one machine compares x86, Win64 and arm64 on the same data.
- **Run (antec-win):** D3D9 needs the console session: a disconnected session fails `GetDeviceCaps` with Invalid call, and an RDP session lists the GPU but returns `D3DERR_NOTAVAILABLE`. After an RDP login, hand the session over with `tscon (Get-Process -Id $PID).SessionId /dest:console` (PowerShell); from SSH, as admin, `schtasks /create /tn tscon1 /sc once /st 00:00 /ru SYSTEM /f /tr "tscon 1 /dest:console"` then `schtasks /run /tn tscon1`. Any RDP login takes the console back. Don't set the Terminal Services GPU policies (`bEnumerateHWBeforeSW`, `AVC444ModePreferred`, `AVCHardwareEncodePreferred`) on Windows 11 26H2: RdpIdd and TermService then crash-loop every 12 s. A fatal D3D error leaves a modal dialog, so automation must time out and kill the process. While connected, console commands need a leading `/`, or they go out as chat. Windows Firewall prompts once per new exe; Deny is fine for a loopback `devmap`.
- **Host quirk:** `platform-socket-stream-contracts` times out on a host that drops loopback RSTs; hosted CI is the reference.
- **CI `windows-client-64`:** builds KisakCOD-mp on `amd64`, native `arm64` and `amd64-sdl3`, and checks the PE machine type; it doesn't run the client. It's advisory, so read each leg. A link error on arm64 alone usually means new x86- or D3DX-only code.
- **vcpkg:** the x86 and Win64 clients take OpenAL Soft and FFmpeg from vcpkg; use a full git clone at the manifest baseline (a blobless clone fails port lookups). The nightly no-Steam job needs `setup-vcpkg`. A renamed exe that dies before writing a log: check the name length (30+ characters overran `Sys_GetSemaphoreFileName`, #777).
- **ASan (MSVC):** `CFLAGS`/`CXXFLAGS=/fsanitize=address`, `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release` (ASan rejects the Debug CRT in try-compile), and `clang_rt.asan_dynamic-x86_64.dll` from `VC\Tools\MSVC\<ver>\bin\Hostx64\x64` beside the exe. MSVC has no UBSan.
- **ASan+UBSan (clang-cl):** winget LLVM (`C:\Program Files\LLVM\bin`, not on PATH) with Ninja: `-fsanitize=address,undefined -fno-sanitize=vptr /EHsc /GR -Wno-c++11-narrowing` (setting the flags drops CMake's defaults). Link with MSVC `link.exe`, `/LIBPATH` to LLVM's `lib\clang\<N>\lib\windows`, plus `clang_rt.asan_dynamic-x86_64.lib` and `clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`, and ship LLVM's ASan DLL. Never `lld-link`: it merges `"*"`/`L"*"` and `""` literals (false ODR and `I_strnicmp` overflow reports) and fills the dllimport `__imp_AIL_*` pointers wrongly, so `SND_InitDriver` jumps to the image base.
- **Sanitizer logs:** quote `log_path` with forward slashes (`log_path='C:/dir/client'`); options split on `:`, so a bare drive letter aborts. UBSan: `print_stacktrace=1:halt_on_error=0`.

### x86 baseline (G0)

- The Steam client fails `CL_CDKeyValidate` off loopback (127.0.0.1 included) with no Steam running; use the nightly `KisakCOD-windows-x86-nosteam-Release` artifact. Bind `+set net_ip 127.0.0.1` on both ends.
- Put an x86 `d3dx9_43.dll` (NuGet Microsoft.DXSDK.D3DX 9.29.952.8) beside the exe; the build copies the OpenAL Soft and FFmpeg DLLs there itself.
- An empty dm server still rotates: a 1-minute round takes about 2 minutes of wall time with intermission and load.
- **Wine (Fedora):** `wine32` with a `WINEARCH=win32` prefix; `wine-common` must be installed or `wineboot` loops on missing `winmd` files; disable `mscoree,mshtml`; Fedora ships the builtin d3d9 as `wine-d3d9.dll`, so copy it to `d3d9.dll` in the prefix, and set `HKCU\Software\Wine\Direct3D\renderer=vulkan`. The display driver is chosen when the prefix's desktop starts, so restart `wineserver` after changing it. Piped stdin doesn't reach the Win32 console. A fresh profile shows a modal "Set Optimal Settings?" box before `R_Init`; under headless mutter answer it over XTest. The non-headless dedi reads `localization.txt` from its working directory, so run it from the install dir (else zones become `zone\(null)\`). A DB-thread `Com_Error` in a non-headless build hits a deliberate `__debugbreak`: `winedbg --file`, `cont`, then `x /s <com_errorMessage>`.
- **Stock 1.8 client (row 36):** it reads the CD key from `HKLM\SOFTWARE\Activision\Call of Duty 4`, value `codkey`, in the 32-bit view (`reg add … /reg:32`). Writing it needs a person to approve UAC on that host; the key never goes into files, logs or messages.

### macOS (step 2)

- **Driver:** KosmicKrisp, Mesa's conformant Vulkan on Metal (`brew install mesa`, macOS 26+), picked at run time with `VK_DRIVER_FILES=$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`. Mesa is never bundled.
- **dxvk:** `KISAK_DXVK_MACOS` builds jm2/dxvk `kisakcod-macos`, pinned in `scripts/extern/dxvk_macos.cmake`, with `-Dnative_macos`. The fork's `KISAK_MACOS.md` lists each patch and how to rebase it.
- **Not MoltenVK:** upstream dxvk won't support portability-subset drivers, and on MoltenVK every texture sampled black.
- **Minimums:** the client targets macOS 26 (`scripts/mp`); the server keeps 13.0. `scripts/extern/dxvk_macos.cmake` builds the dxvk dylibs with `MACOSX_DEPLOYMENT_TARGET=26.0` to match.
- **Frame rate:** KosmicKrisp presents FIFO even with `r_vsync 0`, so in-game FPS reads the display refresh (120 on the M1 Max). Compare CPU cost with `sample` instead.
- **Run:** `./build.sh --install-deps client` (Homebrew; preset `macos-arm64-client`), or `--run` to launch too. Launch with `DXVK_WSI_DRIVER=SDL3` and a local retail `fs_basepath`. Capture frames with Mesa's `VK_LAYER_MESA_screenshot` (`VK_LAYER_MESA_SCREENSHOT_CONFIG=frames=…,output_dir=…`).
- **Evidence:** zones loaded, the first error, and what each frame shows, described in text. Frames and data stay local.

Every client, x86 included, answers the engine's Miles and Bink calls with OpenAL Soft (`sound/snd_openal.cpp`: PCM/ADPCM samples, 2D pan, 3D position, FFmpeg streams; no reverb or EQ) and FFmpeg's Bink decoder (`gfx_d3d/r_bink_ffmpeg.cpp`, file opens only; movie audio is silent), owner 2026-10-22. No build links `mss32` or `binkw32`. Windows takes both from vcpkg (`vcpkg.json`, pinned by its baseline; `build-win.ps1 -InstallDeps` fetches vcpkg), Linux and macOS from pkg-config (`scripts/client_media.cmake`).

## Shaders

- Retail `.ff` shaders are D3D9 SM2/SM3 bytecode; there's no source.
- Before writing a translator for step 3, evaluate DXVK's DXSO front end. It compiles SM1–3 bytecode to SPIR-V and is zlib-licensed.
- Derived shader cache (`src/database/shader_cache.*`, no engine callers yet):
  - The key is a domain-separated SHA-256 of the original bytecode.
  - The sidecar header records format and converter versions, stage, shader model, and source and payload hashes. Any mismatch regenerates the sidecar from the original.
  - Bytecode that isn't valid SM2/SM3 is rejected, never cached.
  - Original `.ff` files are never modified.

## Usercmd invariance

Input from any platform (SDL, Cocoa, …) enters the existing `cl_input.cpp` path: `CL_MouseEvent` → `CL_Input` → `CL_CreateCmd` → `CL_FinishMove` → `CL_WritePacket`. It must not bypass, rescale or reorder that path:

- Mouse look is scaled by `cl_mouseAccel` × rate + `sensitivity`, then by the cgame FOV scale, then by `m_yaw`/`m_pitch`.
- Input is normalised by `frame_msec`.
- Angle deltas are quantised into `usercmd_s` with the existing msec accounting.
- Key and button state follow `CL_CreateCmd`/`CL_FinishMove` unchanged.

Identical input traces must produce the same usercmds as Windows x86.

## Audio, cinematics, voice

| Today | Native client | Gate |
|---|---|---|
| Miles | OpenAL Soft | Retail sound aliases play |
| Bink | FFmpeg | Retail `.bik` files play |
| Speex 1.1.9 over DirectSound | Speex 1.1.9 kept; OpenAL capture/playback | Decoder interop with Steam 1.8 peers, both directions |

Voice travels in out-of-band `"v"` packets:

- Client to server: qport, a count, then a size byte plus raw Speex bytes per frame.
- Server to client: a count, then per frame a talker byte, a size byte and the Speex bytes.

The framing has no codec-version field, so the claim that "a codec version is embedded in voice packets" was false. The gate is decoder interop ([NET_STEAM18.md](NET_STEAM18.md) captures voice); encoder byte equality is not required.

## Surface a native client replaces

| Surface | Size | Counted by |
|---|---|---|
| `src/gfx_d3d` | 187 files; 89.7k lines of `.cpp` | `git ls-files`, `wc -l` |
| D3D9 device | 289 `device->` calls in 22 files | grep, comments excluded |
| Miles | 131 `AIL_*` calls in 4 `src/sound` files | grep `AIL_…(` |
| Bink | 26 calls, all in `r_cinematic.cpp` | grep `Bink…(` |
| DirectSound voice | 24 lines in 3 `src/groupvoice` files | grep DirectSound API names |
| DirectInput | Vestigial: `<dinput.h>` and one unused function pointer; the mouse polls `GetCursorPos` | grep |

## Bring-up runs (Linux, dxvk-native)

Install pkg-config, OpenAL Soft and FFmpeg development files (Debian/Ubuntu
`libopenal-dev libavformat-dev libavcodec-dev libavutil-dev`; Fedora `openal-soft-devel
libavformat-free-devel libavcodec-free-devel libavutil-free-devel`). Build dxvk-native 3.1.1 with only
its SDL3 WSI, then the client; `./build.sh --install-deps` does both, dxvk into `.deps/`. The client presets need `PKG_CONFIG_PATH`
pointing at that install:

```sh
meson setup --buildtype=release --prefix=$P -Dnative_sdl3=enabled \
  -Dnative_sdl2=disabled -Dnative_glfw=disabled build <dxvk src>
ninja -C build install
PKG_CONFIG_PATH=$P/lib64/pkgconfig cmake --preset linux-amd64-client
cmake --build build-linux-amd64-client -j --target KisakCOD-mp
LD_LIBRARY_PATH=$P/lib64 bin/KisakCOD-mp +set fs_basepath <install> \
  +set fs_homepath <scratch> +set r_fullscreen 0
```

`DXVK_HUD=fps` draws the frame rate on screen. A data-free run stops at
`fileSysCheck.cfg` but still creates the device. The client reads and writes
profiles under `fs_basepath/players`, so run it against a copy-on-write clone
of the install (`cp -cR` on APFS, `cp --reflink` elsewhere) with a seeded
profile, never the retail directory; symlinked installs don't list. Retail data rules are in
[FASTFILE_LOADER.md](FASTFILE_LOADER.md).

**State without a screen.** The Release build lacks types for globals, so
read them with casts in `gdb -p <pid>`:

- `*(int*)&com_frameNumber`, twice, gives main-loop frames per second
  (capped at about 62/s, or 30/s while loading, until the client is active);
- `((int(*)(int))UI_GetActiveMenu)(0)` (1 = main);
- `((int(*)(void*))Menu_Count)((void*)&uiInfoArray)`;
- `*(char**)` of `Menu_GetFocused(&uiInfoArray)`: the focused menu name.

The engine names its main thread `Main`, so find the process with `pgrep -f`
on the binary path, not `pgrep -x`.

**Without a desktop.**

- A headless Wayland compositor gives a display with no input seat:
  `mutter --headless --wayland --no-x11 --wayland-display=<name> --virtual-monitor 1920x1080`.
- Run the client with `WAYLAND_DISPLAY=<name> SDL_VIDEO_DRIVER=wayland`.

**Scripted runs.**

- Drive the console on stdin (`screenshot`, `+attack`, `quit`), or by rcon,
  whose output goes into the UDP reply and drops bytes on long replies. `wait`
  doesn't delay in exec'd configs, and there's no `echo`; before the client is active, an
  unknown command's `Unknown command "X"` line works as a marker.
- Spawn without input after about 30 s: `openScriptMenu team_marinesopfor
  autoassign`, then `openScriptMenu changeclass assault_mp,0`.
- Markers: `byte snapshot for <name>` is a server print, so a listen server
  shows it once the client is active. On headless, `Huffman Took` marks only
  the first map; later maps print `Hitch warning`. `iassert` compiles to
  nothing in Release, so a clean Release run proves nothing about asserts.
- Cinematics: `developer 1` prints `Bink via FFmpeg: opened …` and `closed at
  frame N of N after decoding N-1 frames in X s`; the intro chain takes about
  115 s. Audio: `ALSOFT_CONF=al.conf`, with `al.conf` holding
  `[general]` `drivers=wave` `sample-type=int16` and `[wave]` `file=mix.wav`
  (one per line), records the final mix for RMS/peak numbers. Never link
  Apple's OpenAL.framework.

**Maps.**

- `+devmap <map>` needs a selected profile: `profiles/active.txt` naming a
  profile directory. Without one the map load stops at "You have not chosen a
  profile yet".
- A client that plays cinematics (Win32, and POSIX with FFmpeg) runs the
  intro chain on a fresh profile, as retail does. The intro drops a connect
  in flight, so a scripted `+devmap` sets `+set com_introPlayed 1`. Stub
  builds skip the intro while connecting (#614).

**Screenshots.**

- Regression sets (`scripts/screenshot_set.py`; `r_gamma` is a display ramp, so shots miss it): frames aren't deterministic, so settle 1.5-2.5 s
  after `setviewpos`, pin `com_maxfps`, MSAA off and the window size, hide
  gun and HUD, and shoot A/B/A. Compare the share of pixels off by >16/255,
  not the max; calm-map noise is a mean under 0.2 and under 0.5% of pixels.

- Send `screenshot` on stdin once the menu is up: a `+wait N +screenshot`
  capture fires while zones load and can be all black.
- `screenshotJPEG` has no encoder in any build and says so (#593); use
  `screenshot`, which writes TGA.
- dxvk's `GetFrontBufferData` copies the swapchain backbuffer, not the
  desktop, to the destination's top-left, so the Win32 monitor offset doesn't
  apply under `KISAK_DXVK_NATIVE` (#593).
- After `Present` a discard swapchain's backbuffer is undefined, so
  `screenshot` reads the backbuffer before `Present`, resolving a
  multisampled one with `StretchRect` before `GetRenderTargetData` (#624).

**Sanitizer evidence runs (G5 rows 55-60).**

- Build client and server with clang `-fsanitize=address,undefined`; POSIX
  clients need `-shared-libsan` (#759), or ASan reports a bad free in
  `FS_Startup`. Give each process `ASAN_OPTIONS`/`UBSAN_OPTIONS` with
  `log_path=…` (and UBSan `halt_on_error=0`), so 0 reports means no files.
- Server: `+set g_log games_mp.log +set g_logsync 1`. Its J;/Q;/InitGame/
  ExitLevel lines are the timeline; CS_ACTIVE prints need `developer 1`.
  Start the rotation on another map, since its first entry reloads the
  current one, and set `scr_dm_timelimit` only after spawning.
- Triage: about 120 client UBSan reports in five classes (flattened rows,
  misaligned packed loads, function-type casts, LCG overflow, null
  zero-length `mem*`). Group them by `file:line` and check that each
  out-of-bounds index stays inside its parent object.
- macOS: LAN UDP from tmux fails with EHOSTUNREACH and never prompts for
  Local Network access; launch through Terminal (`open -a Terminal x.command`).
  On Linux, find your own mutter by its `--wayland-display` and stop it by
  PID, since peers share the host.

### Loader hooks

The 64-bit disk32 converters create renderer objects only on a client,
through `database/db_disk32_renderer_hooks.h`. Its one `KISAK_DEDI_HEADLESS`
switch keeps a headless server's handles null.

| Family | Client hook |
| --- | --- |
| TechniqueSet | `Load_BuildVertexDecl`, `Load_CreateMaterial{Vertex,Pixel}Shader` |
| Image | `Load_Texture`; a texture alias `AddRef`s the earlier texture (`ShareTexture`) |
| Material | `Load_PicmipWater`, once per completed water |
| GfxWorld | `Load_VertexBuffer` for the world and layer vertices |

- **Aliases:** each hook sits inside its converter, so an alias reuses the
  earlier object.
- **XModel:** needs no hook. On a client, zone blocks 7 and 8 are static
  vertex and index buffers, and `DB_SetStreamIndex` copies what streams into
  them when the stream position pops.
- **Tests:** disk32 family tests build headless unless the family name ends
  in `-client`.

### 64-bit patterns the client hit

NATIVE64.md is at its cap, so the patterns are listed here. Grep for:

- x86 byte sizes for pointer-holding structs, such as `UI_Alloc(8u, 4)` for a
  two-pointer node;
- `_DWORD` arrays holding pointers;
- x86 strides in handle math, such as `/ 40` for a struct that is 48 bytes at
  64-bit;
- `uint32_t` free-list links;
- a struct pointer stepped one byte at a time (UBSan misalignment);
- an unsigned index minus a constant into the neighbouring member (#611). x86
  wraps it mod 2^32, but 64-bit zero-extends it far out of bounds.
