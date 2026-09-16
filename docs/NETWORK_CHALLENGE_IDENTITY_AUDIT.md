# MP discovery, challenge and client-identity source audit

Status: source inventory complete at the recorded SHA below; original-binary
behaviour and acceptance remain **unproven**. This document is documentation
only. It does not change production behaviour, protocol/build numbers,
authentication or network encodings, and it does not certify retail
compatibility.

Tracking: this is the parent-level source audit for
[issue #122](https://github.com/jm2/kisakcod/issues/122) / bead `ki-oq93`. The
full issue acceptance remains open; this report references #122 and never
closes it. It is bounded to the discovery/challenge/identity/connect path and
does not own the dependent stages #123–#137 (see section 9). It follows the
same convention as [DVAR_SERVER_COMMAND_AUDIT.md](DVAR_SERVER_COMMAND_AUDIT.md).

## 1. Provenance and evidence boundary

- **Recorded source SHA:** `47935eaaf35b4dfa045c5013a31617fb845a9e4d`
  (`47935eaa`, the `origin/master` tip when this stage started; merge of fork
  PR #143). Every line citation below was read from that exact checkout; file
  paths are repository-relative.
- **Evidence class:** direct source inspection at the recorded SHA. No
  executable, no wire capture, no original commercial binary, no licensee data
  and no reference-server session were used. Nothing here is a runtime result.
- **Scope boundary:** the mandatory contract in
  [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) governs this work.
  Perfect interoperation with unmodified original commercial 1.7 and
  unmodified Steam commercial 1.8 is required and is still **unproven**.
  Community CoD4x "1.8" is a different reference and is not a substitute.
- **Numbers:** the protocol constants and string literals quoted below are the
  values actually present at this SHA. No protocol number, build number or
  version string is inferred, corrected or invented by this audit.

## 2. Scope

This audit traces one exchange end to end: the client's connectionless
`getchallenge` / `connect` pair and the server's `SV_GetChallenge` /
`SV_DirectConnect` handling, including the fork-specific client-identity and
Steam-ticket arguments, the identity namespaces they are validated against, and
the ban key that results. It also records where the legacy CD-key authorization
round-trip now sits.

Non-goals: changing any of the above; deciding what the commercial dialect
*should* be; pinning reference hashes; claiming any profile passes.

## 3. Where the protocol version is pinned

| Location | Value at `47935eaa` |
|---|---|
| `src/server_mp/sv_init_mp.cpp:700` | server dvar `protocol` registered as `1`, flags `DVAR_SERVERINFO \| DVAR_ROM` (read-only) |
| `src/server_mp/sv_client_mp.cpp:655-660` | `SV_DirectConnect` rejects any `connect` whose `protocol` userinfo is not `1`, replying `EXE_SERVER_IS_DIFFERENT_VER 1.0` |
| `src/client_mp/cl_main_mp.cpp:1120-1121` | the client writes `protocol` = `1` into the `connect` userinfo |
| `src/client_mp/cl_main_pc_mp.cpp:212-220` | `CL_ServerInfoPacket` keeps a server in the browser list only when its advertised `protocol` equals `debug_protocol` (default `1`) |

These four sites are mutually consistent at this SHA, but they are all
fork-chosen literals. They are **not** evidence of the commercial dialect, and
the `"1.0"` string in the rejection message is not a verified commercial
version string. This is exactly the "changing one constant is not sufficient"
case the parent describes: the constant cannot be validated until the authentic
reference manifests exist.

## 4. The fork exchange, end to end

### 4.1 Client request — `getchallenge`

`CL_CheckForResend` (`src/client_mp/cl_main_mp.cpp:1042`) sends the connectionless
request on entry to `CA_CONNECTING` (`:1094-1115`):

- Steam build (`:1108`): `getchallenge 0 "<base64 ticket>" "<SteamID64>"`.
- No-Steam build (`:1112`): `getchallenge 0 "" "<32-hex GUID>"`, where the GUID
  comes from `CL_EnsureGuid` (`:52-77`), a persistent per-install value stored
  in `cl_guid` and regenerated only if the stored value is not a valid 32-hex
  string.

Both forms always supply three arguments after the verb. There is no
fork-client path that sends a bare `getchallenge`.

### 4.2 Server challenge — `SV_GetChallenge`

`src/server_mp/sv_client_mp.cpp:130-269`:

1. Find or allocate a `challenge_t` slot for the source address (`:143-166`),
   seeding `challenge` from `svs.time`/`rand()` (`:158-159`).
2. Read arg 2 as the base64 ticket and arg 3 as the *client identity*
   (`:168-171`).
3. Reject the request when the identity argument is empty
   (`:178-182`, `"A client identity is required"`).
4. Enforce an identity-format namespace (`:190-204`):
   - `KISAK_STEAM` builds require **ticket ⇒ decimal SteamID64**, and
     **no ticket ⇒ 32-hex GUID**.
   - non-Steam builds accept a presented ticket with either identity form, but
     still require a 32-hex GUID when no ticket is presented.
5. Check permanent and temporary ban lists keyed on the identity
   (`:206-221`).
6. Store the identity into the challenge record's `cdkeyHash` field
   (`:224`; `challenge_t` at `src/server_mp/server_mp.h:752`, field
   `cdkeyHash[33]` at `:761`).
7. Steam builds only: decode and validate the ticket against the claimed
   SteamID64; any presented ticket that fails is rejected (`:228-246`).
8. Non-Steam builds: any presented ticket is ignored and the client is treated
   as identity-only (`:247-252`).
9. Unless a validated ticket succeeded, reject when the operator dvar
   `sv_requireSteam` is enabled (`:254-265`; dvar registered at
   `src/server_mp/sv_init_mp.cpp:735-739`, default `true` under `KISAK_STEAM`
   and `false` otherwise, per `:730-734`).
10. Emit `challengeResponse <n>` immediately (`:268`).

### 4.3 Identity helpers

The two accepted identity forms are validated by inline helpers in
`src/qcommon/identity.h`:

- `IsHexGuid` (`:9-24`): exactly 32 hex characters, either case.
- `ParseSteamId` (`:26-48`): 1–20 decimal digits, non-zero, overflow-checked
  against `uint64`.

Both are pure functions with no reference to the client's build; the
namespace *policy* lives in the `#ifdef KISAK_STEAM` block above.

### 4.4 Client connect

On `challengeResponse` (`src/client_mp/cl_main_mp.cpp:1497` dispatch,
`:1769-1780` handler) the client stores the challenge and moves to
`CA_CHALLENGING`. `CL_CheckForResend` then builds the `connect` userinfo
(`:1117-1134`): the usual info string plus `protocol`=1, `challenge`, and
`qport`, sent as `connect "<userinfo>"`. The identity/ticket is **not**
resent here; it is bound to the server-side challenge record from step 4.2.

### 4.5 Server admission — `SV_DirectConnect`

`src/server_mp/sv_client_mp.cpp:621`, admission path at `:653-728`:

1. `protocol` must equal `1` (`:655-660`).
2. Find the challenge record matching the source address **and** the supplied
   challenge value; if none, reply `error\nEXE_BAD_CHALLENGE` (`:684-698`).
3. Compute the challenge ping and apply `sv_minPing` / `sv_maxPing` for
   non-LAN clients (`:699-727`).
4. The PunkBuster authorization block is commented out
   (`:731-749`), as are the CD-key server hooks (`:271-304`).

The identity stored in 4.2 step 6 is copied into the connecting client's
`cdkeyHash` (`:690`) and remains the fork's GUID / ban key for the session.

### 4.6 The legacy authorization path is inert

- `CL_RequestAuthorization` (`src/client_mp/cl_main_mp.cpp:593-659`) has its
  entire body commented out. Its call sites (`:1096`, `:1551`, `:2122`) are
  therefore no-ops.
- `svs.authorizeAddress` is never assigned anywhere under `src/` at this SHA;
  the only references are reads (`src/server_mp/sv_client_mp.cpp:71,109,271-288`
  and `src/server_mp/sv_main_pc_mp.cpp:119`). It therefore keeps its
  zero-initialised `NA_BAD` type, so `SV_AuthorizeRequest`
  (`src/server_mp/sv_client_mp.cpp:58-111`) never sends `getIpAuthorize`. Its
  only live call site (`src/server_mp/sv_client_mp.cpp:1706`, after a dropped
  gamestate resend) is consequently also a no-op.
- `SV_AuthorizeIpPacket` (`src/server_mp/sv_main_pc_mp.cpp:104`) is reachable
  only from the `ipAuthorize` connectionless packet (`src/server_mp/sv_main_mp.cpp:730-733`),
  which would have to arrive from an authorize server the fork never contacts.

Net effect: no CD-key authorization round-trip occurs. Authentication/rejection
decisions are made entirely inside `SV_GetChallenge` from the ticket and the
identity argument. This is a deliberate fork behaviour, and it is the single
largest known difference to audit against the commercial exchange.

## 5. Fork-specific deviations to resolve against references

Every row states what is *known from source at this SHA* and what is *unknown
until authentic references exist*. None of the unknowns may be filled in by
guessing a number or by copying CoD4x behaviour.

| # | Observable fork behaviour | Where | Why it can block an unmodified commercial peer | Evidence that would resolve it |
|---|---|---|---|---|
| D1 | `getchallenge` always carries a ticket slot and an identity argument; the server rejects an empty identity | `cl_main_mp.cpp:1108,1112`; `sv_client_mp.cpp:178-182` | A peer that does not supply the fork's extra arguments is refused before any challenge is issued | Captured discovery/challenge exchange from both commercial profiles |
| D2 | Server admission never performs a CD-key authorization round-trip; `svs.authorizeAddress` is never set | `sv_client_mp.cpp:71,58-111`; `cl_main_mp.cpp:593-659` | Commercial 1.7/Steam 1.8 authorization semantics and provider constraints are not modelled | Reference capture plus documented provider configuration for both profiles |
| D3 | `protocol` is `1` on both ends and `"1.0"` is the only version string | `sv_init_mp.cpp:700`; `sv_client_mp.cpp:655-660`; `cl_main_mp.cpp:1121` | The admitted dialect is a fork choice, not a verified commercial value | Reference manifests: executable SHA-256, file/displayed version, build metadata (never a guessed constant) |
| D4 | `challengeResponse` is emitted immediately for any accepted identity; the commercial timing/first-ping gate is not modelled | `sv_client_mp.cpp:254-268` | A peer whose retry/timing expectations differ may not converge | Captured challenge/retry timing from the references |
| D5 | Accepted identities are only decimal SteamID64 or 32-hex GUID | `sv_client_mp.cpp:190-204`; `identity.h:9-48` | The identity namespace/short form a commercial peer presents in this exchange is unverified | Reference capture of the identity bytes and their documented meaning |
| D6 | `cdkeyHash` (the session/ban key) is simply the identity string | `sv_client_mp.cpp:224`; `server_mp.h:761` | Commercial ban/identity conventions may key on a different value | Reference evidence of the commercial identity/ban key |
| D7 | Steam ticket validation is compile-time gated; non-Steam builds ignore any ticket | `sv_client_mp.cpp:228-252` | Ticket handling must be characterised per profile and launch mode | Reference runs in each profile/mode, including ticket-bearing and ticketless peers |

## 6. Required evidence (fail-closed)

Both required commercial profiles are mandatory. A profile that is unavailable,
skipped or substituted with a community build **cannot** count as passing, per
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) and the parent issue. This
audit produces no pass/fail result; it enumerates the source questions the
reference capture must answer. Licensed binaries, game data, CD keys, account
identifiers and tickets must stay out of the public repository; only sanitized
fixtures and evidence metadata may be attached.

## 7. Disposition at this stage

Kept deliberately separate, because "it works fork-to-fork" and "it interoperates
with retail" are different claims:

| Layer | State at this stage |
|---|---|
| Implementation | Existing fork source inspected only; no production change made by this audit |
| Production enrollment | Unchanged; the fork exchange remains as-is |
| Native runtime tests | None added by this audit; the path is not covered by an original-binary session |
| Original-commercial interop | **Unproven** for both 1.7 and Steam 1.8 |
| Package/release evidence | None; commercial profiles remain unpinned |

## 8. Non-goals

This document does not relax any invariant in
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md): valid commercial packet
layouts and semantics stay frozen, authentication is not bypassed, and hardening
may reject malformed input without altering legitimate retail behaviour. It does
not assert mixed-version interchangeability and does not describe CoD4x as
either required commercial reference.

## 9. Related work

The dependent acceptance stages are tracked separately and are not duplicated
here: #123 fragment reassembly, #124 nested cursor ownership, #125 corpus
fail-closed coverage, #126 capability/evidence dashboard, #127 MSG reference
fixtures and simulation parity, #128 server-controlled dvar audit, #129 native
ABI/asset closure, #130 native64 headless MP server, #131 shader/Vulkan
feasibility, #132 audio/voice/cinematic gates, #133 retail-content regression
matrix, #134 hosted sanitizers/Win32 coverage, #135 desktop platform
acceptance, #136 multiplayer services/headless ops, #137 release provenance.
The current gas-city checkpoint lives in [task.md](task.md).
