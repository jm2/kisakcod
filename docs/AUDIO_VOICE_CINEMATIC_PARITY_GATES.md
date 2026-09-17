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
| Codec | In-tree **Speex 1.1.9** (`SPEEX_VERSION "speex-1.1.9"`), compiled from `src/groupvoice/speex/`, headers in `deps/speex/`. This is the fork's observed build; whether retail/Steam peers used this version or bitstream is not established here, so retention of this build is conditional on reference evidence, not a source-version freeze (see §2.2 paragraph, DEP-3 and VOX-8) | `src/groupvoice/speex/misc.h:38-43` |
| Encoder/decoder | `Encode_Init` selects narrowband/wideband/ultra-wideband Speex modes; `Encode_Sample` uses `speex_encode_int`/`speex_bits_write`; `Decode_Sample` uses `speex_bits_read_from`/`speex_decode` (float output truncated to `int16`) | `src/groupvoice/encode.cpp`, `decode.cpp` |
| Capture/playback device layer | DirectSound-based `record_dsound.cpp`, `play_dsound.cpp`, `directsound.h`; Windows mixer/waveIn plumbing in `win_voice.cpp` | `src/groupvoice/`, `src/win32/win_voice.cpp` |
| Client→server send — the fork's **only** client voice writer (`CL_WriteVoicePacket`, reached only via `CL_VoiceTransmit`) | Out-of-band `"v"` message: `MSG_WriteString("v")`, `MSG_WriteShort(qport)`, packet-count byte, then per packet a **one-byte** size field + payload. `CL_WriteVoicePacket`'s local guard accepts client `connectionState` `CA_ACTIVE` (9), `CA_LOADING` (7) or `CA_PRIMED` (8), but every effective caller is gated upstream by `Voice_SendVoiceData` (`win_voice.cpp:86-93`), which requires `CA_ACTIVE`; no reachable path transmits while `CA_LOADING`/`CA_PRIMED` (see the reachability audit below). The `MyAssertHandler` guards are the literal predicates `dataSize > 0` and `dataSize < (2<<15)`; because `MSG_WriteByte` transmits only the low 8 bits, the wire size field is `dataSize & 0xFF`, so a sender length ≥ 256 is malformed (truncated on the wire) and is **not** a valid range | `src/client_mp/cl_voice.cpp:38-57`, `cl_main_mp.cpp:1978-1998`, effective callers `src/win32/win_voice.cpp:86-93,445-565`, enum `client_mp.h:328-340` |
| Server dispatch of the client `"v"` out-of-band packet | `SV_VoicePacket` reads `qport` (`MSG_ReadShort`), resolves the client by address, ignores `header.state == CS_ZOMBIE` (1), routes `header.state >= CS_ACTIVE` (4) to `SV_UserVoice` (in-game) and `header.state` 2..3 (`CS_CONNECTED`/`CS_CLIENTLOADING`) to `SV_PreGameUserVoice` | `src/server_mp/sv_main_pc_mp.cpp:314-334`, `sv_main_mp.cpp:697-700`, state enum `server_mp.h:12-19` |
| Client→server receive, in-game (`SV_UserVoice`) | Packet-count byte, then per packet a **one-byte** size field (`MSG_ReadByte`, representable 0..255) + payload; no `talker` byte (sender resolved by address+qport). Accept predicate `dataSize <= 0 \|\| dataSize > 256`; the `> 256` arm is a literal defensive predicate a byte can never satisfy, so the *effective* accepted positive range is **1..255**. A size byte of `0` logs "invalid voice packet" and aborts the batch; a truncated read returns `-1` (buffer overflow flag) and is likewise rejected; size `256` is not representable on the wire | `src/server_mp/sv_voice_mp.cpp:104-119` |
| Client→server receive, pre-game (`SV_PreGameUserVoice`) | Packet-count byte, then per packet a **two-byte (16-bit)** size field (`MSG_ReadShort`) + payload. Same accept predicate `dataSize <= 0 \|\| dataSize > 256`, so the reader's accepted range is **1..256**; a missing or truncated **size-field** read returns a non-positive value and is rejected, as are `0` and values above `256`. A **present valid size with a truncated payload is not rejected**: when a declared payload read exceeds the message's `cursize + splitSize`, `MSG_ReadData` (`msg_mp.cpp:561ff`, overflow branch :572-579 at the recorded SHA) sets `msg->overflowed` and `memset`s the **entire declared-length destination to 0xFF** — any available prefix bytes are not preserved (declared length 3 with one available byte `0x12` forwards `FF FF FF`, not `12 FF FF`) — and this reader never checks `overflowed` before queueing, so that all-0xFF declared-length payload is forwarded (existing behavior; any hardening is separate production work). **This 16-bit reader is unmatched in this fork:** no effective fork client path emits a 16-bit pre-game size (every reachable transmit path is `CA_ACTIVE`-gated and one-byte — see the reachability audit below), so its origin is unknown from this tree and it cannot be presented as a proven working pre-game exchange — see the unresolved finding below | `src/server_mp/sv_voice_mp.cpp:161-169`, `:174-182` (unchecked `overflowed`), dispatch at `sv_main_pc_mp.cpp:330-332` |
| Server→client send (`SV_WriteVoiceDataToClient`) | Out-of-band `"v"` message: packet-count byte (asserted `>0` and `<= 40`), then per packet a `talker` byte + **one-byte** size field (`MSG_WriteByte`) + payload. `dataSize < (2<<15)` is asserted but only the low byte reaches the wire | `src/server_mp/sv_snapshot_mp.cpp:1872-1896`, `:1916` |
| Client receive of server voice (`CL_VoicePacket`) | Packet-count byte accepted only when `<= 0x28` (40), then per packet a `talker` byte, a **one-byte** size field (`MSG_ReadByte`, representable 0..255) + payload. Accept predicate `dataSize <= 0 \|\| dataSize > 256`; the `> 256` arm is unreachable for a byte, so the effective accepted positive range is **1..255**. `talker >= 0x40` is rejected, and a size byte of `0` (or a truncated/negative read) aborts the batch | `src/client_mp/cl_voice.cpp:65-93` |
| Server relay | `SV_QueueVoicePacket` caps the per-client queue at **40** packets (`voicePacketCount < 40` — plain control flow, enforced in every build) and guards its `talkerNum`/`clientNum` arguments with four separate `MyAssertHandler` bounds assertions (`:129-136`). Those assertions are **non-enforcing diagnostics in a normal Release build**: `MyAssertHandler`'s body compiles empty unless `KISAK_PURE` or `USE_ASSERTS` is defined (`assertive.cpp:643-691`; neither is defined by any build script here), so out-of-range arguments are not trapped in production — the bound holds through valid production callers only. The stored talker is the `uint8_t` field of `VoicePacket_t`, filled by implicit narrowing and written to clients with `MSG_WriteByte`. The byte-range assertion whose diagnostic string reads `"talkerNum == static_cast<byte>(talkerNum)"` is **not enforced**: its guard predicate is the self-comparison `if (talkerNum != talkerNum)`, always false, so the handler body is unreachable (see the dead-assertion finding below). `G_BroadcastVoice`/`voice_global` gate delivery | `src/server_mp/sv_voice_mp.cpp:11-58` (delivery gate), `:125-146` (queue/bounds), struct `g_client_public_mp.h:206-211`, writer `sv_snapshot_mp.cpp:1885-1894`, `src/universal/assertive.cpp:643-691` |
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

**Reachability audit of the client sender (removes the claimed pre-game
overlap).** The fork's only client voice writer is `CL_WriteVoicePacket`
(`cl_voice.cpp:13-63`), reached only through `CL_VoiceTransmit`
(`cl_main_mp.cpp:1978-1998`). `CL_WriteVoicePacket` writes a **one-byte** size
field with `MSG_WriteByte` (`cl_voice.cpp:53`), and its own guard additionally
accepts `CA_LOADING`/`CA_PRIMED` (`cl_voice.cpp:38-41`). That broader arm is a
**local writer guard, not an effective caller state**: `CL_VoiceTransmit` has
exactly two call sites, both inside `Record_QueueAudioDataForEncoding`
(`win_voice.cpp`): line 451 (via `Client_SendVoiceData`, after `Encode_Sample`
produced a frame) and line 562 (flush when the talk key is not held); and
`Client_SendVoiceData` (`win_voice.cpp:445-454`) has no other caller. Both call
sites are downstream of the `Voice_SendVoiceData()` gate at
`win_voice.cpp:482-483`, which returns early unless
`clientUIActives[0].connectionState == CA_ACTIVE` (`win_voice.cpp:86-93`). On
every path this tree can actually reach, the client therefore transmits only
while `CA_ACTIVE`, which the server routes to the one-byte `SV_UserVoice`. The
previously recorded claim that the `CA_LOADING`/`CA_PRIMED` client window
overlaps the server-side `CS_CLIENTLOADING` state is **not established** by any
reachable caller and is withdrawn.

**Unmatched pre-game reader format (unresolved source finding).** The server
dispatches the received `"v"` out-of-band packet by its own state machine:
`SV_VoicePacket` routes `header.state >= CS_ACTIVE` (4) to `SV_UserVoice`
(one-byte size, matches the reachable sender) and `header.state`
`CS_CONNECTED` (2) / `CS_CLIENTLOADING` (3) to `SV_PreGameUserVoice`, which
reads a **two-byte** size (`MSG_ReadShort`, `sv_voice_mp.cpp:164`). Source
inspection found **no** effective fork client path that emits a 16-bit pre-game
size, so this reader format is **unmatched in this fork** and its origin (a
different/legacy client, or a path not present in this tree) is unknown from
the source at this SHA. It is recorded as an **unresolved source finding**, not
a reachable mismatch and not a production defect, and no speculative production
change is authorized by this audit. The format must be resolved against the
pinned original commercial 1.7 / Steam 1.8 references before any pre-game voice
gate (VOX-2b) is claimed as met.

**Dead byte-range talker assertion (corrected source finding).** An earlier
revision of the table above recorded that `SV_QueueVoicePacket` "enforces
`talkerNum == (byte)talkerNum`". Direct inspection at the recorded SHA corrects
that: the intended byte-range check is **not enforced**. The guard at
`sv_voice_mp.cpp:142` is the self-comparison `if (talkerNum != talkerNum)`,
which is always false, so its `MyAssertHandler` body (`:143`) is unreachable.
The only surviving record of the intent is that handler's diagnostic string
`"talkerNum == static_cast<byte>(talkerNum)"`, whose `149` line label is the
decompiled source line, not the current file line. The four concerns are
distinct and are recorded separately:

- **Actual predicate:** `talkerNum != talkerNum` (always false). No byte-range
  assertion executes.
- **Diagnostic intent:** the string and its line label record the author's
  intended postcondition — that the talker index fit in a byte — which the
  predicate does not implement.
- **Reachable talker constraints:** `SV_QueueVoicePacket` has exactly two call
  paths. `G_BroadcastVoice` (`sv_voice_mp.cpp:52`) passes `talker->s.number`,
  and `SV_PreGameUserVoice` (`:182`) passes `talker = cl - svs.clients`. Both
  are `[0, sv_maxclients)` client/entity indices, guarded by the four separate
  assertions at `:129-136` — which are **non-enforcing in a normal Release
  build** (`MyAssertHandler`'s body compiles empty without `KISAK_PURE` or
  `USE_ASSERTS`, `assertive.cpp:643-691`), so the bound holds because both
  reachable callers pass valid indices, not because the asserts trap — and,
  upstream, by the `< 0x40` asserts in `SV_ClientHasClientMuted`. The stored
  field is `uint8_t`
  (`VoicePacket_t.talker`, `g_client_public_mp.h:208`), so even an out-of-byte
  value would narrow silently rather than trap. On every path this tree can
  reach `talkerNum < 256` holds, so the dead predicate is an inventory and
  hardening gap, **not** a reachable defect.
- **Future hardening:** making the assertion effective would be a production
  change. It is **not** authorized or required by this docs audit and needs its
  own bounded change, tests and review (§7 item 5). This audit records the gap
  only and changes no production code.

### 2.3 Cinematics

| Subject | State at this SHA | Evidence |
|---|---|---|
| Bink integration | `#ifdef CINEMA` gate. The in-file default is a commented define (`// #define CINEMA`), but both configured client targets define **CINEMA on** via `target_compile_definitions(... PUBLIC ... CINEMA USE_SEPARATE_BLIT_TEXTURE)`, so Bink is the **active production cinematic baseline** on those targets, not dead code (see the corrected finding below the table) | `src/gfx_d3d/r_cinematic.cpp:18-20`, `scripts/mp/CMakeLists.txt:68`, `scripts/sp/CMakeLists.txt:60` |
| Bink API surface | `BinkOpen`/`BinkOpenPath`/`BinkOpenPath_MemoryResident`, `BinkDoFrame`, `BinkNextFrame`, `BinkWait`, `BinkPause`, `BinkGetRealtime`, `BinkClose`, `BinkSetSoundSystem`+`BinkOpenMiles`, `BinkSetMixBinVolumes`, `BinkSetIOSize`, `BinkControlBackgroundIO`, `BinkBufferSize`, `BinkRegisterFrameBuffers`, `BinkGetFrameBuffersInfo`, custom `Bink_Alloc`/`Bink_Free` and `BinkTexture_PC` decode helpers | `src/gfx_d3d/r_cinematic.cpp` |
| Threading | Playback runs on a dedicated cinematic thread (`R_Cinematic_Thread`, `g_cinematicThreadState`, `CRITSECT_CINEMATIC_TARGET_CHANGE`, `THREAD_OWNER_CINEMATICS`) | `src/gfx_d3d/r_cinematic.cpp:118-185` |
| Bink dependency | 32-bit Windows-only blob/import library + headers | `deps/binklib/{binkw32.dll,binkw32.lib,bink.h,binktextures.cpp}` |

**Active CINEMA baseline (corrected source finding).** An earlier revision of
the table above recorded the `CINEMA` gate as **off**, based on the commented
in-file define alone. The build configuration contradicts that:
`scripts/mp/CMakeLists.txt:68` and `scripts/sp/CMakeLists.txt:60` both define
`CINEMA` (together with `USE_SEPARATE_BLIT_TEXTURE`, satisfying the `#error`
guard at `r_cinematic.cpp:22-24`) on the configured MP and SP client targets.
The Bink decode path in `r_cinematic.cpp` is therefore compiled into every
configured client target: the cinematic gates CIN-1..CIN-5 in §4.3 govern
**live production behavior**, and CIN-6's target-selectable stub is required
because that active baseline cannot link on targets that lose the 32-bit
`deps/binklib` blob (macOS/ARM).

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
| VOX-1a | **Encoder bitstream** is byte-identical to pinned golden vectors: fixed PCM input at a fixed sampling rate/quality, for narrowband/wideband/ultra-wideband, reproduces the exact bytes `Encode_Sample` emits | `Encode_Init`/`Encode_SetOptions`/`Encode_Sample`, `src/groupvoice/speex/` | Pinned PCM input frames at fixed rate/quality; compare `Encode_Sample` output bytes exactly | Golden encoded bitstreams committed with the test; provenance = generated from this in-tree Speex 1.1.9 build at the recorded SHA (never retail/captured) | pending |
| VOX-1b | **Decoder output** for the VOX-1a encoded vectors matches pinned decoder PCM within a recorded tolerance. It is **not** asserted byte-equal to the original PCM input, nor exact against retail (a lossy codec does not reproduce source PCM bit-exactly) | `Decode_Init`/`Decode_Sample` | Same encoded vectors; compare decoded `int16` PCM (`speex_decode` output truncated at `decode.cpp:78-79`) against pinned `Decode_Sample` output with an explicit sample-level tolerance (Section 5) | Pinned decoded-PCM reference + tolerance record | pending |
| VOX-2a | **Client→server in-game** framing byte-exact: out-of-band `"v"` + `qport` short, packet-count byte, then per packet one-byte size + payload; accepted server-side positive sizes **1..255**. Include zero and truncation/error cases; do not treat sender lengths ≥ 256 as valid (they truncate to the low byte on the wire). The writer is the fork's only sender, and its broader `CA_LOADING`/`CA_PRIMED` guard arms are not reachable through any effective caller (`Voice_SendVoiceData` requires `CA_ACTIVE`), so this one-byte format is the only reachable client→server width | `cl_voice.cpp:38-57` (send), effective callers `win_voice.cpp:86-93,445-565`, `sv_voice_mp.cpp:104-119` (receive) | Serialize known packets and diff against expected bytes; cases: size 0, size 255, sender length ≥256 shown truncated, truncated payload (a present valid size with a truncated payload is **forwarded**, not rejected: the `MSG_ReadData` overflow branch sets `msg->overflowed` and 0xFF-fills the **entire declared-length destination**, available prefix bytes included — `SV_UserVoice` never checks `msg->overflowed`; the fixture must include a **partially available payload**, e.g. declared size 3 with one available byte `0x12` expected to forward `FF FF FF`, not `12 FF FF`, so byte-exact expectations cannot encode prefix-preserving behavior) | Byte diff + rejection tests | pending |
| VOX-2b | **Client→server pre-game reader format**: packet-count byte, then per packet a 16-bit size (`MSG_ReadShort`) + payload; reader-accepted sizes **1..256** (missing/truncated size-field reads, `0` and >256 rejected). **Unmatched/unknown format in this fork** — no effective client path emits a 16-bit pre-game size (see the §2.2 reachability audit), so its origin is unknown from this tree; this is an unresolved source finding and a fork-to-fork round-trip is not valid proof | `sv_voice_mp.cpp:161-169`, `:174-182` (unchecked `overflowed`), dispatch `sv_main_pc_mp.cpp:330-332` | Reader rejection cases: raw 16-bit sizes 0, 256, 257 and a missing/truncated size field. A **present valid size (1..256) with a truncated payload is forwarded, not rejected**: when a declared payload read exceeds `cursize + splitSize`, `MSG_ReadData` (`msg_mp.cpp:561ff`) sets `overflowed` and 0xFF-fills the **entire declared-length destination** (available prefix bytes are not preserved — declared length 3 with one available byte `0x12` forwards `FF FF FF`, not `12 FF FF`), and `SV_PreGameUserVoice` (`sv_voice_mp.cpp:174-182`) never checks `overflowed` before queueing — fixtures must include a **partially available payload** pinning that all-0xFF forwarding byte-exactly as existing behavior (hardening would be separate production work). Any writer fixture requires recorded provenance and must not assert a one-byte writer / 16-bit reader pair as a working exchange | Reader rejection tests (size-field cases) + truncated-payload forwarding record + explicit unresolved-finding record | pending |
| VOX-2c | **Server→client** framing byte-exact: out-of-band `"v"`, packet-count byte (`1..40`), then per packet `talker` byte + one-byte size + payload; client accepts positive sizes **1..255** and `talker < 0x40` | `sv_snapshot_mp.cpp:1872-1896`, `cl_voice.cpp:65-93` | Serialize known packets; cases: count 0/41, size 0/255, talker 0x40, truncated payload (a present valid size with a truncated payload is **forwarded**, not rejected: the `MSG_ReadData` overflow branch sets `msg->overflowed` and 0xFF-fills the **entire declared-length destination**, available prefix bytes included — `CL_VoicePacket` never checks `msg->overflowed`; the fixture must include a **partially available payload**, e.g. declared size 3 with one available byte `0x12` expected to forward `FF FF FF`, not `12 FF FF`, so byte-exact expectations cannot encode prefix-preserving behavior) | Byte diff + rejection tests | pending |
| VOX-3 | Server relay preserves the stored talker byte and payload byte-exactly and enforces the **40-packet cap** (plain control flow, all builds). The reachable `[0, sv_maxclients)` `talkerNum`/`clientNum` bounds exist only as `MyAssertHandler` assertions (`:129-136`) that are **non-enforcing in a normal Release build** — `assertive.cpp:643-691` compiles the body empty unless `KISAK_PURE` or `USE_ASSERTS` is defined, so out-of-range arguments are not trapped in production. It does **not** enforce a byte-range talker assertion: the intended check is a dead self-comparison (`if (talkerNum != talkerNum)`, `sv_voice_mp.cpp:142`) whose diagnostic string merely records the intent (see §2.2 dead-assertion finding). Hardening any of these predicates is a separate production change, not part of this gate | `sv_voice_mp.cpp:125-146` (queue/bounds), `:142-144` (inert byte assert + narrowing store), `sv_snapshot_mp.cpp:1885-1894` (byte writer), `g_client_public_mp.h:206-211` (`uint8_t talker`), `assertive.cpp:643-691` (empty body in normal Release) | Queue flood (cap 40 — enforced control flow) + identity-boundary cases (`0`, `sv_maxclients`−1, `sv_maxclients`, negative) run in an **assertion-intercepting build** (define `KISAK_PURE`/`USE_ASSERTS` or stub `MyAssertHandler`); in a normal Release build those asserts are non-enforcing diagnostics, so record invalid-argument inputs as untrapped there and restrict bounds coverage to valid production callers `[0, sv_maxclients)`; record that no byte-range assertion fires | Byte diff + cap/rejection tests + assertion-build bound record + inert-predicate record | pending |
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
| DEP-3 | Voice codec implementation is preserved **conditional on authentic reference evidence**: the observed in-tree Speex 1.1.9 build (`src/groupvoice/speex`, `deps/speex`) is to remain as implemented *until* a recorded original commercial 1.7 / Steam 1.8 reference establishes the required wire codec/profile; mandatory commercial wire behavior governs any evidence-backed correction, not an unverified source-version identity | `src/groupvoice/speex`, `deps/speex` | Guard: codec-version/checksum assertion **plus** a recorded reference-evidence decision before any codec change | Source guard + version check + reference record | not-implemented |

## 5. Relationship to commercial compatibility (#122)

- Media gates **never** override byte or behavioral compatibility. Network
  voice bytes (VOX-2a/2b/2c/VOX-3/VOX-7) and disk asset bytes (CIN-1, NUL-1) are part
  of the mandatory contract; the permissiveness of #122's
  "Test non-network rendering/audio details with suitable tolerances, but
  retain exactness wherever they affect packet contents or network-visible
  behavior." clause ([NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md))
  applies only to rendering/audio detail, **not** to valid wire/disk encodings.
- Codec preservation (DEP-3) is conditional, not a source-version freeze: the
  in-tree Speex 1.1.9 build is retained as implemented until a pinned commercial
  reference establishes the required wire codec/profile. Mandatory commercial
  wire behavior governs any evidence-backed correction.
- Tolerance policy for AUD-3/AUD-4/AUD-6/CIN-4/VOX-1b must be recorded
  explicitly (signal, scene, sample rate, window; for VOX-1b the PCM comparison
  method and per-sample tolerance) before results are accepted; a pass with
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
- **Unresolved source finding:** the server's pre-game client→server reader
  (`SV_PreGameUserVoice`, two-byte `MSG_ReadShort` size) is **unmatched in this
  tree** — no effective fork client path emits a 16-bit pre-game size; every
  reachable transmit path is `CA_ACTIVE`-gated and writes a one-byte size (§2.2
  reachability audit). The reader format is unmatched/unknown, not a proven
  exchange and not a reachable mismatch. It is not fixed here and must be
  resolved against the pinned commercial references before VOX-2b is claimed;
  no speculative production change is inferred.
- **Corrected source finding (no live defect):** the server relay's intended
  byte-range talker assertion is inert — its predicate is the always-false
  `talkerNum != talkerNum` (`sv_voice_mp.cpp:142`) while only the diagnostic
  string records the intent. Reachable callers pass valid `[0, sv_maxclients)`
  indices and the stored field is `uint8_t`, so no reachable path violates byte
  range; the four bounds assertions themselves are **non-enforcing in a normal
  Release build** (`MyAssertHandler`'s body compiles empty without
  `KISAK_PURE`/`USE_ASSERTS`, `assertive.cpp:643-691`), so they are
  diagnostics, not production enforcement. This is an inventory correction,
  not a production defect and not a basis for a code change (§2.2). Hardening
  any of these predicates is out of scope for this docs audit.
- **This document does not certify:** any audio/voice/cinematic behavior, any
  original-reference interoperation, any target's media closure, or packaging
  readiness. It defines the gates and the evidence each requires.

## 7. Follow-ups

Bounded implementation stages should be tracked separately from this umbrella,
each with its own PR and final-head CI plus substantive review, for example:

1. AUD: reproduce the #76 defect set (reverb/volume/playback/shutdown) as
   failing tests before selecting/among backend candidates.
2. VOX: pinned in-tree Speex 1.1.9 encoder-bitstream vectors (VOX-1a), pinned
   decoder PCM with an explicit tolerance (VOX-1b), and directional framing
   fixtures (VOX-2a/2c/3) — independent of any device backend and runnable now.
   The VOX-2b pre-game reader format is unmatched in this fork and its origin is
   unknown from source, so it must be resolved against the pinned references
   first; retail-equivalence assertions stay with VOX-8.
3. CIN: define and build the target-selectable cinematic stub (CIN-6) so
   non-Windows targets link.
4. NUL: a headless media lifecycle test proving NUL-1/2/3.
5. VOX hardening (optional, separate bounded change): make the relay's intended
   byte-range talker assertion effective (`sv_voice_mp.cpp:142`). This is a
   production change and must not be folded into this docs stage; it needs its
   own PR, tests and review.

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
