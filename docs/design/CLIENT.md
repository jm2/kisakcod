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

## POSIX bring-up runbook

**Build.**
- Install dxvk-native 3.1.1 built with its SDL3 WSI: `meson -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled`.
- Configure `linux-amd64-client` or `linux-arm64-client` with `PKG_CONFIG_PATH` set to its `lib*/pkgconfig`.
- POSIX links the system's shared SDL3, which dxvk-native also uses. A static SDL copy breaks dxvk's WSI.

**Run.**
- `posix_client_main.cpp` sets `DXVK_WSI_DRIVER=SDL3` unless it is already set.
- A host without a desktop can use a headless Wayland compositor: `mutter --headless --wayland --no-x11 --virtual-monitor 1920x1080`, with `WAYLAND_DISPLAY` set to its socket and `SDL_VIDEO_DRIVER=wayland`. It has no input seat.
- Flags:
  - `+set db_unverified64 1` admits asset families K4 hasn't verified.
  - `+set com_introPlayed 1` skips the intro chain (`COM_PlayIntroMovies`). Otherwise, on a fresh profile, `cinematic` sets `CA_CINEMATIC` during a `+devmap` handshake and the client ignores `connectResponse`. Retail does the same.
  - `+devmap <map>` loads a map, but needs a profile: `fs_homepath/players/profiles/<name>/` plus `profiles/active.txt` naming it.
- Retail data stays on the host. Only data-free evidence leaves it.

**Data-free evidence, read with gdb attached to the running client.** The engine renames its main thread `Main`, so find the process with `pgrep -f` on the binary path.
- `UI_GetActiveMenu(0)`: 1 is main.
- `Menu_Count(&uiInfoArray)`, and the focused menu's name through `Menu_GetFocused`.
- `clientUIActives[0]`: `keyCatchers`, `connectionState` (`connstate_t`).
- `com_frameNumber`: twice, to measure frame rate.
- To time a load, break on `Load_VertexBuffer` and on `R_AllocStatic{Vertex,Index}Buffer`.

**Status (2026-10-15).** arm64 on Apple M1, dxvk 3.1.1:
- Main menu at about 190 fps.
- `mp_crash` loads, and the client reaches `CL_InitCGame`.
- Current stop: `CG_Init` → `BG_LoadAnim` → `Scr_EndLoadAnimTrees` → `SL_ShutdownSystem(2)`, "invalid script string user-reference removal".

### Loader hooks

The 64-bit disk32 converters create renderer objects only on a client, through `database/db_disk32_renderer_hooks.h`. Its one `KISAK_DEDI_HEADLESS` switch keeps headless handles null.
- **techniqueset:** `Load_BuildVertexDecl` and `Load_CreateMaterial{Vertex,Pixel}Shader`.
- **image:** `Load_Texture`. An alias `AddRef`s the earlier texture through `ShareTexture`.
- **material:** `Load_PicmipWater`, once per completed water.
- **GfxWorld:** `Load_VertexBuffer`.

Each hook sits inside the converter, so an alias reuses its object.

XModel needs no hook. On a client, blocks 7 and 8 are static vertex and index buffers, and `DB_SetStreamIndex` copies what streams into them on pop.

Family tests build headless unless the family name ends in `-client`.

A sequence whose marks add user-4 or user-8 references holds `db_hashCritSect` through `DB_BeginRegistrySession`/`DB_EndRegistrySession`, never `Sys_LockWrite`. The bridge's registry window needs the hash (#600).

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
