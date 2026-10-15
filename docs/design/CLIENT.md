# Client

WS-6 · Gate G5 (Vulkan: G6) · Cap 10 KB · Related: [CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md), [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)

## Route

| Step | Renderer | Targets | Status |
|---|---|---|---|
| 1 | Existing D3D9 renderer, 64-bit | Windows amd64 | Interim vehicle |
| 2 | D3D9 via dxvk-native | Linux amd64/arm64; macOS arm64 on KosmicKrisp (Homebrew mesa; client macOS 26+, server 13+), jm2/dxvk fork via `KISAK_DXVK_MACOS` | Interim vehicle |
| 3 | Native Vulkan; MoltenVK on macOS | All five | Release (G6) |

Interim vehicles are test milestones, not deliverables.

Miles (`mss32.dll`) and Bink (`binkw32.dll`) are 32-bit only, so a 64-bit client can't load them from a retail install. Step 1 needs OpenAL Soft and FFmpeg, or silent stubs, from its first build.

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
