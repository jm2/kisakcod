# External service blocked-evidence ledger

**Issue:** [#136](https://github.com/jm2/kisakcod/issues/136) (retail-compatible
multiplayer services and headless operations). This document **references**
#136 and [#122](https://github.com/jm2/kisakcod/issues/122); it does not close
or complete either of them.

**Recorded source SHA:** `a2ee6e2673388c367fbdf6da8812169ea9ad70d6` (branch
`polecat/ki-oh65`). All file/line citations below are anchored to this SHA.

**Evidence class:** direct source inspection at the recorded SHA only. No
runtime probe, DNS query, wire capture, or licensed-binary inspection was
performed for this ledger. Reachability of any host named here is **neither
asserted nor denied**; availability claims require the operator/#122 evidence
tracked in section 9.

**Deliverable class:** documentation only. This ledger changes no production
behavior and certifies no compatibility.

## 1. Purpose

#136 requires unavailable external services to be recorded as explicit blocked
evidence, with LAN/direct-connect validation kept distinct from internet
service availability. This ledger inventories the master-server, CD-key
authorization, and autoupdate touchpoints that exist in the source at the
recorded SHA, states for each what the source alone can and cannot establish,
and lists the evidence that is blocked.

The discovery/challenge/identity surface (server browse queries, connect
challenge, identity tickets) is audited separately in
[NETWORK_CHALLENGE_IDENTITY_AUDIT.md](NETWORK_CHALLENGE_IDENTITY_AUDIT.md)
(#122) and is not duplicated here.

## 2. Scope

**Owned by this ledger:**

- Master server — server-to-master heartbeat/status and the client server
  browser listing path.
- CD-key authorization — the client and server `getIpAuthorize` round-trip.
- Autoupdate — the client update-check service.

**Not owned by this ledger:**

- Discovery/challenge/identity wire behavior —
  [NETWORK_CHALLENGE_IDENTITY_AUDIT.md](NETWORK_CHALLENGE_IDENTITY_AUDIT.md).
- HTTP (`wwwdl`) download transport — `docs/CODEBASE_AUDIT.md` H4.
- Session/lifecycle runtime testing (loss/reordering, resource bounds, map
  rotation, commands, graceful termination/restart) — a later #136 stage.
- Optional alternate discovery/transport changes (e.g. IPv6, community master
  protocols) — kept separate per the #136 acceptance criteria; none are
  proposed or evaluated here.

## 3. Service inventory

All four services share the same transport family: UDP connectionless
out-of-band messages (`NET_OutOfBandPrint`). Endpoints are configurable via
dvars registered under `KISAK_MP`
(`src/qcommon/common.cpp:1815-1826`, `DVAR_CHEAT`):

| Dvar | Default | Port dvar | Default port |
| --- | --- | --- | --- |
| `masterServerName` | `cod4master.activision.com` | `masterPort` | 20810 |
| `authServerName` | `cod4master.activision.com` | `authPort` | 20800 |

The autoupdate host list is hardcoded (not dvar-driven) at
`src/client_mp/cl_main_mp.cpp:3812-3816`: `cod2update.activision.com`,
`cod2update2.activision.com`, `cod2update3.activision.com`,
`cod2update4.activision.com`, `cod2update5.activision.com`, all on UDP port
28960.

| Service | Client path | Server path | Fork state at recorded SHA |
| --- | --- | --- | --- |
| Master listing (browser) | `getservers` send `src/client_mp/cl_main_pc_mp.cpp:430-459`; `getserversResponse` parse `src/client_mp/cl_main_mp.cpp:1540` | `gameCompleteStatus` `src/server_mp/sv_main_pc_mp.cpp:64-79` | Listing path live |
| Master heartbeat | — | `SV_MasterHeartbeat` `src/server_mp/sv_main_pc_mp.cpp:77-99` | **Fork-disabled** (body commented out) |
| CD-key authorize | `CL_RequestAuthorization` body commented `src/client_mp/cl_main_mp.cpp:613-624` | `SV_AuthorizeRequest` `src/server_mp/sv_client_mp.cpp:59-113`; reply handler `SV_AuthorizeIpPacket` `src/server_mp/sv_main_pc_mp.cpp:104` | **Inert both directions** (see section 6) |
| Autoupdate | `CL_CheckAutoUpdate` `src/client_mp/cl_main_mp.cpp:3515-3581`; reply `CL_UpdateInfoPacket` `src/client_mp/cl_main_mp.cpp:1263-1300` | — | Live client-side; fails silently by design |

## 4. Master server — server side

### 4.1 Address resolution (`SV_MasterAddress`)

`src/server_mp/sv_main_pc_mp.cpp:13-47`:

- A static `netadr_t` cache is re-resolved only while `adr.type == NA_BOT`;
  once resolved it is returned as-is for subsequent calls.
- On success the code asserts the type is no longer `NA_BOT` and prints the
  resolved address at verbosity 15.
- On resolution failure (`NET_StringToAdr` returns false, type `NA_BAD`) the
  code asserts `adr.type == NA_BAD` and prints `Couldn't resolve address: %s`.
  Failure is reported, never fatal.
- Recorded quirk (documented as-is, **not** changed here): the branch that is
  meant to preserve an explicit `:port` suffix in `masterServerName` tests
  `strstr(":", com_masterServerName->current.integer)` at
  `src/server_mp/sv_main_pc_mp.cpp:22-23`, i.e. the dvar string is passed as
  the *needle* inside the two-byte haystack `":"`. That can only match an
  empty dvar, so the branch never triggers and the `masterPort` dvar override
  effectively always applies.

### 4.2 Heartbeat is fork-disabled

`SV_MasterHeartbeat` (`src/server_mp/sv_main_pc_mp.cpp:77-99`) has its entire
body commented out, with the fork's rationale recorded in the comment: the
master server "sends responses back that end up in the Steam Auth code".
Consequences at the recorded SHA:

- The `SV_PostFrame` call site remains
  (`src/server_mp/sv_main_mp.cpp:1372`, `"COD-4"`) and is a no-op.
- `SV_MasterShutdown` (`src/server_mp/sv_main_pc_mp.cpp:101-105`) funnels its
  `"flatline"` message through the same disabled body.
- `SV_UpdateLastTimeMasterServerCommunicated`
  (`src/server_mp/sv_main_pc_mp.cpp:50-59`) is `#if 0` with the in-source note
  "I am pretty sure the master server is already dead".

### 4.3 `gameCompleteStatus` remains live

`SV_MasterGameCompleteStatus`
(`src/server_mp/sv_main_pc_mp.cpp:64-79`) is active for dedicated servers
(`com_dedicated == 2`): it resolves the master address and, when the type is
not `NA_BAD`, sends the `gameCompleteStatus` connectionless message via
`SVC_GameCompleteStatus`. Unlike the heartbeat this path was not disabled, so
a dedicated server with a resolvable `masterServerName` does emit this
out-of-band message at match end.

### 4.4 Blocked evidence — server side

- Original retail 1.7 / Steam 1.8 `heartbeat`, `flatline`, and
  `gameCompleteStatus` wire captures and interval semantics (#122 reference
  evidence).
- Availability state of any maintained replacement master the operator may
  authorize (probe evidence; not attempted here).

## 5. Master server — client browser

### 5.1 Request path

`src/client_mp/cl_main_pc_mp.cpp:430-459` (`getservers` command):

- Resolves `masterServerName` via `NET_StringToAdr` (`:442`).
- **Does not check the resolution result**: `to.type` is unconditionally
  forced to `NA_IP` (`:445`) and `to.port` set from `masterPort` (`:446`). A
  name that fails to resolve therefore sends the `getservers` message toward
  a zeroed address instead of skipping the request.
- Sets `cls.waitglobalserverresponse = 1` (`:443`); the flag is read by the
  server-browser UI (`src/client_mp/cl_ui_mp.cpp:112`) and cleared at
  `src/client_mp/cl_main_mp.cpp:1399`.
- Sends `getservers <game> [filter...]` (with a ` demo` suffix when
  `fs_restrict` is set) as an out-of-band message on `NS_SERVER` (`:459`).

### 5.2 Response path

`getserversResponse` is parsed at `src/client_mp/cl_main_mp.cpp:1540`,
feeding the global server list the browser pings.

### 5.3 Blocked evidence — client side

- An authentic `getserversResponse` capture from a live original master (or
  from an operator-authorized replacement), needed to pin response framing
  beyond what the parser at the recorded SHA accepts.
- Commercial-client browser interop evidence (#122).

## 6. CD-key authorization

The legacy authorization round-trip is inert at the recorded SHA. This
re-states, at the current SHA, the analysis of
`NETWORK_CHALLENGE_IDENTITY_AUDIT.md` section 4.6 (recorded at an earlier SHA;
the key lines were re-verified here):

- The client-side resolver body is commented out
  (`src/client_mp/cl_main_mp.cpp:613-624`), so `CL_RequestAuthorization` is a
  no-op.
- The server-side resolution of `svs.authorizeAddress` is commented out
  (`src/server_mp/sv_client_mp.cpp:271-288`); `svs.authorizeAddress` is never
  assigned under `src/` and keeps its zero-initialized value. `netadrtype_t`
  defines `NA_BOT = 0` and `NA_BAD = 1`
  (`src/qcommon/net_chan_mp.h:24-27`), so the guard at
  `src/server_mp/sv_client_mp.cpp:71` (`type != NA_BAD`) **passes**, and
  `SV_AuthorizeRequest` (`src/server_mp/sv_client_mp.cpp:59-113`) builds the
  `getIpAuthorize` message and attempts the send at `:109`. That send is
  dropped downstream because `NET_SendPacket` does not route a zero `to.type`.
- The reply handler `SV_AuthorizeIpPacket`
  (`src/server_mp/sv_main_pc_mp.cpp:104`) is reachable only from the
  `ipAuthorize` connectionless packet
  (`src/server_mp/sv_main_mp.cpp:730-733`) and would require an authorize
  server the fork never contacts.

**Framing for the #136 criterion "preserve authentication rather than
bypassing it":** no new code bypasses authorization; the legacy path is inert
in the inherited source, and admission is decided by the challenge/ticket path
analyzed in `NETWORK_CHALLENGE_IDENTITY_AUDIT.md`. Whether commercial 1.7 /
Steam 1.8 require an authorization round-trip in any profile, and the provider
constraints involved, remain **blocked** reference evidence under #122.

## 7. Autoupdate

### 7.1 Host list

`src/client_mp/cl_main_mp.cpp:3812-3816` hardcodes five update hosts named
`cod2update*.activision.com`. The names present at the recorded SHA are
recorded verbatim; **no claim is made here** about which host names original
retail CoD4 1.7 shipped — that is a #122 reference-evidence question.

### 7.2 Check flow

`CL_CheckAutoUpdate` (`src/client_mp/cl_main_mp.cpp:3515-3581`), run once per
client session (`autoupdateChecked`, `:187`):

1. Resolves all five names and keeps the resolvable ones (`:3530-3535`).
2. Picks one at random; if it fails, retries the rest (`:3543-3552`).
3. On success: sets port 28960 (`:3554`) and sends the out-of-band
   `getUpdateInfo2 "CoD4 MP" "1.0" <cpustring>` message on `NS_CLIENT1`
   (`:3565`).
4. Sets `autoupdateChecked = 1` on every path; total resolution failure is
   dev-print only (`:3577-3579`) — **silent no-op in normal play**.

The reply `CL_UpdateInfoPacket`
(`src/client_mp/cl_main_mp.cpp:1263-1300`) guards on a resolved
`autoupdateServer` (`:1273`) and source address match (`:1288`), then sets
`cl_updateavailable` / `cl_updatefiles` / `cl_updateversion`. A subsequent
update fetch proceeds through the legacy download path
(`autoupdateStarted` / `autoupdateFilename`,
`src/client_mp/cl_main_mp.cpp:973-984`).

### 7.3 Blocked evidence — autoupdate

- Original update-service behavior (message framing, update manifest shape).
- An operator policy decision on what, if anything, should replace the
  hardcoded hosts. Any such change is optional and must land separately per
  the #136 acceptance criteria; nothing is changed here.

## 8. Validation matrix: LAN/direct-connect vs internet availability

| Capability | Evidence class | Status |
| --- | --- | --- |
| Server browser against a LAN-hosted master replacement (dvar `masterServerName`/`masterPort` override) | LAN/direct-connect | Testable without internet services |
| Direct connect by IP, challenge/connect admission | LAN/direct-connect | Testable without internet services |
| In-band UDP download fallback; `wwwdl` HTTP download from an operator-hosted server | LAN/direct-connect | Testable without internet services (H4 transport) |
| Session/lifecycle behaviors (map rotation, commands, graceful termination/restart, bounded resources) | LAN/direct-connect | Testable without internet services; owned by a later #136 stage |
| Master listing from a true internet master (`getserversResponse` from a live service) | Internet availability | **Blocked** — no probe performed; needs operator/#122 evidence |
| Server heartbeat/status against a true internet master | Internet availability | **Blocked** — same |
| CD-key authorization round-trip | Internet availability + commercial semantics | **Blocked** — provider constraints are #122 reference evidence |
| Autoupdate against a real update service | Internet availability + commercial semantics | **Blocked** — see 7.3 |
| Commercial retail 1.7 / Steam 1.8 interop for any path above | Commercial interop | **Blocked** on #122 licensed references/runner evidence |

The `DVAR_CHEAT` flag on the endpoint dvars is a validation consideration for
the LAN rows (overrides require the cheat/dev context); it is recorded, not
changed.

## 9. Blocked-evidence register

| ID | Missing evidence | Needed for | Owner |
| --- | --- | --- | --- |
| BE-1 | Original 1.7/1.8 `heartbeat`/`flatline`/`gameCompleteStatus` captures and interval semantics | Master-status compatibility pinning | #122 / operator |
| BE-2 | Authentic `getserversResponse` capture from a live original or authorized replacement master | Browser response framing pinning | #122 / operator |
| BE-3 | Authorization provider semantics and wire capture (any profile that requires it) | Preserve-authentication assessment | #122 / operator |
| BE-4 | Original autoupdate service behavior and authoritative host names for retail 1.7 | Update-path compatibility pinning | #122 / operator |
| BE-5 | Reachability probes of any internet service (original or replacement) | Internet-availability column of section 8 | Operator |
| BE-6 | Commercial-client interop runs (retail 1.7, Steam 1.8) against this engine | Compatibility certification for all sections | #122 |

None of the blocked items block the LAN/direct-connect validation rows or the
documentation/code work that does not require them (per the 2026-09-10 queue
reconciliation note on #136).

## 10. References

- [#136](https://github.com/jm2/kisakcod/issues/136) — this ledger's owning
  issue (referenced, not closed).
- [#122](https://github.com/jm2/kisakcod/issues/122) — mandatory protocol
  compatibility parent; owns commercial reference evidence.
- [docs/NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) — compatibility
  contract.
- [docs/NETWORK_CHALLENGE_IDENTITY_AUDIT.md](NETWORK_CHALLENGE_IDENTITY_AUDIT.md)
  — discovery/challenge/identity audit; section 4.6 covers the inert
  authorization path at an earlier SHA.
- [docs/CODEBASE_AUDIT.md](CODEBASE_AUDIT.md) — H3 (discovery routing), H4
  (`wwwdl` HTTP transport, fixed).
- PRs #144, #145, #157 — prior stages of this bead's acceptance history.
