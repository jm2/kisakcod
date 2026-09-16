# A08 headless server staging inventory and bounded-stage plan

Inventory and staging document for [fork issue #130](https://github.com/jm2/kisakcod/issues/130)
(roadmap item A08, "First engine — real Win64/Linux amd64 headless MP
integration"). It records the honest current state of the first native
headless server against A08's acceptance criteria, and decomposes the remaining
work into bounded, independently schedulable stages so the parent can advance
through stage beads instead of one opaque block.

**Basis tree:** `2babfed8adb17a5b3c80db288890992ec51ec49c` (`origin/master`,
post-#106). File and line references are exact at this commit; treat later
drift the way `docs/CODEBASE_AUDIT.md` does — the tree wins.

**Governance:** governed by [#122](https://github.com/jm2/kisakcod/issues/122)
and [NETWORK_COMPATIBILITY.md](NETWORK_COMPATIBILITY.md). This document makes
**no** compatibility or delivery claim: both commercial references (original
1.7 and Steam 1.8) remain unpinned, no original-peer cell has passed, and the
five production targets remain **0/5**. It retires no native64 gate and closes
no acceptance criterion.

**Relation to A07:** [NATIVE_ASSET_CLOSURE_LEDGER.md](NATIVE_ASSET_CLOSURE_LEDGER.md)
inventories the per-asset-family closure and lists the headless blockers by
family (its §6). This document maps those blockers (and the composition gates)
onto A08's six acceptance criteria and proposes bounded stages with explicit
exit evidence. The A07 ledger remains the authority for per-family state; this
document does not restate it.

---

## 1. Current composition state (what actually exists today)

| Fact | State | Anchor |
|---|---|---|
| Buildable engine backend | **Win32 only** | `CMakeLists.txt:51-58` fails configuration for Linux/macOS engine targets |
| Buildable runtime width | **32-bit only** | `CMakeLists.txt:60-67` fails 64-bit engine configuration unless `KISAK_ALLOW_UNSUPPORTED_64BIT=ON` |
| Linux/macOS engine + headless source sets | **Explicitly empty** (`COMPLETE FALSE`) | `scripts/platform/linux/platform.cmake:9-14`; `scripts/platform/macos/platform.cmake:9-14` |
| Platform service source sets | **Complete and runtime-tested** (win32, posix, macOS Mach) | `scripts/platform/*/platform.cmake`; `tests/headless_profile_test.cmake:74-96` |
| Headless dedicated composition | Exists, but **Win32-only** | `scripts/dedi/dedi_sources.cmake`; `scripts/dedi/CMakeLists.txt:20-36` |
| Only linked headless artifact | `KisakCOD-dedi.exe` on **Windows x86** | `.github/workflows/ci.yml:373-407` (`windows-x86-headless`) |
| Headless source-profile guards | Enforced: required sources present, client/media sources and bink/mss rejected | `tests/headless_profile_test.cmake`; `kisakcod_assert_headless_dedi_sources` |
| Accidental client/media includes in headless profile | **21 allowlisted entries** (tracked debt, not desired state) | `tests/headless_include_debt.allow`; `tests/headless_include_debt_test.cmake` |
| POSIX UDP socket service | Implemented, runtime-tested, **zero production callers** | `docs/task.md:538-543`; `src/qcommon/sys_socket.h` |
| Terminal crash-freeze primitives | Source/linkage-complete; non-returning paths are **not** hosted-runtime tested | `docs/task.md:530-537` |
| Production target delivery | **0/5** | `docs/task.md:16-22`, `:594-620` |

## 2. Native64 gates that must stay armed

A08 acceptance criterion 2 requires retaining the native64 gates "until safe".
At this basis they are:

- **Engine configuration gate** — `CMakeLists.txt:51-58` (non-win32) and
  `:60-67` (64-bit engine). No engine TU may be treated as width-buildable
  while these are bypassed.
- **Pointer-truncation tripwire** — `tests/pointer_truncation_test.cmake`,
  allowlist `tests/pointer_truncation.allow` (**24** sites at this basis).
- **ABI sizeof-debt tripwire** — `tests/abi_sizeof_debt_test.cmake`,
  allowlists `tests/abi_sizeof_debt.allow` (**183** entries) and
  `tests/abi_sizeof_formula_debt.allow` (**7** entries).
- **Headless media-exclusion assertions** —
  `tests/headless_profile_test.cmake` and
  `tests/headless_include_debt_test.cmake`.

These are fail-closed inventories, not completion evidence. Burning an entry
down is progress; deleting a gate is not.

## 3. A08 acceptance criteria → evidence state → blocker

| # | Acceptance criterion (issue #130) | Current evidence | Remaining blocker / owner |
|---|---|---|---|
| 1 | Honest staged Win64 reference / Linux amd64 headless composition: compile/link, synthetic load/script/network integration, then licensed retail map startup | Win32 x86 headless **links** and is smoke-tested; no POSIX engine link; no synthetic integration harness | POSIX engine source sets empty (linux/macos `platform.cmake`); engine gate armed; 64-bit widening incomplete (A07 ledger §6) |
| 2 | Complete server-required asset/runtime closure from A07; reject unsupported capabilities explicitly; retain native64 gates | Headless source profile asserts required vs. rejected *directories*; A07 ledger §6 orders per-family blockers | Closure not yet *enrolled*: XAnim consumer migration, runtime-ownership enrollment, physics/VM/model work (A07 §6.1-6.5) |
| 3 | Original commercial 1.7 + Steam 1.8 clients join/play, commands, snapshots, map cycles, reconnect, clean shutdown | **None.** No original reference pinned | Licensed runner + pinned references (#122, A11/#133, A12/#134). Explicitly blocked |
| 4 | Resource stability and real terminal-error behavior through a controlled child engine process | Platform process/crash primitives and POSIX service contracts pass; non-returning freeze paths are source/linkage-only | Needs a real **engine** child process (acceptance criterion 1); `docs/task.md:544-548` |
| 5 | Verify target architecture and absence of accidental graphics/audio/vendor client dependencies in the package | Headless source composition excludes client/media dirs; media include debt tracked (21 entries); Windows x86 artifact retained | No POSIX artifact to inspect; include debt not yet burned down; no architecture check on a produced package |
| 6 | Unavailable licensed infrastructure = blocked retail stage; continue honest synthetic integration without claiming delivery | Stated policy; synthetic utility tests proceed | Enforcement is process-level; no synthetic *production* integration harness yet |

## 4. Proposed bounded stages

Each stage is independently schedulable, must land through its own bead/PR with
final-head CI plus substantive review, and must not retire a gate. Stages are
ordered by dependency, not by calendar.

### A08-S1 — XAnim payload consumer migration (unowned; server-reachable)

A07 §6.3 calls out XAnim as a headless-MP-required family whose production
consumers still assume the retail 88-byte payload, while the widened runtime
view is `0x88` on 64-bit. The disk/native split infrastructure is staged but
**not adopted by the engine**:

- `src/xanim/xanim.cpp:152` — `XAnimClone` allocates the literal 88 bytes.
- `src/xanim/xanim_load_obj.cpp:998` — asserts `sizeof(XAnimParts) == 88` on the
  runtime type.
- `src/xanim/xanim_native.h` — records "the remaining migration of every
  XAnimParts consumer to XAnimPartsNative is a follow-up" and forbids engine TUs
  from including the portable-test-only header.

Exit evidence: engine consumers allocate/index by the native view (or an
equivalent width-correct form) while the 88-byte disk mirror is preserved; a
production-path loader allocates and consumes a widened XAnimParts under
sanitizers; serialized bytes unchanged. No native64 gate retired.

### A08-S2 — Runtime-ownership enrollment beyond the seven sites

A07 §6.1 / ledger §5: the durable table/facade/coordinator foundations are
build-enrolled with **zero callers**. Enroll staged load/commit/unload routing
per asset family. Exit evidence: named production caller per family, with
unload/error paths.

### A08-S3 — MP physics ownership conversion (#99)

Owned by `ki-v4m`. Pose, BreakablePiece, DynEntity ownership must land before
DynEnt-bearing maps load native64 (A07 §6.2). Do not duplicate; track here only
as a dependency of the first native64 map load.

### A08-S4 — Model nested-cursor production correction (#124 / #140)

Owned by `ki-okmr`/#140. Nested cursor ownership + checked rewind, cold/warm
loader tests (A07 §6.5). Dependency of criterion 1.

### A08-S5 — Production fuzz coverage (#125 / A03)

Owned by A03. Production DB/XModel/FX harnesses rather than the selected-read
primitive model (A07 §6.7). Supporting evidence, not a milestone closer.

### A08-S6 — POSIX headless engine composition (the first real link)

Populate and link the Linux amd64 headless production source set:
`PLATFORM_LINUX_DEDI_HEADLESS` in `scripts/platform/linux/platform.cmake` plus
the POSIX engine platform sources, while the top-level engine gate remains armed
for unsupported 64-bit states. Consumes the A07 closure (S1-S5) and the merged
`ki-vuj` console/POSIX preparation and `ki-eudd` socket service. Exit evidence:
configured → compiled → linked headless executable on Linux amd64; CI job
retaining the artifact; no client/media/vendor dependency in the link.

### A08-S7 — Synthetic load/script/network integration, then terminal error

With a linked headless binary, add: synthetic map/asset load through the real
loader; script/VM bring-up; loopback network session. Then the acceptance-4
child-process terminal-error test through the real engine route (not a
self-suspending service fixture), plus resource-stability soak. Exit evidence:
repeatable CI run with exact SHA and logs.

### A08-S8 — Package composition verification (acceptance 5)

Verify target architecture and the absence of accidental graphics/audio/vendor
client dependencies on the produced POSIX package; extend the headless
include-debt gate to burn down the 21 current entries where behavior-preserving.

### Blocked retail stage

Criterion 3 (original 1.7 / Steam 1.8 sessions, both directions) is **blocked**
on pinned references and protected runners (#122, A11/#133, A12/#134). It stays
explicitly pending; synthetic integration (S7) may proceed without it, but
cannot substitute for it.

## 5. Explicit non-claims

- No compatibility claim: both commercial references are unpinned; no cell has
  passed; fork-only sessions and CoD4x are not substitutes.
- No native64 retirement: the engine gate, pointer-truncation tripwire, sizeof
  debt ledger, and headless media-exclusion assertions stay armed.
- No enrollment inflation: build-enrolled, zero-caller foundations are not
  delivered capabilities.
- No milestone completion from the existing Windows x86 headless artifact or
  from utility-test counts.

## 6. Progress tracking

The parent `ki-b01r` / #130 stays open under its full-issue acceptance policy;
bounded stages (S1-S8) are tracked as separate beads that reference this
document and the A07 ledger, so partial progress is visible without closing the
parent. A stage PR that lands does **not** close #130.
