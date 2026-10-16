# Client

WS-6 · Gate G5 (Vulkan: G6) · Cap 10 KB · Related: [CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md), [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)

## Route

| Step | Renderer | Targets | Status |
|---|---|---|---|
| 1 | Existing D3D9 renderer, 64-bit | Windows amd64 | Interim vehicle |
| 2 | D3D9 via dxvk-native | Linux amd64/arm64; macOS arm64 on KosmicKrisp (Homebrew mesa; client macOS 26+, server 13+), jm2/dxvk fork via `KISAK_DXVK_MACOS` | Interim vehicle |
| 3 | Native Vulkan; MoltenVK on macOS | All five | Release (G6) |

Interim vehicles are test milestones, not deliverables.

### macOS (step 2)

- **Driver:** KosmicKrisp, Mesa's conformant Vulkan on Metal (`brew install mesa`, macOS 26+), picked at run time with `VK_DRIVER_FILES=$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`. Mesa is never bundled.
- **dxvk:** `KISAK_DXVK_MACOS` builds jm2/dxvk `kisakcod-macos`, pinned in `scripts/extern/dxvk_macos.cmake`, with `-Dnative_macos`. The fork's `KISAK_MACOS.md` lists each patch and how to rebase it.
- **Not MoltenVK:** upstream dxvk won't support portability-subset drivers, and on MoltenVK every texture sampled black.
- **Minimums:** the client targets macOS 26 (`scripts/mp`); the server keeps 13.0.
- **Run:** configure with `KISAK_EXPERIMENTAL_POSIX_CLIENT`, `KISAK_CLIENT_SDL3` and `KISAK_DXVK_MACOS`. Launch with `DXVK_WSI_DRIVER=SDL3`, local retail `fs_basepath` and `db_unverified64 1`. Capture frames with Mesa's `VK_LAYER_MESA_screenshot` (`VK_LAYER_MESA_SCREENSHOT_CONFIG=frames=…,output_dir=…`).
- **Evidence:** zones loaded, the first error, and what each frame shows, described in text. Frames and data stay local.

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
