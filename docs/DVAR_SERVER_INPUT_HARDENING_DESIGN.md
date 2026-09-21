# MP server-controlled dvar input hardening design and tradeoff register

Status: documentation only. This document changes no production behavior, no
dvar flag, no default allowlist and no limit. It specifies hardening options as
designs, conditions each option on original commercial reference evidence, and
records the compatibility tradeoff that blocks acceptance of any option whose
behavior would reject or alter valid retail input. Nothing in this document
certifies retail compatibility or satisfies issue
[#122](https://github.com/jm2/kisakcod/issues/122).

Tracking: A06 roadmap row in
[ROADMAP_EXPANSION_PROPOSAL.md](ROADMAP_EXPANSION_PROPOSAL.md) and
[task.md](task.md). Governed by
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). Source inventory at
[DVAR_SERVER_COMMAND_AUDIT.md](DVAR_SERVER_COMMAND_AUDIT.md) (bead `ki-2dkj`,
PR #142, merged commit `a963e9e9`).

The upstream audit issue
[#128](https://github.com/jm2/kisakcod/issues/128) was closed COMPLETED by the
repository owner on 2026-09-16 after the inventory merged. This register records
the reference-independent design and tradeoff work that the A06 roadmap row
still lists ("Add malformed-input rejection tests without broad allowlists that
break valid commands. Any incompatible restrictions belong in a separate
optional mode"). It keeps that scope bounded and does not re-open or re-close
the audit.

## 1. Provenance and evidence boundary

- **Recorded source SHA:** `2babfed8adb17a5b3c80db288890992ec51ec49c`
  (`2babfed8`, the `origin/master` tip this stage forked from). All line
  citations below were read from that exact checkout; paths are
  repository-relative. Where line numbers differ from the earlier audit's
  recorded SHA `a1ca543b`, this document's numbers win for this stage.
- **Evidence class:** direct source inspection only. No executable, no wire
  capture, no original commercial binary, no licensee data and no reference
  server session were used. Nothing here is a runtime result.
- **Mandatory contract:** perfect interoperation with unmodified original
  commercial 1.7 and unmodified Steam commercial 1.8 is required and remains
  **unproven**. Community CoD4x is a different reference and is not a
  substitute. Any protection whose behavior could bind on valid commercial
  input and is not proven against both references is an acceptance blocker, not
   a compatibility exception. A protection that only refuses input a
   context-specific discriminator proves invalid — and that cannot match a valid
   commercial message — is governed by invariant 4 and principle 4, which permit
   the refusal without proving the references are non-fatal at the same point.
- **Reference inputs are unavailable** in this checkout; see the audit's
  section 10. Their unavailability is carried forward here as a blocker, never
  as a waived or skipped test.

## 2. Scope and non-goals

In scope: a design for optional, reference-conditioned protections against
invalid or unsafe server-controlled dvar input, plus an explicit register of the
tradeoffs each protection creates against valid commercial behavior.

Not in scope, and explicitly declined here:

- No default allowlist, no default limit, no default dvar flag change, and no
  default rejection rule over **valid** commercial input. This design stage
  changes no production behavior in `CG_SetClientDvarFromServer`'s general path.
  Security hardening of input that a context-specific discriminator proves
  invalid, and that cannot reject or alter a valid commercial message, is not
  declined by this bullet: invariant 4 permits rejecting actually-proven
  malformed input, and such hardening is not blanket-deferred to the optional
  mode.
- No certification of retail compatibility from source inspection.
- No change to unrelated work, including the #106 merge hold or the
  operator-owned #119 (`ki-n1et`).
- No re-scoping of the audit: the seven mutation families and their citations
  remain as recorded in the audit.

Restrictions that could reject or alter valid commercial behavior are delivered
as a **separate, default-off optional mode**, so the commercial-compatible
default path stays byte-for-byte and behavior-for-behavior unchanged unless an
operator explicitly opts in. Hardening that only refuses input a
context-specific discriminator proves invalid and cannot alter a valid
commercial message is not forced into that mode: it is a security fix eligible
for the default path under invariant 4. This document enables neither; it
records both designs and the evidence each still requires.

## 3. Invalid / unsafe input classes

These classes are derived from the audit's path summary (section 5.8), observed
behavior (section 8) and invalid-input matrix (section 9.2). They define the
inputs a protection could act on. None of them is claimed to be exploitable;
the audit performed no runtime analysis.

| Class | Input | Reachable client sink |
|---|---|---|
| C1 | `v` command with a dvar name outside the server script grammar `[A-Za-z0-9_]` | `CG_SetClientDvarFromServer` → `Dvar_SetFromStringByName` |
| C2 | `v` command whose name is unknown, creating a new `DVAR_EXTERNAL` string dvar | `Dvar_RegisterString` → `Dvar_RegisterNew` |
| C3 | Repeated unique unknown names exhausting the 4096-dvar pool | `Dvar_RegisterNew` → `Com_Error(ERR_FATAL, ...)` |
| C4 | A value longer than the existing-dvar 1023-byte store bound delivered to `Dvar_SetFromStringByName`. Reachable through a server-controlled initial-gamestate configstring, whose value is bounded only by the 8191-byte `MSG_ReadBigString` reader (see the reachable configstring boundary below), and likewise present as a direct-call sink property; **not** reachable through a server-controlled `v` command, which is bounded to 1023 bytes upstream (see the reachable `v` command boundary below) | Initial-gamestate configstrings through `CL_SystemInfoChanged` / `CG_ParseCodInfo` → `Dvar_SetFromStringByName`; direct `Dvar_SetFromStringFromSource` (`I_strncpyz(...,1024)`) or `Dvar_CopyString`/script-string store |
| C5 | `hud_drawHud` value that is negative, non-numeric, or greater than 1 | `CG_SetDrawHud` → `atoi`, `MyAssertHandler`, assignment |
| C6 | `v` command with an odd **payload** count — the arguments after the verb, i.e. an even total `Cmd_Argc` — a trailing name whose value is read as empty | loop `for (i = 1; i < Cmd_Argc(); i += 2)` reads `Cmd_Argv(i + 1)` past the end |
| C7 | Out-of-domain numeric or enum values | `Dvar_ValueInDomain` rejection / enum reset fallback |
| C8 | Non-empty shellshock configstring naming a local `.shock` file | `BG_LoadShellShockDvars` (local-file values, not wire values) |

Argument-count convention for C6 (and HP6/T6 below): the `v` dispatch loop
starts at `Cmd_Argv(1)` and advances by two
(`for (i = 1; i < Cmd_Argc(); i += 2)`, `src/cgame_mp/cg_servercmds_mp.cpp:664`),
and `Cmd_Argc` counts the verb at index 0. Complete name/value pairs therefore
give an **odd** total `Cmd_Argc` and an even payload count. A trailing name with
no value gives an **even** total `Cmd_Argc` and an **odd** payload count. The
invalid case is the odd payload count (even total), never an odd total; counting
from the verb index instead of the payload would invert the condition.

Reachable `v` command boundary for C4 (and HP4/T4 below): server commands are
not parsed directly from the network message, and the first component that
bounds the wire string is the string reader, not the command slot.
`CL_ParseCommandString` (`src/client_mp/cl_parse_mp.cpp:1192-1205`) reads the
command with `s = MSG_ReadString(msg)` (`:1199`) and only then copies the result
into `clc->serverCommands[seq & 0x7F]`, one of 128 1024-byte slots
(`char serverCommands[128][1024]`, `src/client_mp/client_mp.h:187`), using
`I_strncpyz(..., 1024)` (`:1204`). `MSG_ReadString`
(`src/qcommon/msg_mp.cpp:484-501`) is the upstream boundary: it consumes bytes
from the wire until the terminating NUL (a read that fails past the message end
returns `-1` and is treated as NUL, `:493-494`), stores at most the first 1024
bytes (`if (l < 0x400) string[l] = ...`, `:495`), runs each stored byte through
`I_CleanChar` (`:496`; maps byte 146 to `'`,
`src/universal/q_shared.cpp:518-524`), and then forces `string[1023] = 0`
(`:500`). A command longer than 1024 wire bytes is therefore truncated and
`I_CleanChar`-adjusted inside `MSG_ReadString` before the `I_strncpyz` slot
copy, which can only shorten the already-bounded result, not widen it. The
cgame then executes from that slot:
`s = clc->serverCommands[serverCommandNumber & 0x7F]`
(`src/client_mp/cl_cgame_mp.cpp:268`), `Cmd_TokenizeString(s)` (`:274`), and the
`v` dispatch loop reads the tokenized result
(`src/cgame_mp/cg_servercmds_mp.cpp:663-670`). At most **1023 bytes** remain
for the verb, the name, the separators and the value **together**. A `v` value
that reaches either sink through this server-controlled path therefore cannot,
by itself, exceed the existing-dvar 1023-byte store bound; a longer value is
truncated by the wire string reader before tokenization, not at the sink. C4 is
not, however, reachable only at the sink: the initial-gamestate configstring
route below reaches the same sink with values the `v` path cannot deliver. A
direct call to `Dvar_SetFromStringByName` /
`Dvar_SetFromStringFromSource` (or the script-string registration path) can
still present an arbitrarily long value, but that exercises the sink in
isolation, not the reachable server-controlled input. A test that injects
directly into `serverCommands` bypasses `MSG_ReadString`'s byte consumption,
`I_CleanChar` adjustment and truncation and is likewise not a reachable-input
test. The direct sink, the direct slot injection, the reachable `v` wire path
and the reachable initial-gamestate configstring route must be modeled and
tested separately (section 8).

Reachable initial-gamestate configstring boundary for C4 (and HP4/T4 below): a
second server-controlled route reaches `Dvar_SetFromStringByName` without
passing through the `v` command or `MSG_ReadString`. During an initial remote
gamestate, `CL_ParseGamestate` (`src/client_mp/cl_parse_mp.cpp:1045-1185`) reads
each `svc_configstring` with the 8192-byte `MSG_ReadBigString`
(`s = MSG_ReadBigString(msg)`, `:1113`) and appends the result to
`gameState.stringData`, rejected only when the cumulative store would exceed
`MAX_GAMESTATE_CHARS` `0x20000` (`:1114-1123`); an out-of-range configstring
index is rejected at `:1095`. `MSG_ReadBigString`
(`src/qcommon/msg_mp.cpp:504-527`) maps wire byte 37 (`%`) to `.` (`:513-516`),
runs each stored byte through `I_CleanChar` (`:521-522`) and forces
`bigstring[8191] = 0` (`:526`), so a configstring value can carry up to 8191
bytes — well above the existing-dvar 1023-byte store bound. `CL_ParseGamestate`
then calls `CL_SystemInfoChanged` (`:1179`). `CL_SystemInfoChanged`
(`src/client_mp/cl_parse_mp.cpp:171-247`) reads configstring 1
(`stringOffsets[1]`, `:194`), iterates its system-info pairs with `Info_NextPair`
into 8192-byte (`0x2000`) key and value buffers (`char (*key)[8192]`, `:179`;
`char (*value)[8192]`, `:181`; `:235`) and calls
`Dvar_SetFromStringByName((const char *)key, (char *)value)` for each pair
(`:238`) when the client is not a listen server and is not replaying a demo
(`:197-204`, `:230-240`). A second consumer, `CG_ParseCodInfo`
(`src/cgame_mp/cg_servercmds_mp.cpp:52-71`), reads configstrings 20–147 as keys
(`CL_GetConfigString(localClientNum, i + 20)`, `:65`) and 148–275 as values
(`CL_GetConfigString(localClientNum, i + 148)`, `:68`) and calls
`Dvar_SetFromStringByName(key, value)` (`:69`) on a non-local client; it is
reached from the cgame server-command/configstring path (`:973`) and from cgame
initialization (`src/cgame_mp/cg_main_mp.cpp:1727`). A server-controlled
configstring value can therefore reach `Dvar_SetFromStringByName` at up to
`MSG_ReadBigString`'s 8191-byte bound. Unlike the `v` path, C4 is reachable at
lengths above 1023 bytes through this route, so a value-length policy is a
reachable-input tradeoff here, not only a direct-sink question.

## 4. Design principles

1. **Compatibility first.** The default path is unchanged. No protection is
   enabled by default, and no protection may reject or alter input that an
   original commercial peer validly sends.
2. **Evidence-gated.** Each protection names the exact reference evidence that
   would justify enabling it. Without that evidence the protection stays off
   and the underlying question stays open.
3. **No blanket allowlist.** A default allowlist of permitted dvar names is
   rejected as a design because it would break valid mod and server-created
   dvar behavior and cannot be justified without reference evidence. Any
   policy is expressed as a bounded, opt-in validation mode, not a default list.
4. **Reject malformed input, preserve valid input.**
   Per [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) invariant 4, a
   security fix may reject malformed/invalid input while retaining every valid
   commercial message and its semantics; reproducing a fatal or memory-unsafe
   outcome is **not** required. Two questions must be separated.
   - **Malformed/invalid input** may be refused gracefully, but only when a
     context-specific discriminator proves the input is invalid and cannot also
     be a valid commercial message. That refusal is a robustness improvement,
     not a compatibility change, and it does **not** require evidence that the
     original builds are non-fatal at the same point. Hardening that both
     refuses only such proven-invalid input and cannot alter a valid commercial
     message is eligible for the default path; it is not blanket-deferred to
     the optional mode. Input that merely looks unusual does not qualify: an
     out-of-grammar `v` name may be a name that a commercial server or mod
     legitimately sends (HP1), and a high registration volume is
     indistinguishable from a supported high-volume mod at the pool-cap branch
     (HP3). Inferring hostile intent from those bytes is not valid, so neither
     is treated as malformed; both stay under their reference-dependent gates.
   - **Valid high-volume or boundary input** (for example a supported mod that
     legitimately registers many server-created dvars) must still be accepted
     exactly as the references accept it. Where a protection could bind on valid
     behavior — a registration cap, or a non-fatal return where callers assume
     registration always succeeds — reference evidence and a caller-safety audit
     are required before enabling it, and a restriction that would reject valid
     commercial behavior is reserved for the default-off optional mode.
5. **Bounded copies stay bounded.** Copy-length hardening that cannot reject a
   valid command (e.g., existing `I_strncpyz` bounds) is retained; new length
   rejection policy is opt-in because rejection can diverge.
6. **Tradeoffs are recorded, not hidden.** Every protection that could conflict
   with valid retail behavior is listed in section 6 as a blocker that prevents
   its own acceptance until reference evidence resolves it.

## 5. Protection register

Each protection is a design option; an option that could reject or alter valid
commercial behavior is delivered through the optional mode, while one that only
refuses input a context-specific discriminator proves invalid may be eligible
for the default path. No row below currently qualifies as the latter.
"Default" is the production behavior today. "Optional mode" describes what the
opt-in mode would do. "Reference evidence required" is the evidence that would
have to be captured before the option could be enabled: reference captures for a
reference-dependent protection, or the caller-safety and valid-input preservation
evidence named in the row for eligible default hardening. "Status" is the
acceptance state of the option, not of the A06 task.

| ID | Class | Proposed optional protection | Valid retail behavior at risk | Reference evidence required | Status |
|---|---|---|---|---|---|
| HP1 | C1 | Reject or ignore client `v` names that fail a reference-validated grammar. The client currently length-bounds the name to 149 bytes (`I_strncpyz(text, v23, 150)`, `src/cgame_mp/cg_servercmds_mp.cpp:663-668`) but does not grammar-check it, unlike the server script path (`Dvar_IsValidName`, `src/universal/dvar.cpp:305-319`; used at `src/game_mp/g_client_script_cmd_mp.cpp:2135`, `:2190`). | Original servers or mods may legitimately send `v` names containing characters outside `[A-Za-z0-9_]`; enforcing the script grammar would drop those commands. | Captured `v` name grammar and full name set from both commercial references and a supported-mod fixture. | BLOCKED — do not enable without evidence. |
| HP2 | C2 | Bound or deny new unknown-name dvar registrations in the optional mode (for example, require a permitting policy before `Dvar_RegisterString` creates a `DVAR_EXTERNAL` dvar). | Server-created dvars may be a supported mod feature; a bound could reject valid mod setup. | Evidence that the references create unknown-name dvars, with counts and names. | BLOCKED — do not enable without evidence. |
| HP3 | C3 | Convert the 4096-dvar pool-cap `Com_Error(ERR_FATAL, ...)` (`src/universal/dvar.cpp:1568-1571`) into a bounded, non-fatal refusal, with callers handling the failure. The pool-cap branch only observes `dvarCount >= 4096` (`src/universal/dvar.cpp:1568`) and has no invalid-input discriminator, so it cannot separate a hostile unknown-name flood from a supported high-volume mod: the refusal would bind identically in both cases. Caller-safety tests alone therefore cannot qualify it for the default path. Without a demonstrated context-specific invalid-input discriminator or a reference-backed bound showing valid registration volume cannot reach the cap, the entire shared cap change is reference-dependent and belongs in the default-off optional mode. | Valid commercial/mod input could legitimately approach the cap; a bound that rejects it is incompatible. | Reference/mod evidence that valid registration volume cannot reach the cap, plus the caller-safety audit; or a demonstrated context-specific invalid-input discriminator that cannot match valid registration. | BLOCKED — do not enable without evidence. |
| HP4 | C4 | Reconcile value-length handling at `Dvar_SetFromStringByName` — the existing-dvar 1023-byte store bound and the unknown-name script-string store — across both reachable server-controlled routes and the direct-call path. A server-controlled `v` value is already capped at 1023 bytes by the upstream `MSG_ReadString` wire reader and the subsequent non-narrowing command-slot copy, so a new `v` length bound is not a reachable hardening tradeoff; but an initial-gamestate configstring value is bounded only by the 8191-byte `MSG_ReadBigString` reader and reaches the same sink through `CL_SystemInfoChanged` / `CG_ParseCodInfo` (section 3), so a value-length policy does bind on reachable server-controlled input and must be modeled against that route as well as against the direct-call paths. Any bound or normalization still changes at least one existing path. | For reachable `v` input, none: the upstream wire string reader already bounded it. For reachable configstring input and direct sink callers, rejecting or normalizing a long value can change behavior relative to the existing paths. | Maximum value lengths per dvar type and path for both references across the configstring routes and the direct-call paths, with direct-sink, direct slot-injection, reachable `v` wire-reader and reachable configstring tests recorded separately (section 8). | BLOCKED — sink-level and reachable-configstring change; do not enable without evidence. |
| HP5 | C5 | Enforce a defined `hud_drawHud` range in every build instead of relying on `MyAssertHandler`, which is empty in a non-PURE Release build (`src/cgame_mp/cg_servercmds_mp.cpp:1389-1400`; assert policy `src/universal/assertive.cpp:643-691`). Options: clamp, ignore out-of-range, or reject. | Clamping or rejecting changes `cgameGlob->drawHud` for a value the original client may have assigned verbatim. | Reference behavior for `hud_drawHud` values `0`, `1`, `>1`, negative and non-numeric, per build configuration. | BLOCKED — do not enable without evidence. |
| HP6 | C6 | Define deterministic handling of a `v` command with an **odd payload count** (arguments after the verb; equivalently an even total `Cmd_Argc`) that leaves a trailing name with no value, instead of silently reading an empty value for it (loop `for (i = 1; i < Cmd_Argc(); i += 2)`, `src/cgame_mp/cg_servercmds_mp.cpp:663-670`). | The original client may tolerate the odd payload and apply a partial command; changing the outcome can diverge. | Reference behavior for a `v` command with an odd payload count (even total `Cmd_Argc`). | BLOCKED — do not enable without evidence. |
| HP7 | C7 | Keep the existing domain rejection and enum reset fallback; do not tighten domain behavior in the optional mode without evidence. | Any tightened domain check would reject values the reference accepts. | Domain behavior captured from both references. | NO CHANGE PROPOSED — existing behavior retained. |
| HP8 | C8 | Keep the local-file indirection as-is; it is not wire-value-controlled. Optionally log the requested shock name in the optional mode. | Logging is behavior-neutral; no compatibility risk. | None for logging; reference behavior only if filing policy changes. | DESIGN RECORDED — low risk, not a compatibility change. |

Already-bounded paths that need no new protection (documented so they are not
mistaken for open findings):

- `cg_objectiveText` and `g_scriptMainMenu` copies are already bounded
  (`I_strncpyz(...,1024)` and `(...,256)`, `src/cgame_mp/cg_servercmds_mp.cpp:1384-1407`).
- The client `v` name is already length-bounded to 149 bytes (150-byte buffer,
  `src/cgame_mp/cg_servercmds_mp.cpp:663-668`).
- The existing-dvar string store is already bounded to 1023 payload bytes plus
  the NUL (`I_strncpyz(buf, string, 1024)`, `src/universal/dvar.cpp:2607`).
- The server-controlled `v` command is already bounded **before** tokenization:
  `MSG_ReadString` consumes the wire string and stores at most the first 1024
  bytes, forcing `string[1023] = 0` (`src/qcommon/msg_mp.cpp:484-501`), and
  `CL_ParseCommandString` then copies that result into a 1024-byte
  `serverCommands` slot (`I_strncpyz(..., 1024)`,
  `src/client_mp/cl_parse_mp.cpp:1192-1205`; `src/client_mp/client_mp.h:187`),
  leaving at most 1023 bytes for verb, name, separators and value together, so
  the reachable `v` value is below the existing-dvar 1023-byte store bound. This
  bounds the `v` route only: the separate initial-gamestate configstring route is
  bounded by the 8192-byte `MSG_ReadBigString` reader instead (up to 8191 bytes,
  section 3) and can therefore exceed the existing-dvar 1023-byte store bound.

## 6. Unresolved tradeoff register (acceptance blockers)

The following tradeoffs are unresolved because the licensed references are
unavailable. Each records what still has to be proven before the protection it
names can be enabled; where a tradeoff can affect valid commercial behavior it
blocks that protection's acceptance. None of them justifies relaxing the
compatibility contract in [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md).

| ID | Tradeoff | Protections | Blocker statement |
|---|---|---|---|
| T1 | Name-grammar enforcement may reject valid retail or mod `v` names. | HP1 | Cannot accept until both references' `v` name sets are captured and shown to satisfy the grammar for every valid command. |
| T2 | Bounding unknown-name registrations may reject a supported mod feature. | HP2 | Cannot accept until reference/mod evidence shows whether server-created dvars are part of valid behavior and at what volume. |
| T3 | Replacing the fatal pool-cap with a graceful refusal requires a caller-safety audit (callers may assume registration always succeeds). Reproducing a fatal outcome is not a compatibility requirement in itself (invariant 4), but the pool-cap branch only observes `dvarCount >= 4096` and has no invalid-input discriminator: it cannot distinguish a hostile unknown-name flood from a supported high-volume mod, so a refusal binds both. The cap change is therefore reference-dependent, not eligible default hardening until a discriminator or reference-backed bound exists. | HP3 | Cannot accept until reference/mod registration volume is shown not to reach the cap, or a context-specific invalid-input discriminator is demonstrated; the caller-safety audit is also required. |
| T4 | A reachable server-controlled `v` value cannot exceed the existing-dvar 1023-byte store bound because `MSG_ReadString` first consumes the wire string and stores at most 1024 bytes (forcing `string[1023] = 0`), and the later `CL_ParseCommandString` `I_strncpyz` copy is non-narrowing (section 3), so a new `v` length bound is not a reachable tradeoff. A server-controlled initial-gamestate configstring is a separate reachable route to the same sink: it is bounded only by the 8192-byte `MSG_ReadBigString` reader (up to 8191 bytes) before `CL_SystemInfoChanged` / `CG_ParseCodInfo` call `Dvar_SetFromStringByName` (section 3), so a value-length policy does bind on reachable server-controlled input there. A sink-level value-length bound or normalization also still alters at least one of the two existing direct-call paths, and neither direct-sink tests, direct `serverCommands` injection, nor a reachable `v` test alone may be presented as covering the reachable configstring route. | HP4 | Cannot accept a value-length change until reference value lengths per path are captured for the configstring routes and the direct-call paths, with direct-sink, direct slot-injection, reachable `v` wire-reader and reachable configstring evidence recorded separately. |
| T5 | Range handling for `hud_drawHud` changes client state relative to the reference. | HP5 | Cannot accept until reference behavior is captured for in-range and out-of-range inputs. |
| T6 | `v` handling of an odd payload count (even total `Cmd_Argc`) changes a tolerated outcome. | HP6 | Cannot accept until reference behavior for an odd payload count (even total `Cmd_Argc`) is captured. |

Until the reference-dependent questions are resolved — and until any eligible
malformed-input hardening is separately implemented and tested — the correct
production state is the unchanged default path. This is the "record the
unresolved tradeoff and block its acceptance" requirement of the A06 acceptance
criteria, applied per protection.

## 7. Optional-mode design and activation safety

The protections above that could reject or alter valid commercial behavior are
reserved for a separate, default-off optional mode. This reservation is not a
blanket deferral of every protection: hardening that only refuses input a
context-specific discriminator proves invalid, and that cannot alter any valid
commercial message, is a security fix permitted by
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) invariant 4, and is not
required to be hidden behind the optional mode. The optional mode exists for the
incompatible restrictions:

1. **Default off.** The mode is inactive in every default build and every
   commercial-compatible configuration. The default path is the audited current
   behavior.
2. **Explicit activation.** The mode is enabled only by an explicit operator
   configuration. It is never auto-enabled, never defaulted on, and never
   enabled by a compatibility profile.
3. **No default allowlist.** The mode does not install a permitted-name list,
   a default cap or a default rejection rule.
4. **Per-protection gates.** Each protection in section 5 is independently
   gated, so an operator can enable an optional-mode protection only with its
   resolved reference evidence. No protection in section 5 currently qualifies
   as eligible default hardening: name-grammar rejection (HP1) and the pool-cap
   refusal (HP3) both bind input that may be valid and stay reference-dependent
   (section 8). The optional mode remains available for any restriction that
   could reject or alter valid commercial behavior.
5. **No compatibility claim.** Enabling the mode is not a certification of
   retail compatibility and does not satisfy #122. Both commercial profiles
   remain release gates in `NETWORK_COMPATIBILITY.md`.
6. **Observability without behavior change.** Where a protection is only for
   diagnosis (for example, logging a `v` name that would fail the grammar, or
   logging a shellshock name), it may be added in the optional mode without
   altering the applied command.

## 8. Test requirements

These extend the audit's matrix (section 9). The evidence required depends on
whether a protection is reference-dependent or is eligible default hardening:

- **Reference-dependent valid-input/compatibility certification.** A protection
  that could bind on valid commercial behavior — HP1, HP2, HP3, HP4, HP5 and
  HP6 — can be accepted only after the rows below are run against both original
  commercial 1.7 and Steam commercial 1.8. Full
  [#122](https://github.com/jm2/kisakcod/issues/122) certification stays blocked
  until both profiles pass; this document certifies neither.
- **Eligible default hardening on proven-malformed input.** Hardening that only
  refuses input a context-specific discriminator proves invalid, and that cannot
  alter a valid commercial message, does not carry the both-reference commercial
  run as a precondition. Its acceptance basis is controlled malformed-input
  tests, a caller-safety audit and valid-input preservation evidence. No
  protection in section 5 currently qualifies: name-grammar rejection (HP1) and
  the pool-cap refusal (HP3) can bind input that may be valid. Valid-input
  behavior requirements are not waived: no protection may reject or alter a
  valid commercial message, and no valid-input or compatibility claim is
  certified without the reference evidence above.

1. **No-valid-rejection proof.** For every protection, run the audit's
   legitimate-command rows L1–L11 in the default configuration and (once the
   relevant evidence is captured) in the optional mode, and prove no valid
   command is rejected or altered. A protection that rejects or alters any L row
   fails. For a reference-dependent protection the applied wire bytes and client
   state must additionally be identical to the reference; for a protection that
   qualifies as eligible default hardening, the default-path L-row result is the
   valid-input preservation evidence and no commercial-reference run is a
   precondition.
2. **Invalid-input handling.** For every protection, run the corresponding
   I1–I8 rows and R1–R9 transition rows in the default configuration and (where
   applicable) the optional mode. For a protection that qualifies as eligible
   default hardening, the controlled malformed-input outcomes, caller-safety
   audit and valid-input preservation evidence are the acceptance basis and do
   not require a commercial-reference run. For a reference-dependent protection,
   record the outcome against the reference outcome. The value-length row (I5)
   must distinguish four evidence classes: a **direct sink call**, a **direct
   `serverCommands` injection**, a **reachable `v` wire-reader path**, and a
   **reachable initial-gamestate configstring route**. The reachable `v` path is
   bounded upstream by `MSG_ReadString`, which consumes the wire string, applies
   `I_CleanChar` to each stored byte and forces `string[1023] = 0` before
   `CL_ParseCommandString` copies the result into the command slot (section 3).
   A reachable `v` test must therefore exercise that wire reader (or parse a
   `svc_serverCommand` message end to end) so byte consumption, `I_CleanChar`
   adjustment and truncation are all covered. The reachable configstring route
   is separate: it must parse an initial gamestate whose configstrings carry the
   value and reach `Dvar_SetFromStringByName` through `CL_SystemInfoChanged`
   (configstring 1 system-info pairs) and/or `CG_ParseCodInfo` (configstrings
   20–147/148–275), exercising the 8192-byte `MSG_ReadBigString` reader including
   its `%`-to-`.` mapping and its `I_CleanChar` adjustment (section 3). A value
   longer than 1023 bytes that reaches the sink only through this route proves
   the route is reachable and must not be reported as a `v`-path result. A direct
   sink test that exceeds the store bound, or a test that injects directly into
   `serverCommands`, does not model reachable server behavior and must be
   reported separately. For HP6 the corresponding row is I6, which is a `v`
   command with an **odd payload count** (arguments after the verb; even total
   `Cmd_Argc`), matching the convention in section 3.
3. **Disconnect / reconnect / map transitions.** R1–R9 remain required with the
   optional mode on and off, including the `sv_cheats` cheat-state reset (R6),
   `mapname` init (R7), the `B`/`n` `cg_thirdPerson` reset (R8) and the initial
   snapshot `fs_debug` write (R9).
4. **Pool-cap isolation.** HP3's fatal-path behavior must be exercised in an
   isolated child process, never in the main test process.
5. **Build-configuration matrix.** HP5's assert behavior must be recorded for a
   Release build (empty `MyAssertHandler`) and for an asserts-enabled build,
   separately.
6. **Both references, both directions.** For every reference-dependent
   protection, each row is recorded per reference profile and per direction
   (native client → commercial server, commercial client → native server, and the
   commercial-to-commercial baseline). A protection that qualifies as eligible
   default hardening records its controlled malformed-input, caller-safety and
   valid-input preservation evidence separately. Missing evidence stays pending,
   never passing.

## 9. Open evidence questions

- What exact `v` names do both commercial references and a supported mod emit,
  and do any fall outside `[A-Za-z0-9_]`?
- Do either reference's servers create unknown-name dvars at join, and how many
  per session?
- What is each reference's behavior when the dvar pool is exhausted?
- What value lengths do the references actually send per dvar type and path,
  and is the reachable `v` budget always below the existing-dvar 1023-byte store
  bound once the upstream `MSG_ReadString` wire reader and the non-narrowing
  `CL_ParseCommandString` command-slot copy are accounted for?
- Source inspection already identifies a second server-controlled route to
  `Dvar_SetFromStringByName`: initial-gamestate configstrings read with the
  8192-byte `MSG_ReadBigString` and delivered through `CL_SystemInfoChanged`
  (configstring 1 system-info pairs) and `CG_ParseCodInfo` (configstrings
  20–147/148–275) (section 3). The open question is therefore not whether such a
  path exists but what value lengths the references send on it, what the
  references' `Dvar_SetFromStringByName`/`Dvar_SetFromStringFromSource` do with a
  configstring value longer than the 1023-byte store bound, and whether such a
  configstring creates or alters any existing/unknown-name dvar — none of which
  can be settled from source inspection alone.
- What are the reference outcomes for `hud_drawHud` `>1`, negative and
  non-numeric, and for a `v` command with an odd payload count (even total
  `Cmd_Argc`)?

## 10. Guardrails

- No production behavior, dvar flag, allowlist, limit or default change.
- No certification of retail compatibility from source inspection.
- No change to unrelated work, including the #106 merge hold and the
  operator-owned #119 (`ki-n1et`).
- The commercial-compatibility parent #122 remains open and is the gate for any
  eventual protection.
- Any protection that conflicts with valid retail behavior stays an unresolved
  tradeoff (section 6) and blocks its own acceptance rather than relaxing the
  compatibility contract.

## 11. Citation index

All paths are relative to the repository root at
`2babfed8adb17a5b3c80db288890992ec51ec49c`.

| Topic | Citation |
|---|---|
| Client `v` dispatch and name length bound | `src/cgame_mp/cg_servercmds_mp.cpp:663-670` |
| Client dvar handler and special cases | `src/cgame_mp/cg_servercmds_mp.cpp:1359-1407` |
| Server `setclientdvar` / `setclientdvars` builders | `src/game_mp/g_client_script_cmd_mp.cpp:2097-2210`; registration `:3547-3548` |
| Name grammar check | `src/universal/dvar.cpp:305-319` |
| Unknown-name registration and pool-cap fatal | `src/universal/dvar.cpp:1556-1585`, `:1568-1571` |
| Existing-dvar string bound | `src/universal/dvar.cpp:2600-2617` |
| Upstream wire string reader (first reachable `v` truncation and `I_CleanChar` adjustment) | `src/qcommon/msg_mp.cpp:484-501`; `I_CleanChar` `src/universal/q_shared.cpp:518-524` |
| Client server-command slot (second, non-narrowing `v` copy) | `src/client_mp/client_mp.h:187`; `src/client_mp/cl_parse_mp.cpp:1192-1205` |
| cgame server-command execution from the slot | `src/client_mp/cl_cgame_mp.cpp:268-274` |
| Initial-gamestate configstring reader (reachable configstring route, 8192-byte buffer) | `src/client_mp/cl_parse_mp.cpp:1113`; `MSG_ReadBigString` `src/qcommon/msg_mp.cpp:504-527` |
| Configstring 1 system-info pairs → dvar sink | `CL_SystemInfoChanged` `src/client_mp/cl_parse_mp.cpp:171-247` (sink `:238`), called at `:1179` |
| Cod-info configstring keys/values → dvar sink | `CG_ParseCodInfo` `src/cgame_mp/cg_servercmds_mp.cpp:52-71` (sink `:69`); call sites `:973`, `src/cgame_mp/cg_main_mp.cpp:1727` |
| Domain check | `src/universal/dvar.cpp:493-543` |
| Assert policy | `src/universal/assertive.cpp:643-691` |
| Dvar type/flag definitions | `src/universal/q_shared.h:482-519` |
| Full path inventory and test matrix | [DVAR_SERVER_COMMAND_AUDIT.md](DVAR_SERVER_COMMAND_AUDIT.md) |
| Compatibility contract | [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) |
