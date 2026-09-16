# Production audio, voice and cinematic replacement parity gates

Status: source/definition inventory complete at the recorded SHA below; runtime,
original-reference and per-target acceptance remain **unproven**. This document
is documentation only. It does not change production behavior, codecs, framing,
dependencies or packaging, and it does not certify retail compatibility.

Tracking: [issue #132](https://github.com/jm2/kisakcod/issues/132) / bead
`ki-dkeb` (A10). The mandatory compatibility parent is
[#122](https://github.com/jm2/kisakcod/issues/122) and
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). Related forks: A05
[#127](https://github.com/jm2/kisakcod/issues/127), A07
[#129](https://github.com/jm2/kisakcod/issues/129), A09
[#131](https://github.com/jm2/kisakcod/issues/131), A11
[#133](https://github.com/jm2/kisakcod/issues/133), A12
[#134](https://github.com/jm2/kisakcod/issues/134), A13
[#135](https://github.com/jm2/kisakcod/issues/135), A15
[#137](https://github.com/jm2/kisakcod/issues/137). Upstream dependency plan:
[#75 Bink](https://github.com/SwagSoftware/KisakCOD/issues/75),
[#76 Miles](https://github.com/SwagSoftware/KisakCOD/issues/76).

Scope: the acceptance gates that replacement audio playback, voice
capture/playback and cinematics must satisfy, mapped onto the concrete
production subjects that exist in this tree. It defines *what must be
demonstrated* and *how*. It does not implement a backend, choose a codec
substitution, rebake content, or claim any gate already passes.

## 1. Provenance and evidence boundary

- **Recorded source SHA:** `2babfed8adb17a5b3c80db288890992ec51ec49c`
  (`2babfed8`, the `origin/master` tip when this stage started; merge of fork
  PR #106). Every line citation below was read from that exact checkout, and
  paths are repository-relative.
- **Evidence class:** direct source inspection at the recorded SHA. No audio
  device, no Bink content, no licensed original commercial 1.7 or Steam 1.8
  binary, and no reference session were used. **Nothing here is a runtime
  result.**
- **Scope boundary:** the mandatory contract in
  [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) governs all of this
  work. Perfect interoperation with unmodified original commercial 1.7 and
  Steam commercial 1.8 is required and remains **unproven**. Community
  CoD4x "1.8" is a different reference and is not a substitute. Media
  tolerances defined here never relax valid wire/disk byte compatibility.
- **Numbers:** all counts, byte bounds and mode lists below were measured at
  the recorded SHA. Where this audit disagrees with older plan prose (for
  example the historical "54 `AIL_*` calls" figure in `PORTING.md`), the
  source at this SHA wins; the discrepancy is noted where it matters.

## 2. Where media is used today (source inventory)

### 2.1 Audio playback engine

| Subject | State at this SHA | Evidence |
|---|---|---|
| Core mixer/alias engine | Decompiled `snd.cpp` (5,032 lines), `snd_driver.cpp` (1,873 lines), `snd_utils.cpp`, headers `snd_local.h`/`snd_public.h` | `src/sound/` |
| Miles bridge | `SND_*` driver functions call the proprietary Miles `AIL_*` API directly. Measured `AIL_*` occurrences: **25** in `snd_mss.cpp`, **104** in `snd_driver.cpp`, **3** in `snd.cpp` (**132** total under `src/sound/`) | `src/sound/snd_mss.cpp`, `snd_driver.cpp`, `snd.cpp` |
| Miles dependency | 32-bit Windows-only blob/import library + headers | `deps/msslib/{mss32.lib,mss.h,dlls/}` |
| Portable backend | **None.** A precise word-boundary search for `OpenAL`, `alSource`, `alcOpenDevice` and `ALCdevice` over `src/` and `deps/` returns **no matches** at this SHA | verified at recorded SHA |
| Null-sound media contract | `SND_IsNullSoundFile(const SoundFile*)` is part of the public sound API | `src/sound/snd_public.h:515`, `src/sound/snd.cpp:2187`, consumed at `snd.cpp:1648` |

The driver surface a replacement must reproduce is the full `SND_*` contract in
`src/sound/snd_public.h` and `snd_driver.cpp`, including the channel-lifecycle,
spatialization, configuration and save/restore calls. Representative call
sites: `SND_StartAlias2DSample` (`snd_driver.cpp:351`),
`SND_StartAlias3DSample` (`:550`), `SND_StartAliasStreamOnChannel` (`:795`),
`SND_Update2DChannel` (`:1659`), `SND_Update3DChannel` (`:1712`),
`SND_UpdateStreamChannel` (`:1787`), `SND_SetRoomtype` (`:996`),
`SND_Update2DChannelReverb` (`:1416`), `SND_Update3DChannelReverb` (`:1432`),
`SND_UpdateStreamChannelReverb` (`:1448`),
`SND_Set{2D,3D,Stream}ChannelPlaybackRate` (`:1345`,`:1371`,`:1400`),
`SND_Set{2D,3D,Stream}ChannelVolume` (`:1227`,`:1259`,`:1306`),
`SND_Get{2D,3D,Stream}ChannelLength` (`:1464`,`:1480`,`:1496`),
`SND_Stop/Pause/Unpause{2D,3D,Stream}Channel`, and
`SND_ApplyChannelMap` (`:300`) / `SND_Apply3DSpatializationTweaks` (`:500`).

### 2.2 Voice

| Subject | State at this SHA | Evidence |
|---|---|---|
| Codec | In-tree **Speex 1.1.9** (`SPEEX_VERSION "speex-1.1.9"`), compiled from `src/groupvoice/speex/`, headers in `deps/speex/`. This is the fork's observed build; whether retail/Steam peers used this version or bitstream is not established here (see §2.2 paragraph and VOX-8) | `src/groupvoice/speex/misc.h:38-43` |
| Encoder/decoder | `Encode_Init` selects narrowband/wideband/ultra-wideband Speex modes; `Encode_Sample` uses `speex_encode_int`/`speex_bits_write`; `Decode_*` uses `speex_bits_read_from`/`speex_decode_int` | `src/groupvoice/encode.cpp`, `decode.cpp` |
| Capture/playback device layer | DirectSound-based `record_dsound.cpp`, `play_dsound.cpp`, `directsound.h`; Windows mixer/waveIn plumbing in `win_voice.cpp` | `src/groupvoice/`, `src/win32/win_voice.cpp` |
| Client→server send (in-game and pre-game), as emitted here | Out-of-band `"v"` message: `MSG_WriteString("v")`, `MSG_WriteShort(qport)`, packet-count byte, then per packet a **one-byte** size field + payload. The send-side `MyAssertHandler` guards are the literal predicates `dataSize > 0` and `dataSize < (2<<15)`; because `MSG_WriteByte` transmits only the low 8 bits, the wire size field is `dataSize & 0xFF`, so a sender length ≥ 256 is malformed (truncated on the wire) and is **not** a valid range | `src/client_mp/cl_voice.cpp:44-57` |
| Client→server receive, in-game (`SV_UserVoice`) | Packet-count byte, then per packet a **one-byte** size field (`MSG_ReadByte`, representable 0..255) + payload; no `talker` byte (sender resolved by address+qport). Accept predicate `dataSize <= 0 \|\| dataSize > 256`; the `> 256` arm is a literal defensive predicate a byte can never satisfy, so the *effective* accepted positive range is **1..255**. A size byte of `0` logs "invalid voice packet" and aborts the batch; a truncated read returns `-1` (buffer overflow flag) and is likewise rejected; size `256` is not representable on the wire | `src/server_mp/sv_voice_mp.cpp:104-119` |
| Client→server receive, pre-game (`SV_PreGameUserVoice`) | Packet-count byte, then per packet a **two-byte (16-bit)** size field (`MSG_ReadShort`) + payload. Same accept predicate `dataSize <= 0 \|\| dataSize > 256`, so the accepted range here is **1..256**; `0`, negative/truncated reads and values above 256 are rejected | `src/server_mp/sv_voice_mp.cpp:161-169` |
| Server→client send (`SV_WriteVoiceDataToClient`) | Out-of-band `"v"` message: packet-count byte (asserted `>0` and `<= 40`), then per packet a `talker` byte + **one-byte** size field (`MSG_WriteByte`) + payload. `dataSize < (2<<15)` is asserted but only the low byte reaches the wire | `src/server_mp/sv_snapshot_mp.cpp:1872-1896`, `:1916` |
| Client receive of server voice (`CL_VoicePacket`) | Packet-count byte accepted only when `<= 0x28` (40), then per packet a `talker` byte, a **one-byte** size field (`MSG_ReadByte`, representable 0..255) + payload. Accept predicate `dataSize <= 0 \|\| dataSize > 256`; the `> 256` arm is unreachable for a byte, so the effective accepted positive range is **1..255**. `talker >= 0x40` is rejected, and a size byte of `0` (or a truncated/negative read) aborts the batch | `src/client_mp/cl_voice.cpp:65-93` |
| Server relay | `SV_QueueVoicePacket` caps the per-client queue at **40** packets and enforces `talkerNum == (byte)talkerNum`; `G_BroadcastVoice`/`voice_global` gate delivery | `src/server_mp/sv_voice_mp.cpp:11,94-146` |
| Voice lifecycle entry points | `Voice_Init`/`Voice_Shutdown` called from the sound system (`snd.cpp:3699`,`:3940`); `Voice_SendVoiceData`, `Voice_IncomingVoiceData` | `src/win32/win_local.h:176-182`, `win_voice.cpp` |

The **voice wire format is a compatibility contract**, not an internal detail.
What this source inspection establishes is the *fork's own observed framing* at
the recorded SHA — the directional byte layouts tabulated above, and the in-tree
codec identified by its `SPEEX_VERSION` string (`speex-1.1.9`). It does **not**
establish that unmodified original commercial 1.7 or Steam 1.8 peers exchange
exactly these bytes, nor that retail shipped this codec version or bitstream:
no original-reference capture was made here, so the equivalence claim is
expressly **deferred to VOX-8** and must not be recorded as proven retail fact.
The retained design intent is that the codec payload is embedded in these bytes,
so a substituted codec (for example Opus) or changed framing would risk breaking
in-game voice against real clients; that is a risk hypothesis to be tested
against the references, not a certified fact. Directional gates and the
malformed-length cases live in §4.2.

### 2.3 Cinematics

| Subject | State at this SHA | Evidence |
|---|---|---|
| Bink integration | `#ifdef CINEMA` gate; the define is **off** (`// #define CINEMA`) | `src/gfx_d3d/r_cinematic.cpp:18-20` |
| Bink API surface | `BinkOpen`/`BinkOpenPath`/`BinkOpenPath_MemoryResident`, `BinkDoFrame`, `BinkNextFrame`, `BinkWait`, `BinkPause`, `BinkGetRealtime`, `BinkClose`, `BinkSetSoundSystem`+`BinkOpenMiles`, `BinkSetMixBinVolumes`, `BinkSetIOSize`, `BinkControlBackgroundIO`, `BinkBufferSize`, `BinkRegisterFrameBuffers`, `BinkGetFrameBuffersInfo`, custom `Bink_Alloc`/`Bink_Free` and `BinkTexture_PC` decode helpers | `src/gfx_d3d/r_cinematic.cpp` |
| Threading | Playback runs on a dedicated cinematic thread (`R_Cinematic_Thread`, `g_cinematicThreadState`, `CRITSECT_CINEMATIC_TARGET_CHANGE`, `THREAD_OWNER_CINEMATICS`) | `src/gfx_d3d/r_cinematic.cpp:118-185` |
| Bink dependency | 32-bit Windows-only blob/import library + headers | `deps/binklib/{binkw32.dll,binkw32.lib,bink.h,binktextures.cpp}` |

### 2.4 Null / headless media path

| Subject | State at this SHA | Evidence |
|---|---|---|
| Sound data in headless load | `Load_SetSoundData` becomes a no-op under `KISAK_DEDI_HEADLESS`; raw zone payloads stay in memory for typed alias resolution | `src/database/db_load.cpp:2521-2528` |
| Headless runtime ownership | `DB_ClearHeadlessSoundRuntimeData` clears playback allocation so a headless `LoadedSound` owns none | `src/database/db_load.cpp:768`, called at `:2575` |
| Null alias playback | `SND_PlaySoundAlias_Internal` short-circuits aliases whose sound file is null | `src/sound/snd.cpp:1648`, `:2187` |
| Headless scope | `KISAK_DEDI_HEADLESS` is already used across the tree (bgame, database, platform) and `PORTING.md` L1 states headless is the only supported dedi on the four non-Windows targets | `src/**` `#ifdef KISAK_DEDI_HEADLESS`; `PORTING.md` L1 |

### 2.5 Existing test coverage

| Subject | Coverage at this SHA |
|---|---|
| Sound | One focused CMake test, `tests/sound_dry_send_source_test.cmake`; no sound-loader runtime suite and no playback suite (`NATIVE_ASSET_CLOSURE_LEDGER.md` §4.4) |
| Voice | **None** — no encode/decode, framing, or device-lifecycle tests |
| Cinematics | **None** — no Bink decode/seek/A-V tests |
| Null media | No dedicated null-media test |

## 3. Upstream OpenAL (PR #88) selective-reuse audit

Upstream [SwagSoftware/KisakCOD#88](https://github.com/SwagSoftware/KisakCOD/pull/88)
merged an OpenAL implementation while the maintainer's
[#76 discussion](https://github.com/SwagSoftware/KisakCOD/issues/76#issuecomment-5079327704)
still reports reverb, volume, playback and shutdown concerns. The local
reconciliation ledgers (`UPSTREAM_820B0A03_LEDGER.md` row 9, and
`UPSTREAM_C6BE07A2_LEDGER.md` rows 20-23) already defer the restructuring epic
because this tree does not carry the OpenAL-specific surface. This stage does
**not** import that batch.

Disposition, per concern:

| Upstream concern | Local reality at this SHA | Disposition |
|---|---|---|
| OpenAL device/context setup, driver split (`snd_driver` reorganization, `KISAK_SOUND`→`KISAK_OPENAL`) | No `OpenAL`/`alSource`/`alcOpenDevice` exists here; `snd_driver.cpp` is the Miles-backed `SND_*` layer | **Study for shape, do not import wholesale.** The driver split is the useful idea; the code assumes Win32/x86 and an OpenAL Soft that is not vendored here |
| ~17k-line backend/vendor batch | Not present; license/supply-chain review and ownership design not done | **Deferred**; selective reuse only after license and portability review |
| Reverb, attenuation, volume, playback, shutdown defects reported in #76 | Local reverbs live in `SND_Update{2D,3D,Stream}ChannelReverb`; spatialization in `SND_Apply3DSpatializationTweaks`; shutdown in `SND_ShutdownDriver` | **Must be reproduced as explicit defect tests against a chosen backend before reuse is trusted** (gates `AUD-*`) |
| Streaming refill / zone unload | `SND_UpdateStreamChannel`, `SND_StartAliasStreamOnChannel`, zone unload via `DB_FreeUnusedResources` | **Define and test locally** (gates `AUD-*`, `NUL-*`) |

Rule: *an upstream merge is not finished compatibility, and a blind parallel
replacement is not authorized.* Any import is a measured vertical slice behind
the `SND_*`/`Voice_*` contracts with its own tests, or it does not land.

## 4. Parity gates

Each gate below has a stable ID. A gate is "met" only with the evidence class
the row names — source inspection alone never meets a runtime or reference
gate. Until licensed original commercial 1.7 / Steam 1.8 references and
runners are available, every reference gate (AUD-9, VOX-8) stays **blocked**.

Status vocabulary: `source-complete` (inventory done), `not-implemented` (no
backend/tests yet), `pending` (method defined, execution not run), `blocked`
(requires licensed references/runners or a target that cannot link the
dependency).

### 4.1 Audio playback gates

| ID | Gate (requirement) | Production subject | Method / fixture | Evidence required | Status |
|---|---|---|---|---|---|
| AUD-1 | Monotonic playback of PCM sample subranges without reading outside `data_len` | `SND_StartAlias{2D,3D,Stream}*`, `SND_SetData`, `Load_MssSound` | Bounded source payloads + instrumented decode; assert no out-of-range read | Synthetic trace + sanitizer run | pending |
| AUD-2 | Loop and seek semantics preserved (loop points, seek/restart, `SND_ContinueLoopingSound*`) | `snd.cpp` loop path, `SND_Get*ChannelLength` | Deterministic loop/seek fixtures; compare emitted frame timings | Frame-level trace | pending |
| AUD-3 | Pitch/rate changes reproduce the decompiled mapping (no off-by-one in rate units) | `SND_Set{2D,3D,Stream}ChannelPlaybackRate` (`snd_driver.cpp:1345/1371/1400`) | Rate sweep with recorded expected durations | Trace + tolerance policy (Section 5) | pending |
| AUD-4 | Gain/attenuation curves match `SND_Attenuate`/`SND_ChoosePitchAndVolume`/`SND_GetStream3DVolumeFallOff` | `snd_driver.cpp`, `snd_public.h:443` | Curve table comparison over documented radii | Numeric table diff | pending |
| AUD-5 | Reverb/roomtype transitions and cleanup are deterministic and leave no dangling filters | `SND_SetRoomtype` (`:996`), `SND_Update*ChannelReverb` (`:1416/1432/1448`), `AIL_open_filter`/`AIL_find_filter` in `snd_mss.cpp` | Roomtype sequence + shutdown; assert filter/handle counts return to baseline | Handle-leak check + trace | pending |
| AUD-6 | Streaming refill never underflows silently and never overruns the refill budget | `SND_UpdateStreamChannel` (`:1787`), `SND_StartAliasStreamOnChannel` (`:795`) | Streamed fixture with forced slow refill; assert underrun handling | Underrun trace | pending |
| AUD-7 | Zone unload/shutdown frees every playback allocation and leaves no live handles | `SND_ShutdownDriver` (`:65`), `DB_FreeUnusedResources` sweep | Load → play → unload zone → shutdown under leak instrumentation | ASan/leak report | pending |
| AUD-8 | Channel map / speaker configuration applied exactly as specified | `SND_ApplyChannelMap` (`:300`), `AIL_speaker_configuration`/`AIL_set_speaker_configuration` | Channel-count matrix (mono…5.1) against expected down/up-mix | Trace + numeric check | pending |
| AUD-9 | Original-reference audio behavior (timbre/attenuation/looping) matches the unmodified commercial client's expected scene output | Full client playback | Commercial 1.7 and Steam 1.8 reference scenes; documented scene-appropriate tolerances | Reference capture + comparison | **blocked** |

### 4.2 Voice gates

| ID | Gate (requirement) | Production subject | Method / fixture | Evidence required | Status |
|---|---|---|---|---|---|
| VOX-1 | Encode/decode round-trips **Speex 1.1.9** narrowband/wideband/ultra-wideband byte-for-byte for fixed input at a fixed quality | `Encode_Sample`/`Decode_*`, `src/groupvoice/speex/` | Golden bitstream fixtures captured from this build; assert exact bytes | Golden vectors committed with the test | pending |
| VOX-2a | **Client→server in-game** framing byte-exact: out-of-band `"v"` + `qport` short, packet-count byte, then per packet one-byte size + payload; accepted server-side positive sizes **1..255**. Include zero and truncation/error cases; do not treat sender lengths ≥ 256 as valid (they truncate to the low byte on the wire) | `cl_voice.cpp:44-57` (send), `sv_voice_mp.cpp:104-119` (receive) | Serialize known packets and diff against expected bytes; cases: size 0, size 255, sender length ≥256 shown truncated, truncated payload | Byte diff + rejection tests | pending |
| VOX-2b | **Client→server pre-game** framing byte-exact: packet-count byte, then per packet a 16-bit size + payload; accepted sizes **1..256** (0 and >256 rejected) | `sv_voice_mp.cpp:161-169` | Serialize known pre-game packets; cases: size 0, 256, 257, truncated payload | Byte diff + rejection tests | pending |
| VOX-2c | **Server→client** framing byte-exact: out-of-band `"v"`, packet-count byte (`1..40`), then per packet `talker` byte + one-byte size + payload; client accepts positive sizes **1..255** and `talker < 0x40` | `sv_snapshot_mp.cpp:1872-1896`, `cl_voice.cpp:65-93` | Serialize known packets; cases: count 0/41, size 0/255, talker 0x40, truncated payload | Byte diff + rejection tests | pending |
| VOX-3 | Server relay preserves talker byte and payload and enforces the 40-packet cap and byte-talker assertion | `sv_voice_mp.cpp:94-146` | Queue flood + identity-boundary tests | Byte diff + assertion tests | pending |
| VOX-4 | Capture device lifecycle: init, default-device change, removal, re-open, shutdown without leak or crash | `win_voice.cpp` mixer/waveIn, `record_dsound.cpp`, `Voice_Init`/`Voice_Shutdown` (`win_voice.cpp:588/640`) | Device-present / device-absent / device-swap sequences on each client target | Lifecycle trace + leak report | pending |
| VOX-5 | Playback/loss/recovery: missing, late and reordered voice packets degrade gracefully and drop cleanly | `Voice_IncomingVoiceData` (`win_voice.cpp:732`), `DSound_HandleBufferUnderrun` (`play_dsound.cpp:178`) | Loss/reorder/late-arrival injection | Recovery trace | pending |
| VOX-6 | Permission/default-device changes are observed and do not stall the game loop | `win_voice.cpp:100-160` mixer path | Permission-denied and no-device fixtures | Trace + no-hang assertion | pending |
| VOX-7 | **No codec/protocol substitution**: no Opus or new voice framing; wire bytes for valid voice are unchanged | Entire voice path | Source guard + byte-exact wire fixtures | Guard test + fixtures | not-implemented (guard absent) |
| VOX-8 | Two-way voice against **both** reference builds (#122) | Full client | Original 1.7 and Steam 1.8 peers, both directions | Session capture + packet trace | **blocked** |

### 4.3 Cinematic gates

| ID | Gate (requirement) | Production subject | Method / fixture | Evidence required | Status |
|---|---|---|---|---|---|
| CIN-1 | Bink decode reproduces expected frames; no change to source data or pure checksums | `R_Cinematic_UpdateFrame_Core` (`r_cinematic.cpp:185`), `R_Cinematic_StartPlayback*` (`:947/954`), `db_load` cinematic assets | Supported content; frame-level decode compare and checksum comparison | Frame hash comparison | pending |
| CIN-2 | Seek/skip is bounded and deterministic; `R_Cinematic_Advance`/`CinematicHunk_*` state stays consistent | `R_Cinematic_Advance` (`:281`), `CinematicHunk_Reset/Close` (`:455/269`) | Seek/skip sequences, including skip-before-open | State trace | pending |
| CIN-3 | EOF/cancel returns to a clean idle state and stops the backlog (`R_Cinematic_StopPlayback_Now`, `R_CinematicThread_EndBinkAsync`) | `r_cinematic.cpp:445`, `:854` | EOF and mid-play cancel | Shutdown/leak report | pending |
| CIN-4 | A/V synchronization within a documented tolerance; `R_Cinematic_UpdateTimeInMsec`/`BinkGetRealtime` used consistently | `r_cinematic.cpp:409` | Timed playback with audio track; drift measurement | Drift trace + tolerance record | pending |
| CIN-5 | Cinematic hunks/textures closed without leaking on thread shutdown | `CinematicHunk_Close`, `R_Cinematic_ReleaseImages`, `R_Cinematic_Shutdown` (`:936`) | Repeated init/play/shutdown loop | Leak report | pending |
| CIN-6 | Replacement path is target-selectable so macOS/ARM (which lose 32-bit Bink) still links with a defined stub/null | `#ifdef CINEMA`, `deps/binklib` | Build every client target with the stub and without Bink | Per-target build + link evidence | not-implemented |

### 4.4 Null / headless media gates

| ID | Gate (requirement) | Production subject | Method / fixture | Evidence required | Status |
|---|---|---|---|---|---|
| NUL-1 | Headless load parses/owns sound assets and creates **no** playback allocation | `Load_MssSound`, `Load_SetSoundData`, `DB_ClearHeadlessSoundRuntimeData` (`db_load.cpp:2521/2575`) | Load supported maps headless; assert zero device opens and typed alias resolution intact | Headless run + allocation trace | pending |
| NUL-2 | Genuine null media path: null sound-file aliases short-circuit with no device or allocation | `SND_IsNullSoundFile` (`snd.cpp:2187`, `:1648`) | Alias set with null files; assert short-circuit | Call-path trace | pending |
| NUL-3 | Headless never opens an audio device or requires one to start/join/run | `SND_InitDriver` (`:26`), `Voice_Init` under headless | Start/map/session/shutdown headless with no audio hardware | Headless lifecycle run | pending |
| NUL-4 | Each client target validates native media dependencies and runtime behavior (present, absent, downgraded) | Per-target link + runtime | `PORTING.md` target matrix; known absence on macOS/ARM | Per-target dependency manifest + run | not-implemented |

### 4.5 Dependency / packaging gates

| ID | Gate (requirement) | Production subject | Method / fixture | Evidence required | Status |
|---|---|---|---|---|---|
| DEP-1 | Miles and Bink proprietary, 32-bit-only blobs are removed from or replaced before portable release | `deps/msslib`, `deps/binklib` | Packaging audit on each release target | Provenance/SBOM + package contents | not-implemented |
| DEP-2 | Replacement libraries are pinned, license-audited and reproducible (no unpinned downloads) | OpenAL Soft / FFmpeg candidates | Pin source + license review + notices update | Pinned recipe + notices diff | not-implemented |
| DEP-3 | Wire-locked voice codec (Speex 1.1.9) stays in-tree and unmodified; only the device layer changes | `src/groupvoice/speex`, `deps/speex` | Guard: checksum/codec-version assertion | Source guard + version check | not-implemented |

## 5. Relationship to commercial compatibility (#122)

- Media gates **never** override byte or behavioral compatibility. Network
  voice bytes (VOX-2a/2b/2c/VOX-3/VOX-7) and disk asset bytes (CIN-1, NUL-1) are part
  of the mandatory contract; the permissiveness of #122's
  "Test non-network rendering/audio details with suitable tolerances, but
  retain exactness wherever they affect packet contents or network-visible
  behavior." clause ([NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md))
  applies only to rendering/audio detail, **not** to valid wire/disk encodings.
- Tolerance policy for AUD-3/AUD-4/AUD-6/CIN-4 must be recorded explicitly
  (signal, scene, sample rate, window) before results are accepted; a pass with
  no recorded tolerance is not evidence.
- The commercial reference gates (AUD-9, VOX-8, and the retail portions of
  CIN-1/CIN-4) require the pinned original commercial 1.7 and Steam 1.8
  manifests from [#122]/[#127]. They are **blocked**, not waived, until those
  references and licensed runners exist.

## 6. Blockers and explicit non-claims

- **Blocked on licensed references/runners:** AUD-9, VOX-8, and retail
  portions of CIN-1/CIN-4. No available/skipped profile may count as passing.
- **Blocked on target capability:** CIN-6/NUL-4/DEP-1 — Miles and Bink are
  32-bit Windows-only; macOS and ARM cannot link them and need a defined stub
  or replacement.
- **Not implemented:** there is no OpenAL (or other portable) backend, no
  voice device abstraction layer, and no audio/voice/cinematic test suite
  beyond `sound_dry_send_source_test.cmake`.
- **This document does not certify:** any audio/voice/cinematic behavior, any
  original-reference interoperation, any target's media closure, or packaging
  readiness. It defines the gates and the evidence each requires.

## 7. Follow-ups

Bounded implementation stages should be tracked separately from this umbrella,
each with its own PR and final-head CI plus substantive review, for example:

1. AUD: reproduce the #76 defect set (reverb/volume/playback/shutdown) as
   failing tests before selecting/among backend candidates.
2. VOX: byte-exact in-tree Speex 1.1.9 and directional framing fixtures
   (VOX-1/2a/2b/2c/3) — independent of any device backend and runnable now;
   retail-equivalence assertions stay with VOX-8.
3. CIN: define and build the target-selectable cinematic stub (CIN-6) so
   non-Windows targets link.
4. NUL: a headless media lifecycle test proving NUL-1/2/3.

None of these may close [#132](https://github.com/jm2/kisakcod/issues/132),
which retains its full original acceptance; helper-only coverage cannot claim a
milestone complete.

## 8. References

- [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) — mandatory
  commercial 1.7 / Steam 1.8 contract.
- [PORTING.md](PORTING.md) — Miles→OpenAL Soft and Bink→FFmpeg plan, target
  matrix, dependency dispositions.
- [NATIVE_ASSET_CLOSURE_LEDGER.md](NATIVE_ASSET_CLOSURE_LEDGER.md) §4.4 —
  sound family disk schema, ownership and current test evidence.
- [DVAR_SERVER_COMMAND_AUDIT.md](DVAR_SERVER_COMMAND_AUDIT.md) — companion
  audit/definition document style and evidence boundary.
- [ROADMAP_EXPANSION_PROPOSAL.md](ROADMAP_EXPANSION_PROPOSAL.md) — A10 scope
  and upstream crosswalk (#75/#76/#88).
- [task.md](task.md) — A10 tracking row and next-checkpoint framing.
