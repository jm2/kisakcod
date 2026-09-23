# NET_STEAM18: Steam 1.8 wire compatibility

Workstream WS-5 · Gates G4a, G4b · Decisions [ADR-0001](../decisions/0001-network-reference-steam-1-8.md), [ADR-0002](../decisions/0002-punkbuster-out.md), [ADR-0004](../decisions/0004-reverse-engineering-scope.md), [ADR-0005](../decisions/0005-cod4x-compatibility.md) · Queue: [NOW](../NOW.md)

The only network reference is the original Steam release `1.8.13620`. The release gate needs both directions: a KisakCOD client on an unmodified Steam 1.8 server, and an unmodified Steam 1.8 client on a KisakCOD server. PunkBuster is out.

## 1. What the fork speaks today

| Item | Fork today | Where |
| --- | --- | --- |
| Code base | CoD4 **v1.0** PC decompile. `version` is `CoD4 MP 1.0 build …`, `shortversion` is `1.0`; the original build 13620 is noted in `buildnumber.cpp` | `common.cpp`, `buildnumber.cpp` |
| Protocol | `1`, inherited unchanged from upstream | `protocol` dvar (ROM) in `sv_init_mp.cpp`; `SV_DirectConnect` in `sv_client_mp.cpp` rejects any other value with `EXE_SERVER_IS_DIFFERENT_VER 1.0`; `CL_CheckForResend` in `cl_main_mp.cpp` writes it into `connect`; `SVC_Info` advertises it; `CL_ServerInfoPacket` filters the browser on `debug_protocol` |
| Challenge request | `getchallenge 0 "<base64 Steam ticket>" "<SteamID64>"`, or without Steam `getchallenge 0 "" "<cl_guid>"` | `CL_CheckForResend` |
| Challenge handling | `SV_GetChallenge` requires the third argument. A stock request carries only the CD-key MD5, so it is refused with "A client identity is required". It replies `challengeResponse <challenge>` at once | `sv_client_mp.cpp` |
| Authorize / master | The `SV_AuthorizeRequest` call in the challenge path and `SV_MasterHeartbeat` are commented out. `masterServerName` still defaults to `cod4master.activision.com` | `sv_client_mp.cpp`, `sv_main_pc_mp.cpp`, `common.cpp` |
| PunkBuster | Every `Pb*` hook is commented out and stays dead ([ADR-0002](../decisions/0002-punkbuster-out.md)). `getinfo` still sends a `pb` key from `sv_punkbuster` | `sv_main_mp.cpp` and others |

**Result:** stock clients cannot join a fork server today, and a fork client cannot join a stock server.

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

**Huffman tie-break.** `nodeCmp` in `huffman.cpp` orders by weight only. `msg_hData` has two equal-weight pairs (symbols 155/205 and 228/231), so the host `qsort` picks the code book. glibc matches the reference; MSVC-ARM64 and macOS are unverified. `huffman_wire_contract_tests` prints a note and **passes** when a host derives the other code book. The fix (a total order, and a test that fails on disagreement) belongs to [DETERMINISM](DETERMINISM.md). The Steam 1.8 captures decide which order is correct.

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

1. Hash `iw3mp.exe` and record the Steam depot manifest.
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
- Bead 15 adds kinds for connectionless messages, gamestate, snapshot deltas, server commands, voice and download. It emits a field-by-field diff report per message class.

## 7. Profiles

The protocol is a per-instance profile, chosen at startup by one latched dvar.

| Server profile | Behaviour |
| --- | --- |
| `steam18` (default) | Protocol 7. Stock `getchallenge` / `connect`. 1.8 `getinfo` keys. Auth per §8 |
| `proto6` | Reserved for [ADR-0005](../decisions/0005-cod4x-compatibility.md) option B-cheap: stock protocol 6, so CoD4x clients join in legacy mode |
| `fork` | Non-default. Today's Steam-ticket / `cl_guid` path. Keep it only while a test needs it; otherwise delete |

**Client profile `steam18`:** sends `getchallenge <n> "<md5(user's CD key)>"` and protocol 7 in `connect`.

**CoD4x auto-update refusal (every profile):** when a `challengeResponse` carries `xproto`, abort the connect with a message. Download nothing and run nothing. The retail updater also stays unreachable from any server-supplied address: `CL_UpdateInfoPacket` sets `cl_updatefiles`, and `CL_DownloadsComplete` calls `Sys_QuitAndStartProcess` on `autoupdateFilename`.

## 8. Auth policy (open, owner)

| Option | Meaning |
| --- | --- |
| A | Accept the CD-key hash unchecked; it serves only as the ban key |
| B | Try Activision's authorize service first; fall back to A on timeout |

The choice is listed under "Blocked on owner" in [NOW](../NOW.md).

## 9. Prediction check

Replay the captured usercmds through the fork's `Pmove` (`bg_pmove.cpp`), and compare against the captured playerStates using the tolerances in [DETERMINISM](DETERMINISM.md). A mismatch means a `bg_*` difference between 1.0 and 1.8. Such a difference is in scope only if this check finds it.

## 10. Reverse-engineering rule

[ADR-0004](../decisions/0004-reverse-engineering-scope.md) governs RE:

- **Capture first.** RE only for behaviour seen in captures that decoding and diffing cannot explain.
- **Binaries.** RE is allowed on the Steam 1.8 `iw3mp.exe`. The 1.7 exe is allowed only as a diff aid, to locate a change.
- **Beads.** One bead per RE task, naming the capture and the bytes it cannot explain.
- **Output.** A spec note plus a fixture, never transliterated code.

**Out of scope:** PunkBuster; 1.1–1.7 gameplay or balance changes that are invisible on the wire and in §9; CoD4x protocol 21; anti-cheat; UI and menus; single-player.

## 11. Exit and effort

| Gate | Exit evidence |
| --- | --- |
| G4a | An unmodified Steam 1.8 client discovers, connects, plays, changes map and reconnects on the fork's **x86** server |
| G4b | The G4a matrix against every native server that passed G3 |

- **Beads:** 15 (capture decoder and diff report) and 16 (`steam18` server profile on x86).
- **Effort:** 6–12 weeks, or 12–20 if in-band formats changed. All of it can be built and tested on the x86 build, and it starts when the captures arrive.
- **Evidence rules:** fork-peer evidence never counts for G4, and x86 evidence never counts for G4b ([CHARTER](../CHARTER.md), [ROADMAP](../ROADMAP.md)).
