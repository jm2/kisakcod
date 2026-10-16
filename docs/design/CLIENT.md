# Client

WS-6 · Gate G5 (Vulkan: G6) · Cap 15 KB · Related: [CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md), [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)

## Route

| Step | Renderer | Targets | Status |
|---|---|---|---|
| 1 | Existing D3D9 renderer, 64-bit | Windows amd64, arm64 | Interim vehicle |
| 2 | D3D9 via dxvk-native | Linux amd64/arm64; macOS arm64 on KosmicKrisp (Homebrew mesa; client macOS 26+, server 13+), jm2/dxvk fork via `KISAK_DXVK_MACOS` | Interim vehicle |
| 3 | Native Vulkan; MoltenVK on macOS | All five | Release (G6) |

Interim vehicles are test milestones, not deliverables.

### Windows 64-bit (step 1)

D3DX9 has no ARM64 library, so `KISAK_USE_D3DX` (set by `pre_build.cmake` for 32-bit Windows clients only) keeps D3DX on x86. Changes under the switch must leave x86 Release objects byte-identical.

- **Replacements:** 6 sites (`r_material_load_obj.cpp` ×4, `binktextures.cpp` ×2) use `gfx_d3d/r_shader_reflect` (CTAB constant table, semantics) and `gfx_d3d/r_shader_compile` (`ShaderBuffer`, D3DCompile).
- **Native D3DX9_43 rules the reflection matches:** ps_2_x reads `COLOR1` as COLOR0, as retail did (don't fix it); ps_1_x always outputs COLOR0; an SM1 CTAB sits only in the comments after the version token; pixel outputs are `oC#`/`oDepth`.
- **Dependencies:** no `d3dx9.lib`, but the Microsoft.DXSDK.D3DX 9.29.952.8 NuGet supplies the headers.
- **ARM64 build:** VS 2026 `-A ARM64` (CMake ≥ 4.2) with `-DKISAK_ALLOW_UNSUPPORTED_64BIT=ON -DKISAK_ENABLE_STEAM=OFF -DCICD=ON -DDXSDK_DIR=…`; check with `scripts/ci/verify-artifact-arch.ps1 -Expected ARM64`. MSVC defines `_M_ARM64`, not `__aarch64__`; `Sys_CycleCounter` reads CNTVCT_EL0.
- **Host quirk:** `platform-socket-stream-contracts` times out on a host that drops loopback RSTs; hosted CI is the reference.
- **CI `windows-client-64`:** builds KisakCOD-mp on `amd64`, native `arm64` and `amd64-sdl3`, and checks the PE machine type; it doesn't run the client. It's advisory, so read each leg. A link error on arm64 alone usually means new x86- or D3DX-only code.

### macOS (step 2)

- **Driver:** KosmicKrisp, Mesa's conformant Vulkan on Metal (`brew install mesa`, macOS 26+), picked at run time with `VK_DRIVER_FILES=$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`. Mesa is never bundled.
- **dxvk:** `KISAK_DXVK_MACOS` builds jm2/dxvk `kisakcod-macos`, pinned in `scripts/extern/dxvk_macos.cmake`, with `-Dnative_macos`. The fork's `KISAK_MACOS.md` lists each patch and how to rebase it.
- **Not MoltenVK:** upstream dxvk won't support portability-subset drivers, and on MoltenVK every texture sampled black.
- **Minimums:** the client targets macOS 26 (`scripts/mp`); the server keeps 13.0.
- **Run:** configure with `KISAK_EXPERIMENTAL_POSIX_CLIENT`, `KISAK_CLIENT_SDL3` and `KISAK_DXVK_MACOS`. Launch with `DXVK_WSI_DRIVER=SDL3`, local retail `fs_basepath` and `db_unverified64 1`. Capture frames with Mesa's `VK_LAYER_MESA_screenshot` (`VK_LAYER_MESA_SCREENSHOT_CONFIG=frames=…,output_dir=…`).
- **Evidence:** zones loaded, the first error, and what each frame shows, described in text. Frames and data stay local.

Miles (`mss32.dll`) and Bink (`binkw32.dll`) are 32-bit only, so a 64-bit client can't load them from a retail install. Step 1 needs OpenAL Soft and FFmpeg, or silent stubs, from its first build; 64-bit builds default to the stubs (`KISAK_MEDIA_STUBS`).

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

Build dxvk-native 3.1.1 with only its SDL3 WSI, then the client. The
client presets need `PKG_CONFIG_PATH` pointing at that install:

```sh
meson setup --buildtype=release --prefix=$P -Dnative_sdl3=enabled \
  -Dnative_sdl2=disabled -Dnative_glfw=disabled build <dxvk src>
ninja -C build install
PKG_CONFIG_PATH=$P/lib64/pkgconfig cmake --preset linux-amd64-client
cmake --build build-linux-amd64-client -j --target KisakCOD-mp
LD_LIBRARY_PATH=$P/lib64 bin/KisakCOD-mp +set fs_basepath <install> \
  +set fs_homepath <scratch> +set r_fullscreen 0 +set db_unverified64 1
```

`DXVK_HUD=fps` draws the frame rate on screen. A data-free run stops at
`fileSysCheck.cfg` but still creates the device. The client reads and writes
profiles under `fs_basepath/players`, so run it against a copy-on-write clone
of the install (`cp -cR` on APFS, `cp --reflink` elsewhere) with a seeded
profile, never the retail directory; symlinked installs don't list. Retail data rules are in
[FASTFILE_LOADER.md](FASTFILE_LOADER.md).

**State without a screen.** The Release build lacks types for globals, so
read them with casts in `gdb -p <pid>`:

- `*(int*)&com_frameNumber`, twice, gives FPS;
- `((int(*)(int))UI_GetActiveMenu)(0)` (1 = main);
- `((int(*)(void*))Menu_Count)((void*)&uiInfoArray)`;
- `*(char**)` of `Menu_GetFocused(&uiInfoArray)`: the focused menu name.

The engine names its main thread `Main`, so find the process with `pgrep -f`
on the binary path, not `pgrep -x`.

**Without a desktop.**

- A headless Wayland compositor gives a display with no input seat:
  `mutter --headless --wayland --no-x11 --wayland-display=<name> --virtual-monitor 1920x1080`.
- Run the client with `WAYLAND_DISPLAY=<name> SDL_VIDEO_DRIVER=wayland`.

**Maps.**

- `+devmap <map>` needs a selected profile: `profiles/active.txt` naming a
  profile directory. Without one the map load stops at "You have not chosen a
  profile yet".
- Builds without Bink skip the intro cinematic while a connect is in flight
  (#614), so `com_introPlayed` isn't needed.

**Screenshots.**

- `screenshotJPEG` has no encoder in any build and says so (#593); use
  `screenshot`, which writes TGA.
- dxvk's `GetFrontBufferData` copies the swapchain backbuffer, not the
  desktop, to the destination's top-left, so the Win32 monitor offset doesn't
  apply under `KISAK_DXVK_NATIVE` (#593).
- After `Present` a discard swapchain's backbuffer is undefined; capture
  before `Present` (branch `operator/screenshot-backbuffer`, unmerged).

**Open (amd64, 2026-10-09):** the client loads all base zones and reports
the main menu at about 200-240 fps, but every capture is black, including a
backbuffer capture with `r_clear steady` and a magenta `r_clearColor`. The
arm64 client reports its main menu too, and the macOS client draws the full
main menu at a steady ~58 fps (probably a FIFO-only present on KosmicKrisp). Next: capture after the menu is up; if still
black with the magenta clear, the frame isn't reaching the swapchain.
On arm64 (2026-10-15), `+devmap mp_crash` connects, and the server marks the
client active. The static-model cache crash that followed is fixed in #612.

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
