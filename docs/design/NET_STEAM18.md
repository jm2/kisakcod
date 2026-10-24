# NET_STEAM18: Steam 1.8 wire compatibility

Workstream WS-5 · Gates G4a, G4b · Decisions [ADR-0001](../decisions/0001-network-reference-steam-1-8.md), [ADR-0002](../decisions/0002-punkbuster-out.md), [ADR-0004](../decisions/0004-reverse-engineering-scope.md), [ADR-0005](../decisions/0005-cod4x-compatibility.md) · Queue: [NOW](../NOW.md)

The only network reference is the original Steam release `1.8.13620`. The release gate needs both directions: a KisakCOD client on an unmodified Steam 1.8 server, and an unmodified Steam 1.8 client on a KisakCOD server. PunkBuster is out.

## 1. What the fork speaks today

| Item | Fork today | Where |
| --- | --- | --- |
| Code base | CoD4 **v1.0** PC decompile. `version` is `CoD4 MP 1.0 build …`, `shortversion` is `1.0`; the original build 13620 is noted in `buildnumber.cpp` | `common.cpp`, `buildnumber.cpp` |
| Protocol | The `net_wireProfile` dvar picks it (§7): `7` under `steam18`, the default; `1` under `fork`. `gamename` and `shortversion` follow the profile too | `qcommon/wire_profile.cpp`; the `protocol` dvar, `SVC_Info`, `SV_DirectConnect`, `CL_CheckForResend` and `CL_ServerInfoPacket` read it |
| Challenge request | `getchallenge 0 "<base64 Steam ticket>" "<SteamID64>"`, or without Steam `getchallenge 0 "" "<cl_guid>"` | `CL_CheckForResend` |
| Challenge handling | Under `steam18`, a stock request's CD-key MD5 is the client's identity and ban key (§8, option A). `fork` requires the third argument. It replies `challengeResponse <challenge>` at once | `sv_client_mp.cpp` |
| Authorize / master | The `SV_AuthorizeRequest` call in the challenge path and `SV_MasterHeartbeat` are commented out. `masterServerName` still defaults to `cod4master.activision.com` | `sv_client_mp.cpp`, `sv_main_pc_mp.cpp`, `common.cpp` |
| PunkBuster | Every `Pb*` hook is commented out and stays dead ([ADR-0002](../decisions/0002-punkbuster-out.md)). `getinfo` still sends a `pb` key from `sv_punkbuster` | `sv_main_mp.cpp` and others |

**Result:** under `steam18` the server and client speak the community-sourced 1.8 identity, but no stock 1.8 peer has joined yet. That's NOW row 36, which needs the owner's captures (§5).

## 2. Stock handshake (1.0 and 1.7)

1. Client → server: `getchallenge <n> "<md5(cdkey)>"`.
2. Server → Activision authorize service: `getIpAuthorize …` (PB-tagged).
3. Server → client: `challengeResponse`.
4. Client → server: `connect "<userinfo with protocol, challenge, qport>"`.

## 3. In-band formats

We compared the in-tree tables against two 1.7-derived server sources (CoD4X17a_testing, CoD4x_Server). These match exactly:

| Surface | In-tree |
| --- | --- |
| Entity netField tables (field, bits, hint) | 13 tables, listed in `s_netFieldList` (`sv_msg_write_mp.cpp`) and `server_mp.h` |
| `playerStateFields` / `clientStateFields` / `hudElemFields` / `objectiveFields` | 141 / 24 / 40 / 6 |
| Huffman weights | 256 in `msg_hData` (`msg_huffman_data.h`) |
| usercmd delta, snapshot header, configstring layout | `msg_mp.cpp`, `sv_msg_write_mp.cpp` |

Only `archivedEntityFields` (69) differs, and that table is server-internal. This comparison is against 1.7 sources, not 1.8 bytes. Whether protocol 7 changed any in-band format is unknown until capture.

**Huffman tie-break.** `msg_hData` has two equal-weight pairs (symbols 155/205 and 228/231). Retail resolves them with the MSVC CRT `qsort`; `nodeCmp` is a total order that reproduces that code book on every host ([DETERMINISM](DETERMINISM.md)). The Steam 1.8 captures confirm it at G4a.

**`getinfo` keys.**

| Key | Fork (1.0) | 1.7-era servers |
| --- | --- | --- |
| `kc` | sent | replaced by `ki` |
| `hw` | `2` dedicated, `6` listen | `1` |
| `hc`, `od`, `build`, `shortversion` | absent | sent |

## 4. Steam 1.8: known and unknown

These points are community-sourced; confirm each by capture:

- An exe-only update on 2018-04-26/27 displayed `1.6.13620`. On 2018-05-03 it became `1.8.13620`.
- A 1.8 client on a 1.7 server gets "Server uses different protocol version:6, you use protocol version:7".
- Putting the 1.7 exe back restores 1.7 play, so the data files are unchanged.
- The last official Linux dedicated server was 1.7.1 (2011).

| Unknown | Settled by |
| --- | --- |
| CD-key or Steam auth; whether PB was removed | Capture of `getchallenge` / `connect` |
| Any in-band format change | Decode and diff (§6) |
| Whether the Activision master and authorize services answer (the master name still resolves to a Demonware host) | Capture on a routed network |
| Whether 1.8 runs `+set dedicated` | Owner test |

## 5. Capture procedure (owner)

1. Hash `iw3mp.exe` and record the Steam depot manifest. Done for the reference build: app 7940 public build 2737681 (built 2018-05-01); `iw3mp.exe` 1.8, 3,297,280 bytes, SHA-256 b8e1a9731fd1213aaed0b5a779f12cfd867ee9cf567c957032afa99292e3aacf, no SteamStub; Windows depots 7941 (manifest 485990305878906444), 7942 (645298708590566320), 7967 (6005281766938853887); English depots 7944, 7945, 7946, 7962.
2. On an isolated LAN, capture a 1.8 server with a 1.8 client:
   - `getinfo` / `getstatus`
   - `getchallenge` / `connect` and the gamestate
   - 5 minutes of play, then a map change and a reconnect
   - voice
   - a download
   - rcon

**Privacy.**

- Raw pcaps stay private. The CD key goes in plaintext to the authorize port, and `getchallenge` carries its MD5.
- rcon and userinfo passwords travel in the clear.
- Replace any Steam ticket or SteamID with a dummy of the same length.
- Scrub IPs.
- Scrub payloads only **after** decoding. The netchan XOR key (`CL_Netchan_Encode` / `CL_Netchan_Decode` in `cl_net_chan_mp.cpp`) derives from the challenge, the sequence numbers and the last reliable command string. The usercmd key uses `checksumFeed` and `Com_HashKey` of a server command. Editing any of these before decoding breaks the decode.
- Commit only sanitized, decoded fixtures.

## 6. Decode and diff

- Decode with the fork's own MSG and netchan code, so a diff points at a real codec delta.
- The harness is `tests/net_capture_certification.cpp` (ctest `net-capture-certification`). Fixtures live under `tests/fixtures/netcaptures/steam-1.8/` with a `MANIFEST.txt`. The only required profile is `steam-1.8`. The harness exits 77 while evidence is missing.
- Current capture kinds: `scalar-sequence`, `huffman-block`, `usercmd-delta` (`kKindRules` in `tests/net_capture_fixtures.hpp`).
- Still to add, with the captures: kinds for connectionless messages, gamestate, snapshot deltas, server commands, voice and download, and a field-by-field diff report per message class.

**Test clients.** `scr_testclients` (and `addtestclient()`) spawn nothing
unless `developer 1` and `developer_script 1` are set. A connect rejected on
protocol logs only through `Com_DPrintf`, so a missing bot is silent without
`developer 1`. Test clients send protocol 1, so a steam18 server rejects them
until #641 (open) sends the server's wire protocol. The first
int-stat `getstat` on a 64-bit server comes from a bot (#667).

## 7. Profiles

The protocol is a per-instance profile, chosen at startup by one latched dvar.

| Server profile | Behaviour |
| --- | --- |
| `steam18` (default) | Protocol 7. Stock `getchallenge` / `connect`. 1.8 `getinfo` keys. Auth per §8 |
| `proto6` | Reserved for [ADR-0005](../decisions/0005-cod4x-compatibility.md) option B-cheap: stock protocol 6, so CoD4x clients join in legacy mode |
| `fork` | Non-default. Today's Steam-ticket / `cl_guid` path. Keep it only while a test needs it; otherwise delete |

**Client profile `steam18`:** sends `getchallenge <n> "<md5(user's CD key)>"` and protocol 7 in `connect`.

**CoD4x auto-update refusal (every profile):** when a `challengeResponse` carries `xproto`, abort the connect with a message. Download nothing and run nothing. The retail updater also stays unreachable from any server-supplied address: `CL_UpdateInfoPacket` sets `cl_updatefiles`, and `CL_DownloadsComplete` calls `Sys_QuitAndStartProcess` on `autoupdateFilename`.

## 8. Auth policy

| Option | Meaning |
| --- | --- |
| A | Accept the CD-key hash unchecked; it serves only as the ban key |
| B | Try Activision's authorize service first; fall back to A on timeout |

Decided 2026-10-07: option A (owner). Under `steam18`, `SV_GetChallenge` takes a stock request's CD-key hash as the client's identity.

## 9. Prediction check

Replay the captured usercmds through the fork's `Pmove` (`bg_pmove.cpp`), and compare against the captured playerStates using the tolerances in [DETERMINISM](DETERMINISM.md). A mismatch means a `bg_*` difference between 1.0 and 1.8. Such a difference is in scope only if this check finds it.

## 10. Reverse-engineering rule

[ADR-0004](../decisions/0004-reverse-engineering-scope.md) governs RE:

- **Capture first.** RE only for behaviour seen in captures that decoding and diffing cannot explain.
- **Binaries.** RE is allowed on the Steam 1.8 `iw3mp.exe`. The 1.7 exe is allowed only as a diff aid, to locate a change.
- **Beads.** One bead per RE task, naming the capture and the bytes it cannot explain.
- **Output.** A spec note plus a fixture, never transliterated code.

**Out of scope:** PunkBuster; CoD4x protocol 21; anti-cheat. Per the owner (#692), any 1.1–1.8 executable-side change the 1.8 exe shows unambiguously is imported, on the wire or not (§12).

## 11. Exit and effort

| Gate | Exit evidence |
| --- | --- |
| G4a | An unmodified Steam 1.8 client discovers, connects, plays, changes map and reconnects on the fork's **x86** server |
| G4b | The G4a matrix against every native server that passed G3 |

- **Work:** the `steam18` profile is done (#531, #532). The capture decoder and diff report (§6) and the G4a run (NOW row 36) wait on the captures.
- **Effort:** 6–12 weeks, or 12–20 if in-band formats changed. All of it can be built and tested on the x86 build, and it starts when the captures arrive.
- **Evidence rules:** fork-peer evidence never counts for G4, and x86 evidence never counts for G4b ([CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md)).

## 12. 1.8 executable changes (#692)

Read from the local 1.8 `iw3mp.exe`; the SP exe is DRM-wrapped and unread, so SP keeps 1.0.

| Change | Status |
| --- | --- |
| getinfo `hc`/`od` (`scr_hardcore`, `scr_oldschool`) | #698 |
| `player_lean_*` defaults (7 of 8, MP) | #701 |
| `g_antilag` is ROM | #702 |
| `sv_floodprotect`: int, 4, burst per 800 ms | #705 |
| `sv_pure` default 1 | #717 |
| Browser `hc`/`od` filters: `ui_browserHardcore`, `ui_browserOldSchool` | #726 |
| `cl_maxppf` (5, [1,10]): pings per frame | deferred: `CL_UpdateDirtyPings` rewrite; reply path untraced |
| `fs_usedevdir` (bool, INIT) | #725 |
| `serverInfo_t` 0x9c, not 0x94 | kept at 0x94; `servercache.dat` stays 1.0 |
| Debug defaults: `sv_cheats`, `logfile`, `loc_warnings*`, `monkeytoy`, `r_showMissingLightGrid` are 0; `developer_script`, `com_timescale`, `g_fog*ReadOnly` flags | one "retail defaults" PR before 0.1.0 |
| `com_maxfps` 250, `r_fullscreen` 0 | intentional, kept |
| Lagometer layout constants | client HUD, not imported |

**Dvars:** 1,165 named registrations; 4 missing (above); 22 defaults or flags differ, all listed above.

**Shared code, static:** 29 bgame functions (pmove, weapons, jump, mantle), paired by the dvars they read, load no float constant ours lacks. The read misses functions with fewer than two dvars, integer logic and control flow.

**Shared code, per frame (§9):** _pending, filled in by the antec run._

**Patch notes:** the public 1.1–1.7 notes are still to be classified as data-side or exe-side.
