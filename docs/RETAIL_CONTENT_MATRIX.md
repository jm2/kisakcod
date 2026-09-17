# Retail-content and MP mod compatibility regression matrix

Ledger for [fork issue #133](https://github.com/jm2/kisakcod/issues/133) (roadmap
item A11). It defines the named content/configuration regression matrix the
commercial compatibility contract requires: the reference manifest fields, the
target/profile/configuration axes, a reproducible catalog of stock-map,
fast-file, raw-file, download/pure and demo cases, the upstream
[#89](https://github.com/SwagSoftware/KisakCOD/issues/89) and
[#40](https://github.com/SwagSoftware/KisakCOD/issues/40) reproductions with an
explicit disposition, and the lifecycle outcomes each case must record.

**Basis tree:** `2babfed8adb17a5b3c80db288890992ec51ec49c` (`origin/master`).
File and line references are exact at this commit; treat later drift the way
`docs/CODEBASE_AUDIT.md` does — the tree wins.

**Governance:** this ledger records required cases and current evidence state
only. It is governed by [#122](https://github.com/jm2/kisakcod/issues/122) and
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). It makes **no**
compatibility claim: both commercial references (original 1.7 and Steam 1.8)
remain unpinned, no commercial interop cell has passed, and missing
reference/runner evidence stays a blocker to certification. Generic asset safety
and an `sv_pure` checkbox do not satisfy these named cases, and this ledger does
not claim every arbitrary third-party mod is supported.

**Machine-checked contract:** the `retail-content-matrix:v1` index block in
§11 is the canonical axis/case/direction/disposition index. It is guarded by
`tests/retail_content_matrix_source_test.cmake`, so the required targets and
their production/reference roles, case families, individual named case ids with
their intended families, the required commercial session directions, the
case×direction child records the outcome schema keys on, the §6.3 aggregate
direction scope and per-profile status/evidence cells, the aggregate
completeness policy, the §4 catalog/index membership, the uniqueness of
disposition ids and the upstream dispositions cannot be silently dropped or
promoted. The upstream dispositions must stay **blocked** while the licensed
references are unavailable, each disposition id is a unique key, and every
commercial §6.3 cell must stay **Blocked / none** (status and evidence-ref
both checked) while the reference manifests are unavailable. The outcome
matrix (§6) records results separately and stays **Blocked** for every
commercial cell until a reference manifest id and case evidence exist; the
guard test does not certify a cell — it only refuses to let the contract shrink,
an aggregate commercial cell be promoted, or an unavailable-evidence
disposition be promoted unnoticed.

---

## 1. Status vocabulary

Each axis entry and each case uses these labels; they are deliberately narrower
than "done".

| Label | Meaning |
|---|---|
| **Defined** | Case input/fixture and expected invariant are specified and reproducible; not yet executed. |
| **Pending** | Required and runnable in principle, but no result is recorded. |
| **Blocked** | Requires an unavailable licensed reference, protected runner or external service. A blocker to certification, never a pass or silent skip. |
| **Pass** | A recorded result on the exact target/profile/configuration with evidence attached (hashes, command, log, reference manifest id). |
| **Fail** | A recorded mismatch with evidence; becomes an explicit profile or a blocker, never normalized away. |
| **N/A** | Evidence-backed non-applicability for one reference (e.g. a server that demonstrably does not emit a field), never a convenience skip. |
| **Supplemental** | Fork-only or synthetic evidence. Useful, but it cannot fill a commercial-reference cell. |

At this basis every commercial cell is **Blocked** and every synthetic case is
**Defined** with no recorded outcome. No row is Pass.

## 2. Principal axes

### 2.1 Targets

The contract covers the five requested production targets plus the Windows x86
baseline kept as an additional regression/reference platform
(`docs/task.md` §"Requested target delivery",
[NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md)).

| Target id | Platform | Role |
|---|---|---|
| `win-amd64` | Windows amd64 | Production client + dedicated server |
| `win-arm64` | Windows ARM64 | Production engine |
| `linux-amd64` | Linux amd64 | Production headless server + full client |
| `linux-arm64` | Linux ARM64 (real hardware) | Production engine |
| `macos-arm64` | macOS ARM64 | Production client (MoltenVK) + headless server |
| `win-x86` | Windows x86 | **Reference/regression only**; not the sole oracle |

### 2.2 Commercial profiles

| Profile id | Meaning |
|---|---|
| `original-commercial-1.7` | Unmodified original commercial CoD4 1.7 client and dedicated/listen server. |
| `steam-commercial-1.8` | Unmodified Steam-distributed commercial 1.8 client and dedicated/listen server. Community CoD4x 1.8 is a **different** reference. |
| `kisakcod-self` | Supplemental fork-only runs. Cannot substitute for either commercial profile. |

### 2.3 Configuration modes

`listen` (in-client host) and `dedicated` (separate server process). Each
commercial profile is recorded per applicable mode, and no target/profile may be
marked passing while a required mode is unrecorded
([NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md), "Real bidirectional
sessions"). Case→mode applicability is declared per case in the §11 `case-mode`
lines, so a listen-only case such as `SM-01` is never required in a dedicated
cell and a dedicated-only case such as `SM-02` is never required in a listen
cell; a case contributes a child only in the modes declared for it.

### 2.4 Session directions

Every content case records the applicable directions separately:

- `KC-server ↔ commercial-client` (each commercial client against a KisakCOD server target).
- `KC-client ↔ commercial-server` (each KisakCOD client target against the applicable commercial server profile).
- `KC ↔ KC` — supplemental only.

Direction applicability follows the target role (§2.1): the
`kc-server-commercial-client` direction requires a server-capable target and the
`kc-client-commercial-server` direction requires a client-capable target. Every
requested production target carries both roles — `macos-arm64` delivers the
MoltenVK client **and** the headless server (`docs/PORTING.md`, "Required
original-binary protocol and handshake integration"; phase 4) — so each target
produces both direction children. The §11 `target-role` lines encode each
target's capabilities and the §6.3 `Required directions` column is derived from
them, so an inapplicable direction is never required; a target that carried only
the client role would not be required to produce a server-direction child, but no
requested target is client-only.

## 3. Reference manifest requirements (both profiles)

`NETWORK_COMPATIBILITY.md` defines the required reference manifest. Both
entries are **Blocked** in this checkout; the fields below are the evidence a
case must cite by reference id.

| Field | Required for | Recorded |
|---|---|---|
| Acquisition/provenance (source, app/depot/manifest or patch id) | Both | No — pending |
| Executable SHA-256 (client and server) | Both | No — pending |
| Displayed/file version and build | Both | No — pending |
| Client and dedicated/listen launch modes | Both | No — pending |
| Data / language / patch hashes | Both | No — pending |
| Observed discovery/challenge/auth behavior (sanitized) | Both | No — pending |
| Test configuration and target hardware | Both | No — pending |

Licensed binaries, game data, CD keys, account identifiers and authentication
tickets are **not** committed to this repository or issue attachments; only
sanitized fixture and evidence metadata may be recorded.

## 4. Content, mode and reproduction case catalog

Each case is a named, reproducible input with an expected invariant and a
required-evidence column. Case ids are stable and are the keys the outcome
matrix (§6) records. "Targets" lists the axis ids from §2; a case must be
recorded for every applicable target and **both** commercial profiles before it
can pass.

### 4.1 Stock maps (local content load, join, map change, unload)

| ID | Input / fixture | Expected invariant | Required evidence | Targets |
|---|---|---|---|---|
| `SM-01` | Stock MP map, local listen-server load | Map zone loads, client joins, spawns, gameplay starts | Reference id + sanitized load/join log | all production + `win-x86` |
| `SM-02` | Stock MP map, dedicated server | Headless load and client join over the same assets | Reference id + server/client log | all production + `win-x86` |
| `SM-03` | `map`/`map_rotate` between two stock maps | Content unload/reload and reconnect match the reference | Reference id + before/after zone + reconnect log | all production + `win-x86` |

### 4.2 Fast-file and raw-file mod cases

| ID | Input / fixture | Expected invariant | Required evidence | Targets |
|---|---|---|---|---|
| `MOD-01` | Full `.ff` fast-file mod zone (non-`iwd` content) | Mod zone loads and gameplay runs on listen and dedicated without crash and without rebaking | Mod content hash + reference id + log | all production + `win-x86` |
| `MOD-02` | Mod using `fs_game` base directory with mixed `.ff`/`iwd` overlays | File-system base and override ordering match the reference; no fork-only path is required | `fs_game` value + file manifest + reference id | all production + `win-x86` |
| `MOD-03` | Raw-file override mod (`raw/` and config/script overrides) | Raw overrides are read through the production file path with unchanged semantics | File list + hashes + reference id | all production + `win-x86` |

`fs_game` handling is production code at `src/server_mp/sv_client_mp.cpp:74`
and `src/server_mp/sv_main_mp.cpp:564`; raw reads go through
`FS_FOpenFileRead`/`FS_ReadFile` (`src/universal/com_files.cpp:1152`,
`:1334`). These cases must not be satisfied by native64 rebaking or a fork-only
golden baseline (acceptance criterion in #133).

### 4.3 Downloads and pure checks

| ID | Input / fixture | Expected invariant | Required evidence | Targets |
|---|---|---|---|---|
| `PC-01` | `sv_pure 0`, unmodified stock client | Join succeeds; no pure rejection | Reference id + server/client log | all production + `win-x86` |
| `PC-02` | `sv_pure 1`, unmodified stock content | Content is accepted; no valid retail file is rejected | Reference id + pure manifest + log | all production + `win-x86` |
| `PC-03` | `sv_pure 1`, modified content | Modified content is rejected for the same reason and at the same stage as the reference | Reference id + rejection log + hashes | all production + `win-x86` |
| `PC-04` | Server download path (`sv_allowDownload`/`sv_wwwDownload`, client `cl_allowDownload`/`cl_wwwDownload`) | Download/pure behavior matches the reference; no valid retail behavior is broadened or rejected | Reference id + transfer log | all production + `win-x86` |

Pure/download dvars are declared at `src/server_mp/server_mp.h:941`, `:952`,
`:954` and `src/client_mp/client_mp.h:609`, `:610`; server download entry points
at `src/server_mp/server_mp.h:1244`.

### 4.4 Demos

| ID | Input / fixture | Expected invariant | Required evidence | Targets |
|---|---|---|---|---|
| `DEMO-01` | Record a stock MP session | Demo is recorded with the reference's format/provenance | Reference id + demo header dump | `win-x86` + production clients |
| `DEMO-02` | Play back a stock demo on the same profile | Playback fidelity matches the reference | Reference id + playback log | `win-x86` + production clients |
| `DEMO-03` | Play back a demo recorded by the other commercial profile | Cross-profile behavior is recorded explicitly, not assumed interchangeable | Both reference ids + playback log | `win-x86` + production clients |

Client demo commands are registered in `src/client_mp/cl_main_mp.cpp:2789`
(`CL_Record_f`) and `:2972` (`CL_PlayDemo_f`).

### 4.5 Upstream reproductions (#89 and #40)

These reproduce the concrete regressions the issue names; each has an explicit
disposition and is **not** satisfied by generic asset safety.

| ID | Upstream | Input / fixture | Expected invariant | Disposition at this basis |
|---|---|---|---|---|
| `UP89-01` | [#89](https://github.com/SwagSoftware/KisakCOD/issues/89) | Named listen-server mods launched on any map (per the upstream report) | Mod launches and loads a stock map without crashing; failure captured with the crashing stage | **Blocked** — named mod content and licensed retail fixtures are not available in this checkout; no reproduction is claimed. |
| `UP89-02` | [#89](https://github.com/SwagSoftware/KisakCOD/issues/89) | Per-map isolation of the #89 crash after the launch fix | Identifies whether the crash is content, load-stage or join-stage, with the exact map/asset | **Blocked** — same unavailable fixtures; depends on `UP89-01`. |
| `UP40-01` | [#40](https://github.com/SwagSoftware/KisakCOD/issues/40) | Fixed-tick command-driven movement comparison against a pinned commercial baseline | Network-visible movement/physics match the commercial baseline; playback of recorded positions alone is not sufficient | **Blocked** — needs a pinned commercial reference; ties to A05/#127 and `ki-jgz`/`#116` scalar-determinism evidence, which is supplemental. |

Movement code paths under audit: user-command angle handling
(`src/bgame/bg_pmove.cpp:608`), slide/step movement
(`src/bgame/bg_slidemove.cpp`). Existing determinism coverage
(`tests/runtime_scalar_determinism_tests.cpp`,
`tests/runtime_scalar_determinism_source_test.cmake`, merged `ki-jgz`/#116) is
recorded as **Supplemental**, not as the commercial baseline.

## 5. Lifecycle outcome coverage

The issue requires tracking content load, client join, gameplay, map change,
unload and reconnect per target and commercial profile. Each case row records
each applicable outcome; the mapping below fixes which cases cover which stage
so no stage is left implicit.

| Lifecycle stage | Cases | Production entry point (basis tree) |
|---|---|---|
| Content load | `SM-01`, `SM-02`, `MOD-01`–`MOD-03` | `DB_LoadXAssets`/`DB_LoadXZone` (`src/database/db_registry.cpp:2463`, `:2576`) |
| Client join / auth | `SM-01`, `SM-02`, `PC-01`–`PC-04` | `SV_DirectConnect` (`src/server_mp/sv_client_mp.cpp:621`), `SV_GetChallenge` (`:130`), `CL_CheckForResend` (`src/client_mp/cl_main_mp.cpp:1042`) |
| Gameplay | `SM-01`, `SM-02`, `MOD-01`–`MOD-03`, `UP40-01` | Server/client game loop and pmove (`src/bgame/bg_pmove.cpp:608`) |
| Map change | `SM-03` | `SV_Map_f` (`src/server_mp/sv_ccmds_mp.cpp:371`), `SV_MapRestart` (`:456`), `SV_SpawnServer` (`src/server_mp/sv_init_mp.cpp:390`) |
| Unload | `SM-03`, `MOD-01`–`MOD-03` | `DB_RemoveXAsset` (`src/database/db_registry.cpp:3343`), `DB_FreeXZoneMemory` (`:3387`) |
| Reconnect | `SM-03`, `PC-04` | `CL_Disconnect` (`src/client_mp/cl_main_mp.cpp:470`) then `CL_CheckForResend` (`:1042`) |
| Demo record/playback | `DEMO-01`–`DEMO-03` | `CL_Record_f` (`src/client_mp/cl_main_mp.cpp:2789`), `CL_PlayDemo_f` (`:2972`) |

## 6. Outcome records (case × direction children)

The issue requires tracking content load, client join, gameplay, map change,
unload and reconnect **by target and commercial profile**. §2.4 additionally
requires every session direction to be recorded separately, and §4 declares the
case ids to be the outcome keys. The outcome schema therefore keys evidence by
case and direction, not by an aggregate cell alone.

### 6.1 Record schema

- **Child record** — the unit of evidence:
  `(target, mode, profile, case, direction) → status / evidence-ref`, where
  `direction` is one of the §2.4 ids and `status` is the §1 vocabulary. Each
  child record carries the applicable §5 lifecycle stages and the §4 required
  evidence for its case, and is Pass only from a real run citing the §3
  reference manifest id and that evidence.
- A missing licensed reference yields **Blocked**, never an implicit Pass.
  `kc-kc` children are **Supplemental** and never satisfy a commercial cell.
- **Aggregate cell** — the §6.3 roll-up `(target, mode, profile)`. It is Pass
  only when every required child record for that cell is Pass. The **weakest
  required child** bounds the cell: any Blocked/Fail/Defined/Pending child
  forces the aggregate to Blocked/Fail/Defined/Pending. An aggregate may never
  be promoted while a required case or direction child is unrecorded.

Required children for a commercial cell are only the **applicable**
combinations: a §4 case contributes a child only in the modes declared for it
(§11 `case-mode`), and a target contributes a child only in the directions its
role supports (§11 `target-role`). `kc-server-commercial-client` children
require a server-capable target and `kc-client-commercial-server` children
require a client-capable target; every requested production target, including
`macos-arm64`, carries both roles, so each produces both direction children,
while `SM-01` (listen) / `SM-02` (dedicated) produce no child for the
non-matching mode. The §11 `outcome` lines
enumerate exactly this applicable case×mode×direction set, and the §11
`completeness aggregate pass-requires-all-case-directions` line fixes the
roll-up policy, so an applicable child cannot be omitted and an inapplicable
mode/role combination cannot be required. An applicability change must move the
`case-mode`/`target-role` declarations, the `outcome` triples and the §6.3
`Required directions` column together.

### 6.2 Required child-record ledger

Each required named case declares the modes it applies to (`Modes`); its two
commercial direction cells are required only for those modes and only for
targets whose role supports the direction (§6.1). The full child set is
therefore the cross-product of this table with the applicable
(target, mode, profile) axes, not an unconditional cross-product. `Status` is
the current §1 label (`Blocked / none` means no evidence is recorded); a Pass
must attach the §4 required evidence named in the last column. The guard
validates these cells directly: while the licensed reference manifests are
unavailable, every commercial direction cell in this ledger must stay
`Blocked / none`, the ledger must cover exactly the §11 case set, and neither
the status nor the evidence half may be promoted — a fabricated child claim
cannot bypass the aggregate roll-up by hiding in a single cell.

| Case | Modes | kc-server-commercial-client | kc-client-commercial-server | Applicable §5 lifecycle stages | Required evidence (§4) |
|---|---|---|---|---|---|
| `SM-01` | listen | Blocked / none | Blocked / none | content load, client join, gameplay | Reference id + sanitized load/join log |
| `SM-02` | dedicated | Blocked / none | Blocked / none | content load, client join, gameplay | Reference id + server/client log |
| `SM-03` | listen, dedicated | Blocked / none | Blocked / none | map change, unload, reconnect | Reference id + before/after zone + reconnect log |
| `MOD-01` | listen, dedicated | Blocked / none | Blocked / none | content load, gameplay, unload | Mod content hash + reference id + log |
| `MOD-02` | listen, dedicated | Blocked / none | Blocked / none | content load, gameplay, unload | `fs_game` value + file manifest + reference id |
| `MOD-03` | listen, dedicated | Blocked / none | Blocked / none | content load, gameplay, unload | File list + hashes + reference id |
| `PC-01` | listen, dedicated | Blocked / none | Blocked / none | client join | Reference id + server/client log |
| `PC-02` | listen, dedicated | Blocked / none | Blocked / none | client join | Reference id + pure manifest + log |
| `PC-03` | listen, dedicated | Blocked / none | Blocked / none | client join | Reference id + rejection log + hashes |
| `PC-04` | listen, dedicated | Blocked / none | Blocked / none | client join, reconnect | Reference id + transfer log |
| `DEMO-01` | listen, dedicated | Blocked / none | Blocked / none | demo record/playback | Reference id + demo header dump |
| `DEMO-02` | listen, dedicated | Blocked / none | Blocked / none | demo record/playback | Reference id + playback log |
| `DEMO-03` | listen, dedicated | Blocked / none | Blocked / none | demo record/playback | Both reference ids + playback log |
| `UP89-01` | listen | Blocked / none | Blocked / none | content load, client join | Reference id + crashing-stage log |
| `UP89-02` | listen | Blocked / none | Blocked / none | content load, client join | Reference id + map/asset isolation |
| `UP40-01` | listen, dedicated | Blocked / none | Blocked / none | gameplay (movement/physics) | Reference id + movement comparison |

### 6.3 Aggregate roll-up

The aggregate cell is derived, never standalone: it can only be as strong as
its weakest required child. At this basis every commercial child is **Blocked**
(no reference manifest) and every `kisakcod-self` child is **Supplemental** with
no claim of parity, so the roll-up below stays Blocked/Supplemental. The matrix
is rendered per target; each cell is `status` / evidence-ref. The
`Required directions` column is derived from the §11 `target-role` lines: a
dual-role target such as `macos-arm64` requires both the
`kc-client-commercial-server` and `kc-server-commercial-client` directions; a
hypothetical client-only target would require only
`kc-client-commercial-server` and is never required to produce a
`kc-server-commercial-client` result, but no requested target is client-only.
Per-case mode filtering (§11 `case-mode`) applies within each cell.

| Target | Mode | Required directions | original-commercial-1.7 | steam-commercial-1.8 | kisakcod-self |
|---|---|---|---|---|---|
| `win-amd64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `win-amd64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |
| `win-arm64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `win-arm64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |
| `linux-amd64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `linux-amd64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |
| `linux-arm64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `linux-arm64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |
| `macos-arm64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `macos-arm64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |
| `win-x86` (reference) | listen | both | Blocked / none | Blocked / none | Supplemental / none |
| `win-x86` (reference) | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |

No cell may be promoted to **Pass** while any applicable required case/direction
child is unrecorded or non-Pass (§6.1), and none may pass without the reference
manifest id and the case evidence from §4. A cell that cannot be executed
because a required profile/mode is unavailable stays **Blocked**, never
"skipped"; a mode/role combination that cannot apply is not required at all.

## 7. Existing implementation and test inventory

This is what can be reused; it is not a compatibility result.

| Area | State | Evidence |
|---|---|---|
| Zone/fast-file load+unload | Decompiled production readers with Disk32 envelopes; no content-regression driver | `src/database/db_registry.cpp:2463`, `:2576`, `:3343`, `:3387`; `docs/NATIVE_ASSET_CLOSURE_LEDGER.md` |
| Pure/download | Production dvar paths and server download entry points exist; no named regression cases | `src/server_mp/server_mp.h:941`, `:952`, `:954`, `:1244`; `src/client_mp/client_mp.h:609`, `:610` |
| Demo record/playback | MP and SP demo code exists; no cross-profile demo result | `src/client_mp/cl_main_mp.cpp:2789`, `:2972`; `src/client/cl_demo.cpp:427`, `:593` |
| Movement/physics determinism | Scalar-determinism utility contracts (`ki-jgz`/#116) | `tests/runtime_scalar_determinism_tests.cpp` |
| Fast-file parity instrument | M5 capture/digest protocol (`ki-msb`/#113); **no real retail asset walk** | `src/database/db_graph_hash.*`, `scripts/ci/run-retail-fastfile-parity.sh` |
| Named mod/#89/#40 reproductions | None | — |

## 8. Unavailable licensed reference inputs

Required for acceptance and **not available** in this checkout; their acceptance
is unproven, not waived:

- Original commercial 1.7 and Steam commercial 1.8 executables, content and
  localization, with provenance and hashes (both reference manifests).
- The named listen-server mods and stock maps referenced by upstream #89.
- A pinned commercial movement/physics baseline for upstream #40.
- Protected runner infrastructure for licensed sessions, and any valid CD
  keys/account identifiers/tickets (which stay out of the repository).

No source inspection, fork-only run or synthetic fixture can substitute for a
licensed reference session.

## 9. Non-goals and guardrails

- No production behavior, content, dvar, pure or download change.
- No certification of retail compatibility from this inventory.
- No native64 rebaking or fork-only golden baseline used to replace retail
  archives/checksums or network-visible movement/physics.
- Original peers stay unmodified: no replacement executable, injected DLL,
  CoD4x installation, forced upgrade/downgrade or new KisakCOD-only auth
  convention; authentication is not bypassed.
- Native SP/savegame product support stays explicitly deferred while existing
  x86 SP regression evidence is protected.
- Keep the merge holds and operator-owned PRs intact; this ledger changes no
  branch or worktree other than its own.

## 10. Owner cross-reference

| Row | Owner | State at basis |
|---|---|---|
| Commercial compatibility parent | `ki-oq93` / [#122](https://github.com/jm2/kisakcod/issues/122) | Open; references blocked |
| Native ABI/asset closure | `ki-mtmw` / [#129](https://github.com/jm2/kisakcod/issues/129) | Ledger merged |
| Scalar determinism | `ki-jgz` / [#116](https://github.com/jm2/kisakcod/pull/116) | Merged (supplemental) |
| Fast-file parity instrument | `ki-msb` / [#113](https://github.com/jm2/kisakcod/pull/113) | Open |
| This matrix | `ki-ubw0` / [#133](https://github.com/jm2/kisakcod/issues/133) | This document + guard test |

## 11. Machine-readable matrix index (v1)

The block below is the canonical axis/case/applicability/direction/disposition
index validated by `tests/retail_content_matrix_source_test.cmake`. Edit §2–§6
and this block together. New cases need a `case <id> <family>` line plus a
`case-mode <id> <mode...>` applicability line and one
`outcome <id> <mode> <direction>` child record per applicable
mode×required direction; new families must be covered by §4; new directions need
a `direction <id> <kind>` line and a `target-role` capability on every target
that can carry it; new targets need a `target-role <id> <role...>` line, a §6.3
aggregate row whose `Required directions` column matches that role, and the
applicable `outcome` triples. Promotion to `Pass` requires the reference-manifest
id plus case evidence recorded in §6.

`case-mode` encodes case→mode applicability, so a listen-only case (`SM-01`) is
never required in a dedicated cell and a dedicated-only case (`SM-02`) is never
required in a listen cell. `target-role` encodes target capability:
`kc-server-commercial-client` outcomes require a server-capable target and
`kc-client-commercial-server` outcomes require a client-capable target.
`macos-arm64` carries both roles — the MoltenVK client and the headless
server — so it is required to produce both direction children and must not be
demoted to client-only; only a target that genuinely lacked the server role
would be excused from a server-direction child. The `outcome` lines therefore
enumerate exactly the applicable case×mode×direction children, and the
`completeness aggregate pass-requires-all-case-directions` line must stay,
because it forces an aggregate cell to depend on all applicable
case×mode×direction children. Each `disposition` id must appear exactly once and
both `disposition` entries must stay **blocked** while their licensed references
are unavailable; every commercial §6.2 child direction cell and every
commercial §6.3 result/evidence cell must stay `Blocked / none` until a real
run cites a reference manifest id, and the §6.2 ledger must cover exactly the
case set declared above.

<!-- retail-content-matrix:v1
target win-amd64 production
target win-arm64 production
target linux-amd64 production
target linux-arm64 production
target macos-arm64 production
target win-x86 reference
target-role win-amd64 client server
target-role win-arm64 client server
target-role linux-amd64 client server
target-role linux-arm64 client server
target-role macos-arm64 client server
target-role win-x86 client server
profile original-commercial-1.7 commercial
profile steam-commercial-1.8 commercial
profile kisakcod-self supplemental
mode listen
mode dedicated
direction kc-server-commercial-client commercial
direction kc-client-commercial-server commercial
direction kc-kc supplemental
case SM-01 stock-map
case SM-02 stock-map
case SM-03 stock-map
case MOD-01 fastfile-mod
case MOD-02 fastfile-mod
case MOD-03 raw-mod
case PC-01 download-pure
case PC-02 download-pure
case PC-03 download-pure
case PC-04 download-pure
case DEMO-01 demo
case DEMO-02 demo
case DEMO-03 demo
case UP89-01 upstream-89
case UP89-02 upstream-89
case UP40-01 upstream-40
case-mode SM-01 listen
case-mode SM-02 dedicated
case-mode SM-03 listen dedicated
case-mode MOD-01 listen dedicated
case-mode MOD-02 listen dedicated
case-mode MOD-03 listen dedicated
case-mode PC-01 listen dedicated
case-mode PC-02 listen dedicated
case-mode PC-03 listen dedicated
case-mode PC-04 listen dedicated
case-mode DEMO-01 listen dedicated
case-mode DEMO-02 listen dedicated
case-mode DEMO-03 listen dedicated
case-mode UP89-01 listen
case-mode UP89-02 listen
case-mode UP40-01 listen dedicated
outcome SM-01 listen kc-server-commercial-client
outcome SM-01 listen kc-client-commercial-server
outcome SM-02 dedicated kc-server-commercial-client
outcome SM-02 dedicated kc-client-commercial-server
outcome SM-03 listen kc-server-commercial-client
outcome SM-03 listen kc-client-commercial-server
outcome SM-03 dedicated kc-server-commercial-client
outcome SM-03 dedicated kc-client-commercial-server
outcome MOD-01 listen kc-server-commercial-client
outcome MOD-01 listen kc-client-commercial-server
outcome MOD-01 dedicated kc-server-commercial-client
outcome MOD-01 dedicated kc-client-commercial-server
outcome MOD-02 listen kc-server-commercial-client
outcome MOD-02 listen kc-client-commercial-server
outcome MOD-02 dedicated kc-server-commercial-client
outcome MOD-02 dedicated kc-client-commercial-server
outcome MOD-03 listen kc-server-commercial-client
outcome MOD-03 listen kc-client-commercial-server
outcome MOD-03 dedicated kc-server-commercial-client
outcome MOD-03 dedicated kc-client-commercial-server
outcome PC-01 listen kc-server-commercial-client
outcome PC-01 listen kc-client-commercial-server
outcome PC-01 dedicated kc-server-commercial-client
outcome PC-01 dedicated kc-client-commercial-server
outcome PC-02 listen kc-server-commercial-client
outcome PC-02 listen kc-client-commercial-server
outcome PC-02 dedicated kc-server-commercial-client
outcome PC-02 dedicated kc-client-commercial-server
outcome PC-03 listen kc-server-commercial-client
outcome PC-03 listen kc-client-commercial-server
outcome PC-03 dedicated kc-server-commercial-client
outcome PC-03 dedicated kc-client-commercial-server
outcome PC-04 listen kc-server-commercial-client
outcome PC-04 listen kc-client-commercial-server
outcome PC-04 dedicated kc-server-commercial-client
outcome PC-04 dedicated kc-client-commercial-server
outcome DEMO-01 listen kc-server-commercial-client
outcome DEMO-01 listen kc-client-commercial-server
outcome DEMO-01 dedicated kc-server-commercial-client
outcome DEMO-01 dedicated kc-client-commercial-server
outcome DEMO-02 listen kc-server-commercial-client
outcome DEMO-02 listen kc-client-commercial-server
outcome DEMO-02 dedicated kc-server-commercial-client
outcome DEMO-02 dedicated kc-client-commercial-server
outcome DEMO-03 listen kc-server-commercial-client
outcome DEMO-03 listen kc-client-commercial-server
outcome DEMO-03 dedicated kc-server-commercial-client
outcome DEMO-03 dedicated kc-client-commercial-server
outcome UP89-01 listen kc-server-commercial-client
outcome UP89-01 listen kc-client-commercial-server
outcome UP89-02 listen kc-server-commercial-client
outcome UP89-02 listen kc-client-commercial-server
outcome UP40-01 listen kc-server-commercial-client
outcome UP40-01 listen kc-client-commercial-server
outcome UP40-01 dedicated kc-server-commercial-client
outcome UP40-01 dedicated kc-client-commercial-server
completeness aggregate pass-requires-all-case-directions
disposition upstream-89 blocked unavailable named-mod and licensed retail fixtures, no reproduction claimed
disposition upstream-40 blocked needs pinned commercial movement baseline, scalar-determinism evidence is supplemental
-->

## 12. Citation index

All paths are relative to the repository root at
`2babfed8adb17a5b3c80db288890992ec51ec49c`.

| Topic | Citation |
|---|---|
| Protocol admission | `src/server_mp/sv_client_mp.cpp:655`, `:659` |
| Protocol advertisement | `src/server_mp/sv_init_mp.cpp:700`; `src/client_mp/cl_main_mp.cpp:1121` |
| Challenge/identity | `src/server_mp/sv_client_mp.cpp:130`, `:621`; `src/client_mp/cl_main_mp.cpp:1042`, `:1097`, `:1108`, `:1112` |
| Map load/change | `src/server_mp/sv_ccmds_mp.cpp:371`, `:456`; `src/server_mp/sv_init_mp.cpp:390` |
| Zone load/unload | `src/database/db_registry.cpp:2463`, `:2576`, `:3343`, `:3387` |
| Pure/download dvars | `src/server_mp/server_mp.h:941`, `:952`, `:954`, `:1244`; `src/client_mp/client_mp.h:609`, `:610` |
| `fs_game` / raw files | `src/server_mp/sv_client_mp.cpp:74`; `src/server_mp/sv_main_mp.cpp:564`; `src/universal/com_files.cpp:1152`, `:1334` |
| Demo record/playback | `src/client_mp/cl_main_mp.cpp:2789`, `:2972`; `src/client/cl_demo.cpp:427`, `:593` |
| Movement/physics | `src/bgame/bg_pmove.cpp:608`; `src/bgame/bg_slidemove.cpp` |
| Determinism contracts | `tests/runtime_scalar_determinism_tests.cpp`, `tests/runtime_scalar_determinism_source_test.cmake` |
| Fast-file parity instrument | `src/database/db_graph_hash.cpp`, `src/database/db_graph_hash.h`, `scripts/ci/run-retail-fastfile-parity.sh` |
| Compatibility contract | `docs/NETWORK_COMPATIBILITY.md`; `docs/task.md` |
