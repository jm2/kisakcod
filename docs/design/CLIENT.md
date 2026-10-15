# Client

WS-6 · Gate G5 (Vulkan: G6) · Cap 10 KB · Related: [CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md), [ADR-0003](../decisions/0003-testing-gates-and-vehicles.md)

## Route

| Step | Renderer | Targets | Status |
|---|---|---|---|
| 1 | Existing D3D9 renderer, 64-bit | Windows amd64, arm64 | Interim vehicle |
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

## Windows 64-bit client (step 1)

D3DX9 ships no ARM64 library, so only the 32-bit client keeps D3DX. `scripts/pre_build.cmake` defines `KISAK_USE_D3DX` for 32-bit Windows client targets alone.

**Six call sites replace D3DX on every 64-bit target:** 4 in `r_material_load_obj.cpp` and 2 in `deps/binklib/binktextures.cpp`.
- `gfx_d3d/r_shader_reflect` finds the CTAB constant table and the input/output semantics.
- `gfx_d3d/r_shader_compile` provides `ShaderBuffer` and `R_CompileShader` (D3DCompile from d3dcompiler_47, which ships with Windows).

**Rules checked against native D3DX9_43**, on vs/ps 1.x–3.x compiler output and hand-built programs:
- On ps_2_x, an HLSL `COLOR1` input reads as COLOR0, as retail did. Don't "fix" it.
- ps_1_x always reports COLOR0 as its output.
- An SM1 CTAB is found only in the comments right after the version token.
- Pixel outputs are `oC#`/`oDepth`, not `v#` reads; Wine's model differs here.

**Changes under `KISAK_USE_D3DX`:** show the x86 Release objects of the touched files unchanged.
- Compare non-`.debug$` sections with relocations resolved to symbol names.
- Debug differs only in `__LINE__` immediates when lines are inserted.

**64-bit Windows clients link no `d3dx9.lib`**, but still need the Microsoft.DXSDK.D3DX NuGet (9.29.952.8) for headers. `r_material_load_obj.cpp` reads the `D3DXSHADER_CONSTANTTABLE`/`D3DXSEMANTIC` layouts. Miles/Bink are silent stubs on 64-bit (`KISAK_MEDIA_STUBS`, default on).

**Windows ARM64 builds** natively on an ARM64 host:
- **Generator:** VS 2026, `-A ARM64` (needs CMake ≥ 4.2). The client also needs `-DKISAK_ALLOW_UNSUPPORTED_64BIT=ON -DKISAK_ENABLE_STEAM=OFF` (no ARM64 Steamworks), plus `-DCICD=ON -DDXSDK_DIR=<nuget>/build/native`.
- **Check the artifact:** `scripts/ci/verify-artifact-arch.ps1 -Expected ARM64 <exe>`.
- **Timing counter:** MSVC defines `_M_ARM64`, not `__aarch64__`. `Sys_CycleCounter` reads CNTVCT_EL0 there.
- **`platform-socket-stream-contracts` can time out on a host:** if a filter drops loopback RSTs, a connect to a closed port never fails. Hosted CI is the reference.

**CI `windows-client-64`** compiles and links KisakCOD-mp, then checks the PE machine type. It doesn't run the client.
- **Legs:** `amd64` (windows-2025), `arm64` (windows-11-arm, native) and `amd64-sdl3` (`-DKISAK_CLIENT_SDL3=ON`, the only Windows build of the SDL client code).
- **Advisory:** the job is `continue-on-error` and not in `scaffolding-complete`, so a red leg doesn't fail the run. Read the leg's own result.
- **What a failure means:** a link error on `arm64` alone usually means new x86- or D3DX-only code. Compile errors on all legs usually mean a 64-bit layout assert.

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
