# KisakCOD capability dashboard

<!-- GENERATED FILE - DO NOT EDIT. Regenerate with `python3 scripts/ci/capability-dashboard.py`. -->

Source manifest: `docs/capability/manifest.json`; manifest authoring base: `2babfed8adb17a5b3c80db288890992ec51ec49c`.
CI and test inventories are derived from `.github/workflows/*.yml`, `.github/workflows/*.yaml`, `tests/CMakeLists.txt` and `CMakePresets.json`, not maintained by hand.

## Requested target delivery: 0/5

A requested target counts as delivered only when every required mode (mp-client, headless-server) reaches `packaged_clean_machine` with a package result, the capability is production-enrolled, and both required commercial reference profiles (commercial-1.7, steam-1.8) are validated. Test, scaffold, instrument, CoD4x and fork-only evidence is excluded from this aggregate.

| Requested target | mp-client | headless-server | Delivered |
|---|---|---|---|
| windows-amd64 | pending | pending | no |
| windows-arm64 | pending | pending | no |
| linux-amd64 | pending | pending | no |
| linux-arm64 | pending | pending | no |
| macos-arm64 | pending | pending | no |

Commercial reference gate is **not satisfied**, so no requested target can be delivered even if a mode row reaches the required level.

## Commercial reference gate (#122)

| Reference | Origin | Required | Status | SHA-256 | Evidence run | Blocker |
|---|---|---|---|---|---|---|
| commercial-1.7 | original-commercial | yes | pending | pending | pending | Reference client/server binaries, content and observed protocol behavior are not provisioned in this repository. Missing reference evidence is a blocker to compatibility certification, never a passing result. |
| steam-1.8 | original-commercial | yes | pending | pending | pending | Protected licensed runner, Steam depot/content manifest and observed protocol behavior are not provisioned. Community CoD4x 1.8 is explicitly not this reference. |

## Capability rows

| Capability | Target | Mode | Owner | Implementation | Production enrolled | Strongest validation | Evidence | Blocker |
|---|---|---|---|---|---|---|---|---|
| windows-x86-mp-client | windows-x86 | mp-client | Preserved Windows x86 baseline (build-win.ps1; CI ci.yml/windows-x86) | implemented | yes | linked_production | sha=pending, run=pending, package=pending | Licensed content startup and original-peer sessions require the deferred protected runner (licensed-smoke.yml); no package smoke has run. |
| windows-x86-headless-dedi | windows-x86 | headless-server | Preserved Windows x86 baseline (CI ci.yml/windows-x86-headless) | implemented | yes | linked_production | sha=pending, run=pending, package=pending | Licensed content startup and original-peer sessions require the deferred protected runner; no package smoke has run. |
| windows-amd64-mp-client | windows-amd64 | mp-client | #130 (A08) on #107 (ki-yvj) scaffolding | not_started | no | none | sha=pending, run=pending, package=pending | No production engine composition; native client delivery follows the headless server slice and the commercial reference oracle. |
| windows-amd64-headless-server | windows-amd64 | headless-server | #130 (A08) | not_started | no | none | sha=pending, run=pending, package=pending | Asset/subobject/ABI closure (#129) and real production link (#130) are not complete. |
| windows-arm64-mp-client | windows-arm64 | mp-client | #130 (A08) on #107 (ki-yvj) scaffolding | not_started | no | none | sha=pending, run=pending, package=pending | Renderer/backend decision (#131) and production link are open; no engine build exists. |
| windows-arm64-headless-server | windows-arm64 | headless-server | #130 (A08) | not_started | no | none | sha=pending, run=pending, package=pending | Asset/subobject/ABI closure (#129) and real production link (#130) are not complete. |
| linux-amd64-mp-client | linux-amd64 | mp-client | #130 (A08) / #136 (A14) | not_started | no | none | sha=pending, run=pending, package=pending | No production engine composition; full client waits on validated graphics/content path, while the headless server slice can proceed. |
| linux-amd64-headless-server | linux-amd64 | headless-server | #130 (A08) | partial | no | none | sha=pending, run=pending, package=pending | Retail content closure and real production link (#130) are not complete; synthetic integration is the next checkpoint. |
| linux-arm64-mp-client | linux-arm64 | mp-client | #130 (A08) / #131 (A09) | not_started | no | none | sha=pending, run=pending, package=pending | No production engine composition; graphics/backend decision (#131) is open. |
| linux-arm64-headless-server | linux-arm64 | headless-server | #130 (A08) | not_started | no | none | sha=pending, run=pending, package=pending | Asset/subobject/ABI closure (#129) and real production link (#130) are not complete. |
| macos-arm64-mp-client | macos-arm64 | mp-client | #130 (A08) / #131 (A09) | not_started | no | none | sha=pending, run=pending, package=pending | Vulkan/MoltenVK feasibility (#131) is unresolved and no production engine build exists. |
| macos-arm64-headless-server | macos-arm64 | headless-server | #130 (A08) | not_started | no | none | sha=pending, run=pending, package=pending | Asset/subobject/ABI closure (#129) and real production link (#130) are not complete. |
| native-sp | all-requested | sp | Deferred by PORTING.md | deferred | no | none | sha=pending, run=pending, package=pending | Explicitly out of scope for the current product target; deferred, not failed. |

## Supporting evidence (never advances a production row)

| Evidence | Provenance | Kind | Strongest validation | Counts toward delivery | Note |
|---|---|---|---|---|---|
| Portable utility test composition (`portable-utility-tests`) | test_artifact | unit_contracts | compiled | no | Historical local macOS ARM64 utility validation was 211/211 at 5f29e810 and the upstream reconciliation reached 208/208 at 820b0a03. These are dated snapshots, not current production delivery. |
| Windows x86 MP/SP/dedicated shared-object and stamp parity gate (`m0-shared-object-stamp-parity`) | test_artifact | shared_object_stamp_parity | synthetic_integration | no | The M0 gate compares the shared buildnumber.cpp reference object's retail sections and buildnumber agreement across MP/SP/dedicated with CodeView metadata excluded. It is not whole-engine equivalence and not original-peer interoperability. |
| Scalar determinism / same-profile reproducibility helpers (ki-jgz, #116) (`same-profile-reproducibility`) | scaffold | same_profile_reproducibility | synthetic_integration | no | Reproducible-build helpers establish same-profile determinism only. They do not establish wire equality or command-driven whole-engine simulation parity. |
| Canonical graph-hash instrument (ki-msb, #113) (`graph-hash-instrument`) | scaffold | graph_capture | synthetic_integration | no | Envelope and self-test evidence is not M5 graph parity. The instrument must be enrolled in real walkers over licensed assets before it counts as actual graph capture. |
| Message/netchan wire contracts (`wire-round-trip`) | test_artifact | wire_equality | none | no | The reviewed wire test links no MSG implementation, so it cannot establish wire equality. Production serializer fixtures against commercial references remain open in #127. |
| Command-driven simulation fidelity oracle (`simulation-oracle`) | scaffold | simulation_fidelity | none | no | Recorded-state playback is not simulation fidelity. A fixed-tick usercmd-driven comparison against both commercial references is required (#127) and not yet built. |
| Licensed gameplay smoke (licensed-smoke.yml) (`licensed-smoke`) | licensed_commercial | licensed_content_startup | none | no | Manual-only and deferred: no self-hosted [kisakcod, windows, x86] runner is registered and KISAKCOD_GAME_DIR is unset. Do not dispatch it. |
| Community CoD4x 1.8 products (`cod4x-reference`) | cod4x | none | none | no | CoD4x is a patched community client, not the Steam commercial 1.8 reference, and cannot satisfy either required commercial profile. |

## Derived CI inventory

3 workflows, 22 jobs, 38 matrix-expanded job invocations.

| Workflow | Job | Matrix legs | Self-hosted |
|---|---|---|---|
| ci.yml | portable-tests (Portable tests / ${{ matrix.platform }}) | 5 | no |
| ci.yml | script-sanitizers (Script production paths / ASan + UBSan) | 1 | no |
| ci.yml | capability-dashboard (Capability evidence dashboard) | 1 | no |
| ci.yml | windows-x86 (Windows x86 / ${{ matrix.config }}) | 2 | no |
| ci.yml | windows-x86-sp (Windows x86 SP / ${{ matrix.config }}) | 2 | no |
| ci.yml | windows-x86-parity (Windows x86 (byte parity)) | 1 | no |
| ci.yml | windows-x86-nosteam (Windows x86 (no Steam)) | 1 | no |
| ci.yml | windows-x86-headless (Windows x86 (headless dedicated)) | 1 | no |
| ci.yml | scaffolding-builds (Portable test/release scaffolding / ${{ matrix.target }}) | 6 | no |
| ci.yml | scaffolding-complete (Portable scaffolding complete) | 1 | no |
| licensed-smoke.yml | preflight (Preflight (licensed infrastructure)) | 1 | no |
| licensed-smoke.yml | windows-x86 (Windows x86 legacy dedicated) | 1 | yes |
| licensed-smoke.yml | windows-x86-headless (Windows x86 headless dedicated) | 1 | yes |
| release.yml | verify-tag (Verify release tag) | 1 | no |
| release.yml | build-source (Source archive) | 1 | no |
| release.yml | build-release (Release / ${{ matrix.target }}) | 6 | no |
| release.yml | windows-x86-sp-release (Release / windows-x86 SP) | 1 | no |
| release.yml | windows-x86-nosteam-release (Release / windows-x86 (no Steam)) | 1 | no |
| release.yml | windows-x86-headless-release (Release / windows-x86 (headless dedicated)) | 1 | no |
| release.yml | checksums (Aggregate checksums) | 1 | no |
| release.yml | scaffolding-complete (Release scaffolding complete) | 1 | no |
| release.yml | publish-release (Publish release) | 1 | no |

## Derived test inventory

| Item | Count |
|---|---|
| Configure presets | 6 |
| `add_executable` registrations | 130 |
| `add_test` registrations | 217 |
| `*_tests.cpp` source files | 118 |
| `*_source_test.cmake` contract files | 55 |

Counts describe the current tree only. Dated local/CI snapshots from earlier trees belong in `docs/task.md`'s historical sections, not here.
