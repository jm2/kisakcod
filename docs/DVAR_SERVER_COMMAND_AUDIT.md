# MP server-controlled dvar mutation audit and commercial test plan

Status: source inventory complete at the recorded SHA below; original-binary
behavior and acceptance remain unproven. This document is documentation only.
It does not change production behavior, dvar flags, default allowlists or
limits, and it does not certify retail compatibility.

Tracking: [issue #128](https://github.com/jm2/kisakcod/issues/128) / bead
`ki-2dkj`. The full issue acceptance remains open on `ki-7ul6`; broad native ABI
acceptance remains issue [#129](https://github.com/jm2/kisakcod/issues/129).
This report references #128 and never closes it.

Scope: the multiplayer client's paths that let server-supplied data mutate
client dvars, the numeric dvar type/flag definitions those paths exercise, the
registration/runtime checks they do and do not enforce, and the client-local
side effects they produce. It also records a concrete test matrix for the
mandatory commercial references. It does not change production code.

## 1. Provenance and evidence boundary

- **Recorded source SHA:** `a1ca543b28391f347d4127797d971fdc0bc5a199`
  (`a1ca543b`, the `origin/master` tip when this stage started; merge of
  fork PR #139). Every line citation in this document was read from that
  exact checkout, and file paths are repository-relative.
- **Evidence class:** direct source inspection at the recorded SHA. No
  executable, no wire capture, no original commercial binary, no licensee data
  and no reference-server session were used. Nothing here is a runtime result.
- **Scope boundary:** the mandatory contract in
  [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) governs this work.
  Perfect interoperation with unmodified original commercial 1.7 and
  unmodified Steam commercial 1.8 is required and is still **unproven**.
  Community CoD4x "1.8" is a different reference and is not a substitute.
- **Numbers:** all counts, bit values and type ordinals below were derived from
  the recorded SHA. They were not copied from older issue text; where the
  audit disagrees with older prose, the source at this SHA wins.

## 2. Reading the boundary: type, flag, authorization, side effect

The A06 review question is not "is there a generic setter" but "what does the
server actually reach, and what does each layer enforce". This audit separates
four concerns that must not be conflated:

1. **Storage type** — `DvarType` (`src/universal/q_shared.h:482-494`), the
   ordinal that determines parsing and clamping.
2. **Flag bits** — `DvarFlags` (`src/universal/q_shared.h:496-519`), metadata
   such as archive/serverinfo/ROM/cheat/latch/external.
3. **Authorization** — the source-gated checks in `Dvar_SetVariant`
   (`src/universal/dvar.cpp:1319-1395`) and validation in the server script
   command wrappers.
4. **Side effects** — writes to `cg_s` client state and dvar storage that
   follow a successful set, including string copies and the modification
   bitmask.

A flag value alone authorizes nothing; authorization depends on the
`DvarSetSource` and on which entry point the caller used. This distinction is
the core result of section 7.

## 3. Dvar type and flag definitions at this SHA

`DvarType` (`src/universal/q_shared.h:482-494`):

| Ordinal | Name | Parsing entry |
|---|---|---|
| `0x0` | `DVAR_TYPE_BOOL` | `Dvar_StringToBool` (`dvar.cpp:1855`) |
| `0x1` | `DVAR_TYPE_FLOAT` | `Dvar_StringToFloat` (`dvar.cpp:1858`) |
| `0x2` | `DVAR_TYPE_FLOAT_2` | `Dvar_StringToVec2` (`dvar.cpp:1861`, `1894-1901`) |
| `0x3` | `DVAR_TYPE_FLOAT_3` | `Dvar_StringToVec3` (`dvar.cpp:1864`, `1903-1914`) |
| `0x4` | `DVAR_TYPE_FLOAT_4` | `Dvar_StringToVec4` (`dvar.cpp:1867`, `1916-1925`) |
| `0x5` | `DVAR_TYPE_INT` | `Dvar_StringToInt` (`dvar.cpp:1870`) |
| `0x6` | `DVAR_TYPE_ENUM` | `Dvar_StringToEnum` (`dvar.cpp:1873`, `1927-1960`) |
| `0x7` | `DVAR_TYPE_STRING` | pointer stored, copied on write |
| `0x8` | `DVAR_TYPE_COLOR` | `Dvar_StringToColor` (`dvar.cpp:1879`) |
| `0x9` | `DVAR_TYPE_COUNT` | count sentinel, not a stored type |

`DvarFlags` (`src/universal/q_shared.h:496-519`), as observed bit values:

| Bit | Name | Observed meaning |
|---|---|---|
| `0x0` | `DVAR_NOFLAG` | no bits |
| `0x1` | `DVAR_ARCHIVE` | saved to config |
| `0x2` | `DVAR_USERINFO` | sent to server on connect/change |
| `0x4` | `DVAR_SERVERINFO` | sent in front-end responses |
| `0x8` | `DVAR_SYSTEMINFO` | replicated to clients when host |
| `0x10` | `DVAR_INIT` | blocked for external/script writes |
| `0x20` | `DVAR_LATCH` | external/script writes latch, not apply |
| `0x40` | `DVAR_ROM` | blocked for external/script writes |
| `0x80` | `DVAR_CHEAT` | blocked for external writes when cheats off |
| `0x100` | `DVAR_TEMP` | temporary |
| `0x200` | `DVAR_AUTOEXEC` | autoexec-related |
| `0x400` | `DVAR_NORESTART` | survives `cvar_restart` |
| `0x800` | (unnamed in `DvarFlags`) | only referenced by the DEVGUI latch check at `dvar.cpp:1396` |
| `0x1000` | `DVAR_SAVED` | saved state |
| `0x4000` | `DVAR_EXTERNAL` | created by a `set`/`setclientdvar` path |
| `0x8000` | `DVAR_CHANGEABLE_RESET` | reset may change |

Store layout: `struct dvar_s` (`src/universal/q_shared.h:606-617`) carries
`flags` as a 16-bit `word` (`:609`) and `type` as a `byte` (`:610`). The
`DvarValue` union (`:521-554`) unions `enabled`, `integer`, `unsignedInt`,
`value`, `vector[4]`, `string` and `color[4]`; this is why a "string set" of a
numeric dvar is a parse into the same union. `dvar_s` comment (`:605`) states
that nothing outside the `Dvar_*` functions should modify these fields.

`DvarSetSource` (`src/qcommon/qcommon.h:322-328`):
`DVAR_SOURCE_INTERNAL = 0x0`, `DVAR_SOURCE_EXTERNAL = 0x1`,
`DVAR_SOURCE_SCRIPT = 0x2`, `DVAR_SOURCE_DEVGUI = 0x3`.

## 4. Runtime write gate: `Dvar_SetVariant`

`Dvar_SetVariant` (`src/universal/dvar.cpp:1319-1395`) is the single write
funnel. For every successful set it does:

1. Domain check `Dvar_ValueInDomain` (`:1338`, implementation `:493-543`). An
   out-of-domain value is rejected with a console message; an out-of-domain
   `ENUM` falls back to the reset value (`:1344-1355`).
2. Optional `domainFunc` rejection (`:1358-1370`).
3. Source-gated flag checks. The guard block at `:1371` runs **only** when
   `source == DVAR_SOURCE_EXTERNAL || source == DVAR_SOURCE_SCRIPT`:
   - `DVAR_ROM` (`0x40`) rejects (`:1373-1377`);
   - `DVAR_INIT` (`0x10`) rejects (`:1378-1382`);
   - `DVAR_CHEAT` (`0x80`) rejects for `DVAR_SOURCE_EXTERNAL` when
     `dvar_cheats` is off (`:1383-1387`);
   - `DVAR_LATCH` (`0x20`) latches instead of applying (`:1388-1394`).
4. `DVAR_SOURCE_DEVGUI` only latches `0x800` flags (`:1396-1400`).
5. Otherwise the current/latched value is updated and
   `dvar_modifiedFlags |= dvar->flags` (`:1401-1407`).

Consequence: a call with `DVAR_SOURCE_INTERNAL` ignores the `ROM`, `INIT`,
`CHEAT` and `LATCH` guards entirely. That is not itself a defect in the
original paths, but it is the property that determines what a server-controlled
path can do. Section 5 shows the MP server paths call the `DVAR_SOURCE_INTERNAL`
wrappers.

## 5. MP server-controlled dvar mutation paths

### 5.1 Reliable game-server command `v` (the `setclientdvar` path)

Server side (gametype script builtin):

- `PlayerCmd_SetClientDvar` (`src/game_mp/g_client_script_cmd_mp.cpp:2097-2155`)
  reads the dvar name and value, validates the name with `Dvar_IsValidName`
  (`:2135`), copies and sanitizes the value (`I_CleanChar`, `"` to `'`,
  `:2139-2146`), then formats `"%c %s \"%s\""` with character `118`
  (`'v'`, `:2147`) and sends it `SV_CMD_RELIABLE` (`:2148`).
- `PlayerCmd_SetClientDvars` (`:2157-2210`) requires an even parameter count
  (`:2182`), validates each name (`:2190`), sanitizes each value (`:2196-2202`),
  builds one `v` command with multiple name/quote-value pairs
  (`finalString` initialized to `"v"` at `:2185`) and sends it reliably
  (`:2209`).
- Both builtins are registered as `setclientdvar` / `setclientdvars`
  (`:3547-3548`).

Delivery: `SV_GameSendServerCommand` (`src/server/sv_game.cpp:944-956`) routes
`clientNum == -1` to all clients and otherwise to
`svs.clients[clientNum]`; `SV_CMD_RELIABLE = 0x1`
(`src/server_mp/server_mp.h:23-24`).

Client side (cgame MP):

- `CG_ServerCommand` (`src/cgame_mp/cg_servercmds_mp.cpp:391-395`) calls
  `CG_DeployServerCommand`, which switches on the first byte of the command
  token (`:461`). `case 0x76` (`'v'`, `:663`) loops in steps of two
  (`:664`), reads name `Cmd_Argv(i)` (`:666`) and value `Cmd_Argv(i+1)`
  (`:668`), and calls `CG_SetClientDvarFromServer` (`:669`).
- `CG_SetClientDvarFromServer` (`:1359-1382`) special-cases three names and
  otherwise calls `Dvar_SetFromStringByName(dvarname, value)` (`:1368`).
- `Dvar_SetFromStringByName` (`src/universal/dvar.cpp:2711-2714`) delegates to
  `Dvar_SetFromStringByNameFromSource(..., DVAR_SOURCE_INTERNAL)`. That
  function (`:2700-2709`) looks up the dvar (`:2704`); if it does not exist it
  **registers a new string dvar with `DVAR_EXTERNAL`** (`:2706`, see
  `Dvar_RegisterString` at `:2163-2185`); if it exists it parses and writes
  with `Dvar_SetFromStringFromSource` (`:2707`, `:2600-2617`).

The two branches then follow different storage paths, and they do not share one
length limit:

- **Existing dvar** (`Dvar_SetFromStringFromSource`, `:2600-2617`): the value
  is copied into a 1028-byte stack buffer with `I_strncpyz(buf, string, 1024)`
  (`:2607`). `I_strncpyz` (`src/universal/q_shared.cpp:45-53`) runs
  `strncpy(dest, src, destsize - 1)` and terminates `dest[destsize - 1]`, so
  this keeps at most **1023 payload bytes plus the NUL**, and the parse at
  `:2608-2615` sees a value already truncated at 1023 bytes. A 1024-byte (or
  longer) payload is therefore truncated before it is interpreted.
  (`Dvar_SetStringFromSource`, used by `Dvar_SetStringByName`, uses the same
  1024 capacity for the `DVAR_TYPE_STRING` case at `:2573`.)
- **Unknown name** (`Dvar_RegisterString` → `Dvar_RegisterVariant` →
  `Dvar_RegisterNew`, `:2163-2185`, `:1763-1786`, `:1556-1632`): the supplied
  string is stored through `Dvar_CopyString` (`:1300-1305`) → `CopyString`
  (`src/universal/com_memory.cpp:339-344`) → `SL_GetString_`/`SL_GetStringOfSize`
  (`src/script/scr_stringlist.cpp:935-940`). That is the script-string store,
  not the 1024-byte stack buffer, and it accepts up to the script-string limit
  (byte count at most 65531 including the terminator, `scr_stringlist.cpp:963-985`).
  An unknown-name value is therefore **not** truncated at 1024 bytes by this
  path.

Observed validation asymmetry: the server script interface validates names
with `Dvar_IsValidName` (`g_client_script_cmd_mp.cpp:2135`, `:2190`), but the
client-side `v` handler does not validate the name before
`Dvar_SetFromStringByName`. A malformed or hostile server can therefore reach
`Dvar_RegisterString` with a name that the legitimate script API would have
rejected; `Dvar_RegisterString`/`Dvar_RegisterVariant`/`Dvar_RegisterNew` do
not reject names, and external names are copied via `Dvar_AllocNameString`
(`dvar.cpp:1578-1581`). This is an observed source property, not a claim that
the original commercial server ever sends such a name.

### 5.2 Serverinfo configstring (cgame, index 0)

`CG_ConfigStringModified` (`cg_servercmds_mp.cpp:853-982`) selects behavior by
configstring index. Index `0` calls `CG_ParseServerInfo` (`:976-978`), which
reads `CL_GetConfigString(localClientNum, 0)` (`:39`) and, when
`!cgs->localServer` (`:44`), calls `Dvar_SetStringByName("g_gametype",
cgs->gametype)` (`:45`). `g_gametype` is registered as a string dvar on both
the server (`src/game_mp/g_main_mp.cpp:386`,
`src/server_mp/sv_init_mp.cpp:698`) and the game VM, so this
is a string parse, not an enum parse. `Dvar_SetStringByName`
(`dvar.cpp:2689-2698`) is another `DVAR_SOURCE_INTERNAL` path (via
`Dvar_SetString`, `:2555-2557`).

The same configstring also feeds an independent **client** (not cgame) write on
map initialization. `CL_InitCGame` reads the `mapname` key from the serverinfo
string (`src/client_mp/cl_cgame_mp.cpp:750-753`): `Info_ValueForKey` on
`gameState.stringOffsets[0]` (`:751`), `I_strncpyz(mapname, v1, 64)` into a
68-byte local (`:752`), then `Dvar_SetStringByName("mapname", mapname)`
(`:753`). This runs whenever cgame is initialized for a remote server, i.e. on
startup and map transitions, and is separate from `CG_ParseServerInfo`'s
`g_gametype` sink. The caller's own 64-byte copy bounds the value to 63 payload
bytes before the setter. It is a second server-controlled sink inside mutation
family #2 of the section 5.8 summary, not a new family.

### 5.3 "cod" info configstring block (cgame, indices 20-275)

For indices in `[20, 276)` `CG_ConfigStringModified` calls `CG_ParseCodInfo`
(`:971-973`). `CG_ParseCodInfo` (`:52-72`) walks up to 128 pairs: key at
configstring `20+i` (`:65`), value at `148+i` (`:68`), stopping at the first
empty key (`:66-67`); each pair goes through
`Dvar_SetFromStringByName(key, value)` (`:69`). It is gated on
`!cgs->localServer` (`:61`). The bound 128 and the 20/148 split are read from
the loop at this SHA, not copied from issue prose.

### 5.4 Systeminfo key/value expansion (client, configstring 1)

`CL_SystemInfoChanged` (`src/client_mp/cl_parse_mp.cpp:171-245`) reads the
systeminfo string from `gameState.stringOffsets[1]` (`:194`) and, when not
running the local server (`:204`, `:230`), iterates key/value pairs
(`Info_NextPair`, `:235`) and calls
`Dvar_SetFromStringByName(key, value)` (`:238`) for each. This means every
systeminfo key the server replicates is a candidate client dvar registration
or write through the same `DVAR_SOURCE_INTERNAL` path. It also sets
`cl_connectedToPureServer` from `sv_pure` (`:241`).

Before that key/value expansion, the same function performs a bulk
client-local reset. When the local server is not running and
`clientUIActives[0].connectionState < CA_ACTIVE` (`cl_parse_mp.cpp:214`), it
reads `sv_cheats` from the systeminfo string (`:216-217`) and, when the value
is zero, calls `Dvar_SetCheatState()` (`:218`). `Dvar_SetCheatState`
(`src/universal/dvar.cpp:2778-2791`) walks the whole dvar pool and, for every
dvar whose flags include `DVAR_CHEAT` (`0x80`), writes its **reset** value with
`DVAR_SOURCE_INTERNAL`. On the initial remote connection this therefore
overwrites preexisting client cheat-dvar values before any server key/value
pair is applied, so a disconnect/reconnect test must record the preexisting
values and their reset outcome.

### 5.5 Indirect local-file path: shellshock configstrings

For configstring indices `[1954, 1970)` `CG_ConfigStringModified` uses the
string as a shock file name and, if non-empty and loadable, calls
`BG_LoadShellShockDvars` (`cg_servercmds_mp.cpp:952-956`). That function
(`src/bgame/bg_misc.cpp:1985-2014`) loads `shock/<name>.shock` from the
client's own files (`:1994-1998`) and applies it through
`Com_LoadDvarsFromBuffer` (`:2005`). `Com_LoadDvarsFromBuffer`
(`src/universal/dvar.cpp:2862-2924`) resets each named dvar with
`DVAR_SOURCE_INTERNAL` (`:2883`) and then writes parsed values via
`Dvar_SetFromString` (`:2905`), also internal. This path is server-influenced
only through the *name* of a local file; the values come from client-side data.
It is included for completeness and is not a wire-value-controlled dvar path.

### 5.6 Map-restart server command (`B` / `n`)

A fast map restart is signalled by a direct reliable game-server command byte,
independently of the `v` family. The byte is emitted only from the fast-restart
branch of `SV_MapRestart`; the entry points differ:
`SV_MapRestart_f` calls `SV_MapRestart(0)` (`src/server_mp/sv_ccmds_mp.cpp:451-453`)
and `SV_FastRestart_f` calls `SV_MapRestart(1)` (`:536-539`). With
`fast_restart == 0` the `!fast_restart` term makes the condition at `:476` true,
so control takes the `SV_SpawnServer(mapname)` branch at `:478-483` and never
reaches the reliable emission. Only the `fast_restart != 0` case can fall through
to the `else if (com_frameTime != sv.start_frameTime)` branch at `:484-528`,
which emits the byte. `SV_MapRestart` sends
`va("%c", savepersist != 0 ? 110 : 66)` through
`SV_AddServerCommand(..., SV_CMD_RELIABLE, ...)`
(`src/server_mp/sv_ccmds_mp.cpp:512-513`); the byte is `'n'` (`0x6E`) when
`savepersist` is set and `'B'` (`0x42`) otherwise. `SV_CMD_RELIABLE = 0x1`
(`src/server_mp/server_mp.h:24`).

Client side, `CG_DeployServerCommand` (`cg_servercmds_mp.cpp:461`) dispatches
`case 0x42` to `CG_MapRestart(localClientNum, 0)` (`:465-467`) and `case 0x6E`
to `CG_MapRestart(localClientNum, 1)` (`:613-615`). `CG_MapRestart` (defined at
`:212`) unconditionally executes `Dvar_SetBool(cg_thirdPerson, 0)` (`:249`).
`Dvar_SetBool` (`src/universal/dvar.cpp:2535-2538`) delegates to
`Dvar_SetBoolFromSource(..., DVAR_SOURCE_INTERNAL)` (`:2537`), so the write
uses the same internal source as family #1 and does not consult
`cg_thirdPerson`'s `DVAR_CHEAT` flag. `cg_thirdPerson` is registered as a
boolean with `DVAR_CHEAT` at `src/cgame_mp/cg_main_mp.cpp:893`
(`Dvar_RegisterBool("cg_thirdPerson", 0, DVAR_CHEAT, ...)`).

This is a sixth mutation family: a direct game-server command that makes the
client perform an internal write to a client cheat dvar. Unlike families #1-#5
it carries no name/value payload; the mutation is the fixed `cg_thirdPerson = 0`
reset inside the restart handler, and it fires on every `B`/`n` byte received
because the reset at `:249` precedes the `if (!savepersist)` block, so both
`savepersist` values run the same `Dvar_SetBool`.

Within KisakCOD's own normal server-side emission path, the `B`/`n` byte is
produced only by the fast-restart branch: `map_restart` (`SV_MapRestart(0)`)
spawns a new server at `:478-483` before reaching `:512-513`, while
`fast_restart` (`SV_MapRestart(1)`) emits it from the fast-restart branch when
the condition at `:476` is false (`sv_maxclients` unmodified, gametype
unchanged) and `com_frameTime != sv.start_frameTime`. That is an attribution of
KisakCOD's server path, not a property the byte carries. The client cannot
establish the sender's code path: `CG_DeployServerCommand` dispatches solely on
the received first byte (`src/cgame_mp/cg_servercmds_mp.cpp:461`, `case 0x42`
`'B'` at `:465-467`, `case 0x6E` `'n'` at `:613-615`), so a custom or hostile
server that places `B`/`n` in its reliable command stream reaches the same
handler and the same fixed reset. The client-side mutation below therefore holds
for any origin; only the emission claim is limited to KisakCOD's normal
server-side path.

### 5.7 Initial-snapshot debug dvar write (`fs_debug`)

Processing the first usable server snapshot performs a fixed internal dvar write
that no name/value payload drives. `CG_ProcessSnapshots`
(`src/cgame_mp/cg_snapshot_mp.cpp:566`) runs from `CG_DrawActiveFrame`
(`src/cgame_mp/cg_view_mp.cpp:1332`). When a snapshot whose `snapFlags` lack bit
`2` becomes the initial snapshot (`cg_snapshot_mp.cpp:590-598`), the function
runs `CG_SetInitialSnapshot`, `CG_SetNextSnap` and `CG_TransitionSnapshot` and
then:

```c
if (!cg_fs_debug->current.integer)
    Dvar_SetInt(cg_fs_debug, 2);
```

(`src/cgame_mp/cg_snapshot_mp.cpp:596-597`).

`cg_fs_debug` is registered as the integer dvar `fs_debug` with default `0` and
`DVAR_NOFLAG` (`src/cgame_mp/cg_main_mp.cpp:920-925`). `Dvar_SetInt`
(`src/universal/dvar.cpp:2540-2542`) delegates to
`Dvar_SetIntFromSource(..., DVAR_SOURCE_INTERNAL)` (`:2333`), so the write uses
the same internal source as families #1-#6; `fs_debug` is not `DVAR_CHEAT`, so
that flag is irrelevant here. The written value `2` is a fixed literal and not
server-supplied data: the server controls only the timing, namely that a first
usable snapshot was received after cgame initialization.

This is a seventh mutation family: a server-event-triggered internal write to a
client dvar with no wire payload. It is transient rather than a lasting
override — `CG_Init` resets `fs_debug` back to `0` when it observes the value
`2` (`src/cgame_mp/cg_main_mp.cpp:1757-1758`) — so a test must record the
in-window value, not just the post-frame final state. The snapshot is normal
server traffic; the family is listed on the same reasoning that already counts
the `B`/`n` fixed reset (section 5.6) as a family, because the audited property
is the server-triggered internal mutation, not payload control.

### 5.8 Path summary

| # | Entry | Server input | Client sink | Source | Validation |
|---|---|---|---|---|---|
| 1 | `v` reliable command | `setclientdvar(s)` values | `CG_SetClientDvarFromServer` → `Dvar_SetFromStringByName` | INTERNAL | server script name check only; client none |
| 2 | configstring 0 | serverinfo `g_gametype` | `CG_ParseServerInfo` → `Dvar_SetStringByName` | INTERNAL | domain parse |
| 2b | configstring 0 (map init) | serverinfo `mapname` | `CL_InitCGame` → `Dvar_SetStringByName` | INTERNAL | caller copy to 64-byte buffer |
| 3 | configstrings 20-275 | key/value blocks | `CG_ParseCodInfo` → `Dvar_SetFromStringByName` | INTERNAL | domain parse |
| 4 | configstring 1 | systeminfo pairs | `CL_SystemInfoChanged` → `Dvar_SetFromStringByName` | INTERNAL | domain parse |
| 4b | configstring 1 (initial connect) | systeminfo `sv_cheats` 0 | `CL_SystemInfoChanged` → `Dvar_SetCheatState` (bulk reset of every `DVAR_CHEAT` dvar) | INTERNAL | none |
| 5 | configstrings 1954-1969 | shock file name | `BG_LoadShellShockDvars` → local file → internal set | INTERNAL | client local file lookup |
| 6 | `B` (0x42) / `n` (0x6E) reliable command | restart byte only | `CG_MapRestart` → `Dvar_SetBool(cg_thirdPerson, 0)` | INTERNAL | none (fixed reset of one `DVAR_CHEAT` dvar) |
| 7 | Initial usable server snapshot | snapshot processing only | `CG_ProcessSnapshots` → `Dvar_SetInt(fs_debug, 2)` | INTERNAL | none (fixed write of one int dvar; `CG_Init` resets it to `0`) |

## 6. Special cases and client-local side effects

`CG_SetClientDvarFromServer` (`cg_servercmds_mp.cpp:1359-1382`) intercepts
three names with case-insensitive comparison:

| Name | Handler | Effect | Citation |
|---|---|---|---|
| `cg_objectiveText` | `CG_SetObjectiveText` | `I_strncpyz(cgameGlob->objectiveText, text, 1024)` | `:1384-1387`; field `cg_local_mp.h:102` |
| `hud_drawHud` | `CG_SetDrawHud` | `atoi(value)`, assert `value <= 1`, then `cgameGlob->drawHud = value` | `:1389-1400`; field `cg_local_mp.h:119` |
| `g_scriptMainMenu` | `CG_SetScriptMainMenu` | assert `text`, then `I_strncpyz(cgameGlob->scriptMainMenu, text, 256)` | `:1402-1407`; field `cg_local_mp.h:103` |

These three are deliberately *not* dvar writes; they write cgame state. Note
the assert at `:1391` is a direct `MyAssertHandler` call, while the project's
`iassert` macro compiles out when `USE_ASSERTS` is undefined
(`src/universal/assertive.h:3-31`, `src/universal/assertive.cpp:685-691`).
In a non-PURE Release build `MyAssertHandler` has an empty body, so the
`hud_drawHud > 1` check does not stop the assignment in that configuration.
Any compatibility test must record the build's assert/PURE configuration
rather than assume the check enforces.

Generic-path side effects after a successful set are: possible new dvar
registration (`dvar.cpp:2706`); for an existing dvar, an internal copy into a
1024-capacity buffer by `I_strncpyz` (`:2607`) that keeps at most 1023 payload
bytes plus the terminator; `Dvar_SetVariant`'s value update and
`dvar_modifiedFlags |= dvar->flags` (`:1407`); and the invalid-enum console
message plus reset fallback (`:2610-2615`). Enum parsing is done by
`Dvar_StringToEnum` (`:1927-1960`); the sentinel `-1337` means "no match". A
newly registered unknown-name dvar does not pass through the 1024-capacity
buffer: `Dvar_CopyString` → `CopyString`/`SL_GetString_` stores it in the
script-string table (section 5.1), so the 1023-byte bound applies only to the
existing-dvar update path.

A further client-local side effect is the map-restart reset (section 5.6): the
`B`/`n` command drives `CG_MapRestart` → `Dvar_SetBool(cg_thirdPerson, 0)`
(`cg_servercmds_mp.cpp:249`), an internal write to a `DVAR_CHEAT` dvar that
does not come from a `v` name/value payload. As with the section 5.4
cheat-state reset, the reset value is applied regardless of the dvar's
preexisting value, so a restart test must record the pre-restart
`cg_thirdPerson` value and the post-restart reset outcome.

A further client-local side effect is the initial-snapshot debug write (section
5.7): processing the first usable server snapshot with `fs_debug == 0` calls
`Dvar_SetInt(cg_fs_debug, 2)` (`src/cgame_mp/cg_snapshot_mp.cpp:596-597`), which
reaches `Dvar_SetIntFromSource(..., DVAR_SOURCE_INTERNAL)`
(`src/universal/dvar.cpp:2540-2542`, `:2333`). The value is a fixed literal, not
a server payload, and `CG_Init` later resets `fs_debug` to `0` when it is `2`
(`src/cgame_mp/cg_main_mp.cpp:1757-1758`). A snapshot-processing test must record
the value while the snapshot is being applied, not only the settled frame state.

## 7. What the numeric flags actually authorize

At this SHA the MP server-controlled paths reach the `DVAR_SOURCE_INTERNAL`
wrappers (`dvar.cpp:2713`, `:2557`). Because the flag guard at
`dvar.cpp:1371` is gated on `EXTERNAL`/`SCRIPT`, a server-set of an existing
dvar does **not** consult `DVAR_ROM` (`0x40`), `DVAR_INIT` (`0x10`),
`DVAR_CHEAT` (`0x80`) or `DVAR_LATCH` (`0x20`). This is a statement about
authorization flow, not about exploitability and not about what the original
commercial engine did. Two concrete consequences for the test plan:

- A production `DVAR_ROM` or `DVAR_CHEAT` flag is not, by itself, a barrier to
  a server-controlled set through these paths.
- Any hardening proposal that adds a "broad new allowlist" or new limits would
  change which legitimate server writes still apply. The compatibility
  contract in [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md) forbids
  rejecting valid retail behavior, so such a change requires original-reference
  evidence first.

Conversely, the server script interface's `Dvar_IsValidName`
(`dvar.cpp:305-319`; `[A-Za-z0-9_]` only) and the `"`→`'` sanitization
(`g_client_script_cmd_mp.cpp:2141-2146`, `:2197-2202`) are *authorization and
encoding* behavior of the legitimate path, not wire-format requirements. The
client handler (section 5.1) has neither check. Any proposed validation must be
evaluated against original behavior and against both the legit path and
forged/hostile input.

## 8. Observed behavior vs claimed commercial behavior

Observed at `a1ca543b` (source inspection only):

- Multiplayer has exactly seven dvar-mutation families in section 5.8; #1 and #6
  are direct game-server commands, #2-#4 are configstring-derived, #5 is a
  local-file indirection, and #7 is triggered by initial server-snapshot
  processing.
- The `v` command pair loop reads arguments at `i` and `i+1` without checking
  an even count; `Cmd_Argv` returns `""` past the end
  (`src/qcommon/cmd.cpp:103-112`), so a trailing odd argument degrades to an
  empty value.
- `CG_SetClientDvarFromServer` normalizes names case-insensitively for its
  three special cases (`I_stricmp`), so `CG_ObjectiveText` and
  `cg_objectivetext` take the same branch as the canonical spelling.
- For an **existing** dvar, `Dvar_SetFromStringFromSource` copies the value
  with `I_strncpyz(buf, string, 1024)` (`dvar.cpp:2607`), so the parse/store
  path sees at most **1023 payload bytes plus the NUL terminator**; a
  1024-byte payload, and any longer payload, is already truncated before
  parsing. An **unknown** name does not pass through that buffer:
  `Dvar_RegisterString`/`Dvar_RegisterNew` store the supplied string via
  `Dvar_CopyString` → `CopyString`/`SL_GetString_` (`dvar.cpp:1300-1305`,
  `src/universal/com_memory.cpp:339-344`, `src/script/scr_stringlist.cpp:935-940`),
  whose lexical limit is the script-string limit (65531 bytes including the
  terminator, `scr_stringlist.cpp:963-985`), not 1024.
- New server-named dvars are created with `DVAR_EXTERNAL` (`0x4000`) and an
  allocated name (`dvar.cpp:1578-1581`). When the pool already holds 4096
  dvars, `Dvar_RegisterNew` calls `Com_Error(ERR_FATAL, ...)`
  (`dvar.cpp:1568-1571`), so a unique unknown-name flood is a client-fatal
  condition, not a bounded rejection. Whether that fatal behavior is
  compatible with retail references is unresolved and is carried as the I8
  acceptance question in section 9.2; this document does not change it.
- `CL_InitCGame` also sets the client's `mapname` dvar from serverinfo
  configstring 0 on map initialization (`src/client_mp/cl_cgame_mp.cpp:750-753`),
  a second server-controlled configstring-0 sink beyond `CG_ParseServerInfo`.
- On the initial remote connection with systeminfo `sv_cheats` false,
  `CL_SystemInfoChanged` calls `Dvar_SetCheatState`
  (`src/client_mp/cl_parse_mp.cpp:214-219`), resetting every `DVAR_CHEAT`
  dvar through the internal source (`src/universal/dvar.cpp:2778-2791`).
- The direct game-server command bytes `B` (`0x42`) and `n` (`0x6E`) call
  `CG_MapRestart`, which resets `cg_thirdPerson` to `0` through the internal
  source (`src/cgame_mp/cg_servercmds_mp.cpp:465-467`, `:613-615`, `:212`,
  `:249`; registration at `src/cgame_mp/cg_main_mp.cpp:893`), so a
  `DVAR_CHEAT` client dvar changes without any `v` name/value payload.
- Processing the first usable server snapshot with `fs_debug == 0` calls
  `Dvar_SetInt(cg_fs_debug, 2)` from `CG_ProcessSnapshots`
  (`src/cgame_mp/cg_snapshot_mp.cpp:596-597`; caller
  `src/cgame_mp/cg_view_mp.cpp:1332`), reaching the internal source
  (`src/universal/dvar.cpp:2540-2542`) with no `v` name/value payload. The
  value is a fixed literal, and `CG_Init` resets `fs_debug` to `0` when it is
  `2` (`src/cgame_mp/cg_main_mp.cpp:1757-1758`).
- Re-registering an existing external dvar with a concrete type is handled by
  `Dvar_Reregister`/`Dvar_MakeExplicitType`
  (`dvar.cpp:1727-1735`, `:1788-1843`).

Not established by this document (must not be inferred):

- Whether original commercial 1.7 and Steam 1.8 send the same `v` payloads,
  quoting, pair ordering, configstring layout or systeminfo key sets.
- Whether the original engines enforce the same name/flag behavior at each
  layer.
- Whether any reachable input is exploitable; no runtime analysis was done.

## 9. Reference test matrix (commercial 1.7 and Steam 1.8)

Each cell is a required test. "Ref" means it must be run against a pinned,
unmodified reference; reference manifests are currently unavailable
(section 10), so the reference columns start blocked and unproven. A
KisakCOD-to-KisakCOD run is supplemental and cannot fill a Ref cell.

Legend: `KC→KC` = native client against native server; `RefC→KC` = original
commercial client against native server; `KC→RefS` = native client against
original commercial server; `Ref` = both original references, each separately.

### 9.1 Legitimate commands and mod cases

| ID | Input | Target | Expected invariant | Required evidence |
|---|---|---|---|---|
| L1 | `setclientdvar <known dvar> "<value>"` | KC→KC, RefC→KC, KC→RefS | accepted; stored value equals sent value per type | wire + state capture, Ref |
| L2 | `setclientdvars` with N pairs | KC→KC, RefC→KC, KC→RefS | all pairs applied in order | wire + state capture, Ref |
| L3 | special `cg_objectiveText` | KC→KC, RefC→KC, KC→RefS | `objectiveText` set, no dvar created | state + Ref |
| L4 | special `hud_drawHud` `0`/`1` | KC→KC, RefC→KC, KC→RefS | `drawHud` set | state + Ref |
| L5 | special `g_scriptMainMenu` | KC→KC, RefC→KC, KC→RefS | `scriptMainMenu` set | state + Ref |
| L6 | value with spaces / quotes / backslashes | KC→KC, RefC→KC, KC→RefS | quoting round-trips; `"` handled per reference | byte-level Ref |
| L7 | each dvar type `bool/float/vec2/vec3/vec4/int/enum/string/color` | KC→KC, RefC→KC, KC→RefS | parse and domain behavior matches reference | type table + Ref |
| L8 | serverinfo `g_gametype` change | KC→KC, RefC→KC, KC→RefS | client `g_gametype` follows server | Ref |
| L9 | cod-info configstring pairs (up to 128) | KC→KC, RefC→KC, KC→RefS | each key/value applies | Ref |
| L10 | systeminfo key/value expansion | KC→KC, RefC→KC, KC→RefS | each replicated key applies or registers | Ref |
| L11 | legitimate mod that sets display dvars on join | KC→KC, RefC→KC, KC→RefS | mod semantics preserved | mod fixture + Ref |

Per-row `KC→RefS` applicability and fixtures: every legitimate-input row now
requires the native client → original commercial server direction. For L2–L5,
L7 and L11 the `v`/dvar traffic is produced by a gametype/script mod running on
the unmodified original commercial server; that fixture is part of the required
evidence and does not replace the reference. L9's cod-info block is observed to
be *consumed* by this client (section 5.3); whether either original server
*emits* it is itself a KC→RefS observation, and an original server that turns
out not to emit it is recorded as an evidence-backed `N/A` for that reference,
never silently skipped. L10's systeminfo is emitted by both original servers.
No cell is considered passed until it has a recorded result for both commercial
1.7 and Steam 1.8 profiles.

### 9.2 Invalid / hostile inputs

| ID | Input | Expected invariant | Required evidence |
|---|---|---|---|
| I1 | invalid name (non `[A-Za-z0-9_]`) via script | script error, no send (legit path) | KC→KC + Ref behavior |
| I2 | invalid name forged in a `v` command | defined client behavior; compare to reference; no memory/safety defect | KC→KC plus security review |
| I3 | enum value not in the domain | console message and reset fallback; no crash | KC→KC + Ref |
| I4 | numeric values out of domain / non-numeric | domain rejection/clamp matches reference | KC→KC + Ref |
| I5 | existing-dvar value of 1023, 1024 and longer payloads; unknown-name value of the same lengths | existing dvar stores at most 1023 payload bytes plus NUL (`I_strncpyz(...,1024)`), so a 1024-byte value is already truncated; an unknown name registers through `Dvar_CopyString`/script strings and is **not** cut at 1024; no overflow either way | KC→KC + fuzz, per path |
| I6 | `v` with odd argument count | empty trailing value, no OOB | KC→KC |
| I7 | `hud_drawHud` negative, non-numeric and > 1 values | `atoi` conversion: `> 1` (including a negative input converted to `uint32_t`) reaches `MyAssertHandler`; non-numeric becomes `0` and is assigned; record Release (empty `MyAssertHandler`) and asserts builds separately; no runtime evidence invented from source | KC→KC (Release and asserts build) |
| I8 | unknown-name flood reaching the 4096-dvar cap | observed source behavior is `Com_Error(ERR_FATAL)` from `Dvar_RegisterNew`; a client-fatal exit is not a bounded rejection and its compatibility acceptance is unresolved — run an isolated fatal-path test in a child process, and do not add production limits or silently accept the outcome | KC→KC + isolated fatal-path run |

### 9.3 Reconnect and map transitions

| ID | Sequence | Expected invariant | Required evidence |
|---|---|---|---|
| R1 | connect → `v` set → disconnect → reconnect | dvar persistence/reset matches reference | Ref |
| R2 | `map <name>` / `map_restart` | server-replicated values reapply; local resets match reference (the fast-restart `cg_thirdPerson` reset is R8) | Ref |
| R3 | `setclientdvar` → `cvar_restart` | `DVAR_NORESTART`/archive behavior matches reference | Ref |
| R4 | external dvar created by server → map change | registration/lifetime matches reference | Ref |
| R5 | pure-check / download boundary | `sv_pure` handling unchanged | Ref |
| R6 | initial remote connection with systeminfo `sv_cheats` 0 | every `DVAR_CHEAT` dvar is reset to its reset value before systeminfo pairs apply; preexisting client cheat values do not survive | Ref |
| R7 | cgame/map init from serverinfo configstring 0 | client `mapname` follows serverinfo `mapname` on startup and each map transition | Ref |
| R8 | fast restart signalling `B` (`0x42`) / `n` (`0x6E`) | `CG_MapRestart` resets `cg_thirdPerson` to `0` even when the client had it non-zero before the restart; the `DVAR_CHEAT` reset is recorded separately for both commercial profiles | Ref |
| R9 | initial usable server snapshot processed (`fs_debug` at `0`) | `CG_ProcessSnapshots` writes `fs_debug = 2` through the internal source while the snapshot is applied, and `CG_Init` settles it back to `0`; record the in-window value and the settled value separately and compare both to the reference | Ref |

### 9.4 Both references

Every row must be recorded separately for original commercial 1.7 and Steam
commercial 1.8; differences become explicit profiles or a blocker, never an
inferred equivalence. CoD4x runs may be recorded as supplemental only.

## 10. Unavailable licensed reference inputs

The following are required for acceptance and are **not available** in this
checkout. Their acceptance is therefore unproven, not waived:

- Original commercial 1.7 and Steam commercial 1.8 executables and content
  (acquisition/provenance, Steam app/depot/manifest identifiers, SHA-256,
  displayed/file versions).
- Reference client and listen/dedicated server launch configurations and
  captured, sanitized transactions.
- Valid CD keys, account identifiers and authentication tickets (must stay out
  of the public repo and issue attachments).
- Protected runner infrastructure for licensed sessions.
- Steam depot manifests and any protocol/build identifier that would
  distinguish Steam 1.8 from CoD4x.

Missing reference evidence remains a blocker to compatibility certification.
No source inspection, fork-only run or synthetic fixture can substitute for a
licensed reference session. Per [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md),
licensed binaries and game data are not committed here.

## 11. Non-goals and guardrails

- No production behavior, dvar flag, allowlist, limit or default changes.
- No certification of retail compatibility from source inspection.
- No change to unrelated work, including non-draft PR #106's merge hold and
  operator-owned PR #119 (`ki-n1et`).
- The parent issue `ki-7ul6` / #128 stays open; native ABI acceptance stays on
  #129.
- Any protection that conflicts with valid retail behavior must be recorded as
  an unresolved tradeoff and block its own acceptance rather than silently
  relaxing the compatibility contract.

## 12. Citation index

All paths are relative to the repository root at
`a1ca543b28391f347d4127797d971fdc0bc5a199`.

| Topic | Citation |
|---|---|
| `DvarType` | `src/universal/q_shared.h:482-494` |
| `DvarFlags` | `src/universal/q_shared.h:496-519` |
| `DvarValue`, `dvar_s` | `src/universal/q_shared.h:521-554`, `:606-617` |
| `DvarSetSource` | `src/qcommon/qcommon.h:322-328` |
| Write gate | `src/universal/dvar.cpp:1319-1395` |
| Domain check | `src/universal/dvar.cpp:493-543` |
| Name validation | `src/universal/dvar.cpp:305-319` |
| Lookup | `src/universal/dvar.cpp:829-832` |
| Registration | `src/universal/dvar.cpp:1556-1632`, `:1695-1786`, `:2163-2185` |
| String setters | `src/universal/dvar.cpp:2555-2617` |
| By-name setters | `src/universal/dvar.cpp:2689-2714` |
| Server builtins | `src/game_mp/g_client_script_cmd_mp.cpp:2097-2210`, `:3547-3548` |
| Server send | `src/server/sv_game.cpp:944-956`; `src/server_mp/server_mp.h:23-24` |
| Map-restart command | `src/server_mp/sv_ccmds_mp.cpp:512-513`; `src/cgame_mp/cg_servercmds_mp.cpp:212`, `:249`, `:465-467`, `:613-615` |
| `cg_thirdPerson` registration | `src/cgame_mp/cg_main_mp.cpp:893` |
| `Dvar_SetBool` | `src/universal/dvar.cpp:2535-2538` |
| Initial-snapshot `fs_debug` write | `src/cgame_mp/cg_snapshot_mp.cpp:566`, `:590-598`, `:596-597`; `src/cgame_mp/cg_view_mp.cpp:1332`; `src/cgame_mp/cg_main_mp.cpp:920-925`, `:1757-1758` |
| `Dvar_SetInt` | `src/universal/dvar.cpp:2333`, `:2540-2542` |
| cgame dispatch | `src/cgame_mp/cg_servercmds_mp.cpp:391-395`, `:461`, `:663-671` |
| cgame dvar handler | `src/cgame_mp/cg_servercmds_mp.cpp:1359-1407` |
| cgame configstrings | `src/cgame_mp/cg_servercmds_mp.cpp:34-72`, `:853-982` |
| client systeminfo | `src/client_mp/cl_parse_mp.cpp:171-245` |
| cheat-state reset | `src/client_mp/cl_parse_mp.cpp:214-219`; `src/universal/dvar.cpp:2778-2791` |
| initial map name | `src/client_mp/cl_cgame_mp.cpp:750-753` |
| string normalize | `src/universal/q_shared.cpp:45-53` |
| string storage | `src/universal/dvar.cpp:1300-1312`; `src/universal/com_memory.cpp:339-344`; `src/script/scr_stringlist.cpp:935-940`, `:963-985` |
| pool cap / fatal | `src/universal/dvar.cpp:1568-1572` |
| shellshock indirection | `src/bgame/bg_misc.cpp:1985-2014`; `src/universal/dvar.cpp:2862-2924` |
| Argument access | `src/qcommon/cmd.cpp:103-112` |
| Assert policy | `src/universal/assertive.h:3-31`; `src/universal/assertive.cpp:643-691` |
