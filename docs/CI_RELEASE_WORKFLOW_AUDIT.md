# CI and release workflow audit

**Bead:** `ki-8px` (audit, documentation only). Implementation lands in B17.

**Scope:** `.github/workflows/ci.yml` and `.github/workflows/release.yml`, compared
against the maintained multi-platform workflow patterns in
`jm2/CroMagRally/.github/workflows/{FullCompileCheck.yml,ReleaseBuilds.yml}` and
`jm2/tributary/.github/workflows/{ci.yml,release.yml}`. `.github/workflows/licensed-smoke.yml`
exists but is explicitly marked `DEFERRED` and is not part of this audit's
production CI/release comparison.

This audit documents gaps only. Source files, workflow files, and the
`docs/task.md` M14 checkbox are unchanged by this bead.

## Method

For each gap we record (a) what the maintained pattern does, (b) what
`.github/workflows/ci.yml` or `.github/workflows/release.yml` actually does today,
(c) the concrete change required, and (d) the touched file(s). Every line of
the gap table is verified against the cited workflow file.

## Current-state summary

`ci.yml` has three top-level jobs (`portable-tests`, `windows-x86`, `windows-x86-nosteam`,
`windows-x86-headless`) that together exercise Windows x86 (Debug + Release),
the Windows x86 Steam OFF compile fallback, the Windows x86 headless dedicated
configuration, and `portable-tests` only for the other four target OS/arch
pairs. None of those four pairs has a production engine build, smoke gate,
package step, or release artifact. No `production-complete` aggregator exists.

`release.yml` has a single `windows-x86` job that runs on tag push or
`workflow_dispatch`, builds MP + dedicated via Visual Studio 17 2022, packs
`bin/Release/*` into `KisakCOD-windows-x86.zip`, computes a one-file
`SHA256SUMS.txt`, and attaches both to a GitHub release via
`softprops/action-gh-release@v2`. Top-level `permissions: contents: write`
applies to the whole workflow. No tag→SHA validation, no source archive, no
multi-target matrix, no production gate, no provenance manifest, no
license/notarization handling, no required aggregator.

## Gap table

### 1. Top-level permissions

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml`, `tributary/ci.yml`,
  `tributary/release.yml`): top-level `permissions: contents: read`; job-level
  override `permissions: contents: write` is applied only on the publish job.
- KisakCOD `.github/workflows/release.yml:13`: `permissions: contents: write`
  at the top level — every job in the workflow inherits write.
- Concrete fix: change `release.yml` line 13 to `permissions: contents: read`
  and add a job-level `permissions: contents: write` only on the publish job.
- Files: `.github/workflows/release.yml`.

### 2. Tag/SHA validation in the release workflow

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:42-58`,
  `tributary/release.yml:25-79`): a `prepare`/`release_metadata` job reads the
  canonical version from `version.properties` or `Cargo.toml`, asserts the tag
  resolves to `${GITHUB_SHA}`, and exposes the immutable build ref as a job
  output that every later build job checks out.
- KisakCOD `.github/workflows/release.yml`: triggers on `push: tags: ["v*"]`
  and `workflow_dispatch`; the publish step at line 58 uses
  `tag_name: ${{ inputs.tag || github.ref_name }}` without ever verifying the
  tag resolves to the checkout commit. `actions/checkout@v4` at line 22 is
  invoked without an explicit `ref`, so the runner uses the workflow's event
  ref.
- Concrete fix: insert a `verify-tag` job before `windows-x86` that checks out
  `refs/tags/${{ github.ref_name }}` with `fetch-depth: 1`, asserts
  `git rev-parse HEAD == ${GITHUB_SHA}`, fails closed otherwise, and exports
  `build_ref` for downstream `actions/checkout` calls. The `windows-x86` job
  must then add `needs: verify-tag` and `with: { ref: needs.verify-tag.outputs.build_ref }`.
- Files: `.github/workflows/release.yml`.

### 3. Production-engine matrix coverage

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:137-179`): per-OS/arch
  build jobs with explicit `-A ARM64` / `-G "Visual Studio 17 2022"` flags,
  matched artifact names, and `fail-fast: false`. `tributary/release.yml`
  covers macOS aarch64, Windows x86_64 + aarch64, Linux x86_64 + aarch64 via
  `deb`/`rpm`/`arch` containers.
- KisakCOD `.github/workflows/ci.yml`: `portable-tests` declares five
  OS/architecture pairs (Linux amd64/arm64, Windows amd64/arm64, macOS arm64)
  but they all run the same `cmake -DKISAK_BUILD_MP=OFF -DKISAK_BUILD_DEDICATED=OFF
  -DKISAK_BUILD_SP=OFF -DBUILD_TESTING=ON` test-only build. The actual
  production engine matrix is entirely missing — `windows-x86` is the only
  production-build job, and the task statement at `docs/task.md` line 156-162
  requires production jobs for all five pairs.
- Concrete fix: add a new `production-builds` job to `ci.yml` with a `matrix`
  that mirrors the five records in `portable-tests` plus a `windows-x86` pair,
  each entry carrying `preset`, `runner`, and per-target build flags that
  exercise the production engine. The job must set
  `KISAK_BUILD_MP`/`KISAK_BUILD_DEDICATED`/`KISAK_BUILD_SP` to the production
  profile for that target and upload a target-labeled binary artifact.
- Files: `.github/workflows/ci.yml`.

### 4. Smoke/parity gate per production target

- Maintained pattern (`CroMagRally/FullCompileCheck.yml:69-79`):
  `ctest --test-dir build --output-on-failure --no-tests=error` after every
  platform compile. `CroMagRally/ReleaseBuilds.yml:60-99` builds a
  `CroMagRally-${VERSION}-source.tar.gz` archive and asserts the archive
  contains expected submodules. `tributary/ci.yml:147-148`: `build-aux/linux/validate-package-compliance.sh
  --elf target/release/tributary`.
- KisakCOD `.github/workflows/ci.yml`: `portable-tests` runs `ctest` at line
  47 after the test-only build. There is no per-target architecture assertion
  (e.g. `cmake -LA` shows `CMAKE_SYSTEM_PROCESSOR`, or `file` on the produced
  binary matches the runner), no licensed-data smoke (the deferred
  `licensed-smoke.yml` is the right place for that when the self-hosted runner
  exists), and no byte-parity or reproducibility gate.
- Concrete fix: after each `production-builds` matrix entry's build step,
  insert an `Assert target architecture` step that runs
  `cmake -LA build/<config> | grep -E '^CMAKE_(SYSTEM_)?(PROCESSOR|ARCH).*'` and
  `file bin/<config>/KisakCOD-*` and fails if the reported architecture does
  not match the matrix entry. After build, run the affected portable-test
  subset (see gap 6) and upload the binary as a target-labeled artifact.
- Files: `.github/workflows/ci.yml`.

### 5. Packaging as an explicit gated stage

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:468-617`,
  `tributary/release.yml:342-528`): each Linux distribution gets its own
  job+container (debian:trixie, fedora:42, archlinux:base-devel) that produces
  the artifact via `cargo deb`/`cargo generate-rpm`/`makepkg` and runs a
  `validate-package-compliance.sh` post-step.
- KisakCOD `.github/workflows/release.yml:44-50`: packaging is a single
  `Compress-Archive` of `bin/Release/*` into a Windows-only zip. No package
  manifests, no per-target archive, no executable-bit check, no
  dependency-closure assertion. `ci.yml` does not produce any production
  artifact at all.
- Concrete fix: add a per-target packaging job that stages build output into a
  clean `dist/<target>/` tree, asserts executable bits on POSIX archives via
  `unzip -l`/`tar -tvf`, computes SHA-256 per file, and uploads the archive
  as a workflow artifact named `KisakCOD-<target>-<config>`.
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 6. Affected-test subset (local + CI parity)

- Maintained pattern (`tributary/ci.yml:139-157`): runs `cargo fmt --check`,
  `cargo clippy --all-targets -- -D warnings`, debug + release `cargo test`,
  and a fuzz-workspace fmt/clippy pass as separate steps. Every step is
  independently gated.
- KisakCOD `.github/workflows/ci.yml:46-47` runs the full `ctest` on every
  push, with no way to scope to the affected files. The `windows-x86` job at
  line 162 calls the same full `ctest` against an explicit allowlist regex.
  There is no `affected_tests_command` in the rig `formula_vars`.
- Concrete fix: define an `affected_tests_command` in the kisakcod rig's
  `formula_vars` that runs `git diff --name-only origin/master...HEAD`,
  filters to `tests/**`, and dispatches `ctest -R` against the
  matching test targets. Wire that command into `mol-polecat-work`'s
  `affected_tests_command` variable and into `ci.yml` so a single change only
  re-runs its affected tests on PR.
- Files: rig `formula_vars`, `.github/workflows/ci.yml`.

### 7. Source archive + provenance manifest

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:60-99`,
  `tributary/release.yml:25-79`): a dedicated `build-source` job creates a
  reproducible `tar.gz` from the validated tag, records submodule SHAs in a
  `SUBMODULES.txt`, asserts known files exist in the archive, and uploads the
  archive as `*-source`. The release aggregate `SHA256SUMS` covers source and
  every binary.
- KisakCOD `.github/workflows/release.yml`: no source archive job. The
  `SHA256SUMS.txt` at line 50 is a single-line file holding only the zip hash.
  There is no `GAME_VERSION`, `GAME_VERSION` + `${GITHUB_SHA}`, toolchain, or
  target triple in any released artifact.
- Concrete fix: add a `build-source` job that runs after `verify-tag`,
  produces `dist/KisakCOD-${VERSION}-source.tar.gz` via `git archive` from the
  verified SHA, includes a `provenance.json` carrying
  `{commit, tag, target, toolchain, config}`, and uploads both as
  `KisakCOD-${VERSION}-source`.
- Files: `.github/workflows/release.yml`.

### 8. Aggregate SHA256SUMS + aggregator job

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:619-636`,
  `tributary/ci.yml:519-536`, `tributary/release.yml:537-563`): a single
  `checksums` job `needs:` every build job, downloads every artifact via
  `actions/download-artifact`, produces a sorted `SHA256SUMS.txt`, and uploads
  the file as a workflow artifact. `publish-release` then re-downloads the
  full set, regenerates a flat `SHA256SUMS.txt`, and attaches it to the GitHub
  Release with `fail_on_unmatched_files: true`.
- KisakCOD `.github/workflows/release.yml`: each build job computes its own
  one-line `SHA256SUMS.txt` (line 50). There is no cross-job aggregator, no
  `production-complete` job, no `publish-release` step that scans all
  artifacts. The `softprops/action-gh-release` action uploads `files:`
  literally, with no duplicate-name guard.
- Concrete fix: add a `checksums` job that `needs:` every production-build job,
  downloads all artifacts, fails on duplicate filenames whose contents
  differ, regenerates a sorted `SHA256SUMS.txt` for the union of files, and
  uploads the result. Add a `production-complete` aggregator that `needs:`
  every production-build + checksums job and is the only direct prereq of the
  publish job.
- Files: `.github/workflows/release.yml`.

### 9. Concurrency and cancellation policy

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:21-24`):
  `concurrency: { group: ${{ github.workflow }}-${{ github.ref }}, cancel-in-progress: false }`
  for release, and `cancel-in-progress: true` for CI
  (`CroMagRally/FullCompileCheck.yml:17-19`). `tributary/release.yml` mirrors
  this with explicit `cancel-in-progress: false`.
- KisakCOD `.github/workflows/{ci,release}.yml`: neither workflow declares a
  `concurrency:` block. Two pushes to the same tag will race two release
  publishes, and rapid PR pushes cannot cancel superseded CI.
- Concrete fix: add `concurrency: { group: ${{ github.workflow }}-${{ github.ref }}, cancel-in-progress: <bool> }`
  to both files — `true` for `ci.yml`, `false` for `release.yml`.
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 10. Pinned action versions

- Maintained pattern (`CroMagRally`, `tributary`): every `uses:` line pins a
  full commit SHA (e.g.
  `actions/checkout@df4cb1c069e1874edd31b4311f1884172cec0e10 # v6`,
  `actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a # v7`).
- KisakCOD `.github/workflows/ci.yml:32,60,164,182,245`,
  `.github/workflows/release.yml:22,52,59` use floating tags
  (`actions/checkout@v4`, `actions/setup-dotnet@v4`,
  `actions/upload-artifact@v4`, `softprops/action-gh-release@v2`). A supply
  chain incident on any of these tags would silently change what runs.
- Concrete fix: replace every `uses: <action>@<tag>` with the current pinned
  full commit SHA matching the tag (`@v4` → `<current v4 SHA> # v4`).
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 11. `windows-x86` job is the only production build

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:137-179`,
  `tributary/release.yml:213-339`): each target OS/arch gets its own job with
  its own toolchain setup (`actions/setup-dotnet@v4`,
  `msys2/setup-msys2@v2.32.0`, `brew install`), its own CMake configure
  flags, and its own artifact.
- KisakCOD `.github/workflows/ci.yml:49-169` (`windows-x86`),
  `.github/workflows/release.yml:16-65` (`windows-x86`): both target only
  Windows x86. The release job additionally does `KISAK_BUILD_MP=ON` /
  `KISAK_BUILD_DEDICATED=ON` / `KISAK_BUILD_SP=OFF` while CI's `windows-x86`
  job uses `-DCICD=ON -DKISAK_BUILD_MP=ON -DKISAK_BUILD_DEDICATED=ON -DKISAK_BUILD_SP=OFF
  -DKISAK_MEASURE_FX_ARCHIVE_STACK=ON` — different feature flags in CI vs
  release means CI is not validating what release ships.
- Concrete fix: factor the Windows x86 configure+build into a single script
  referenced by both `ci.yml` and `release.yml`, or pass a shared `cmake-args`
  matrix entry consumed by both. Then add equivalent per-target production
  jobs for Linux amd64, Linux arm64, Windows arm64, and macOS arm64 (see gap
  3).
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`,
  `scripts/ci/`.

### 12. No `windows-x86-nosteam` / `windows-x86-headless` parity in release

- Maintained pattern: every build matrix entry has parity gates and shared
  configuration between CI and release.
- KisakCOD `.github/workflows/ci.yml:171-250` adds two extra jobs
  (`windows-x86-nosteam`, `windows-x86-headless`) that exercise the Steam OFF
  fallback and the headless dedicated configuration. Neither is mirrored in
  `release.yml`. A release that switches the Steam ON path does not gate
  against the Steam OFF path, and a headless-server regression would only
  surface in CI.
- Concrete fix: add `windows-x86-nosteam-release` and
  `windows-x86-headless-release` jobs to `release.yml` whose
  `KISAK_ENABLE_STEAM=OFF` / `KISAK_DEDI_HEADLESS=ON` flags match their CI
  counterparts. These should be required prerequisites of the publish job.
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 13. `softprops/action-gh-release` invoked from the build job

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:705-708`): the
  `softprops/action-gh-release` invocation lives in a dedicated
  `publish-release` job with `permissions: contents: write` and
  `fail_on_unmatched_files: true`. The release build jobs only upload
  artifacts to the workflow run.
- KisakCOD `.github/workflows/release.yml:58-65`: `Publish GitHub release` is
  one of the final steps of the `windows-x86` job — the same job that
  built, configured, and packaged the binary, and which inherits the
  workflow-wide `contents: write` (see gap 1). No
  `fail_on_unmatched_files`.
- Concrete fix: remove `Publish GitHub release` from `windows-x86`. Add a
  `publish-release` job whose only steps are `actions/download-artifact`,
  flatten-with-dedupe, regenerate `SHA256SUMS.txt`, and the
  `softprops/action-gh-release` invocation with `fail_on_unmatched_files: true`.
  Set `permissions: contents: write` on this job only.
- Files: `.github/workflows/release.yml`.

### 14. Windows x86 ARM64 release job is a near-clone, not a matrix

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml:158-179`):
  `build-windows-arm64` is a near-clone of `build-windows` with the only
  changes being `runs-on: windows-11-arm` and `-A ARM64`. The duplication is
  intentional and called out in a comment.
- KisakCOD `.github/workflows/ci.yml:26-31`: the `portable-tests` matrix
  already enumerates `Windows arm64` against `windows-11-arm`, but there is no
  parallel Windows arm64 production job anywhere. The architecture asymmetry
  means Windows arm64 production is unproven.
- Concrete fix: add a `windows-arm64` matrix entry to `production-builds` that
  mirrors the CroMagRally pattern (`runs-on: windows-11-arm`,
  `-G "Visual Studio 17 2022" -A ARM64`, separate artifact name).
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 15. Retention policy is implicit

- Maintained pattern (`CroMagRally/ReleaseBuilds.yml`,
  `tributary/ci.yml:172-176`): `actions/upload-artifact` is called with
  explicit `retention-days` and `if-no-files-found: error`, plus
  `compression-level: 0` for archive bundles (compression is a separate
  publisher concern).
- KisakCOD `.github/workflows/ci.yml:168-169,248-249`: retention is set to 14
  days for `windows-x86` and `windows-x86-headless`. No retention is set on
  `portable-tests`. `release.yml` does not declare any artifact retention
  policy — the GitHub release itself is the long-term store, but the
  one-line `SHA256SUMS.txt` and the zip are uploaded as workflow artifacts
  with the default 90-day retention.
- Concrete fix: pin `retention-days` consistently across every upload
  (`if-no-files-found: error` already enforced) and document the policy in a
  top-of-file comment.
- Files: `.github/workflows/ci.yml`, `.github/workflows/release.yml`.

### 16. No architecture truth at upload time

- Maintained pattern (`CroMagRally/FullCompileCheck.yml`,
  `tributary/ci.yml:147-148`): after every build, a small validator script
  inspects the produced binary (e.g. `file`, `readelf -h`) and asserts the
  reported architecture matches the matrix entry.
- KisakCOD `.github/workflows/ci.yml`: there is no post-build architecture
  assertion. A Windows arm64 runner that silently fell back to x86 emulation
  would still upload a binary labeled `windows-arm64`.
- Concrete fix: add a `Verify binary architecture` step to every matrix
  build job that runs `file bin/<config>/KisakCOD-*` (or `dumpbin /headers`
  on Windows) and asserts the output contains the expected architecture
  token. Fail closed on mismatch.
- Files: `.github/workflows/ci.yml`.

## Concrete patch proposal

The following is the proposed `ci.yml`/`release.yml` shape. It is intentionally
a proposal rather than an implementation because this bead is an audit and the
implementation lands in B17.

```diff
+ .github/workflows/ci.yml
+ permissions:
+   contents: read
+ concurrency:
+   group: ${{ github.workflow }}-${{ github.ref }}
+   cancel-in-progress: true
+ jobs:
+   portable-tests:        # unchanged: five OS/arch records, ctest after build
+
+   production-builds:     # NEW: same five records as portable-tests + windows-x86
+     strategy:
+       fail-fast: false
+       matrix:
+         include:
+           - target: windows-x86,   runner: windows-2022,  generator: "Visual Studio 17 2022", arch: Win32
+           - target: windows-amd64, runner: windows-2025,  generator: "Visual Studio 17 2022", arch: x64
+           - target: windows-arm64, runner: windows-11-arm, generator: "Visual Studio 17 2022", arch: ARM64
+           - target: linux-amd64,   runner: ubuntu-24.04,    generator: "Unix Makefiles",       arch: x86_64
+           - target: linux-arm64,   runner: ubuntu-24.04-arm,generator: "Unix Makefiles",       arch: aarch64
+           - target: macos-arm64,   runner: macos-15,        generator: "Unix Makefiles",       arch: arm64
+     steps:
+       - actions/checkout@<pinned-sha> # v4
+       - run: cmake -S . -B build-release -G "${{ matrix.generator }}" -A ${{ matrix.arch }} ...
+       - run: cmake --build build-release --config Release --parallel
+       - run: file bin/Release/KisakCOD-*    # architecture truth gate
+       - run: ctest --test-dir build-release -C Release --output-on-failure
+       - actions/upload-artifact@<pinned-sha> # v4 with name=KisakCOD-<target>-Release

+   production-complete:   # NEW: aggregator; required by release.yml's publish job
+     needs: [portable-tests, production-builds]
+     if: ${{ always() && !cancelled() }}
+     runs-on: ubuntu-24.04
+     steps:
+       - run: |
+           for s in ${{ needs.*.result }}; do
+             if [ "$s" != "success" ]; then exit 1; fi
+           done

+ .github/workflows/release.yml
+ on:
+   push:
+     tags: ['v*.*.*']
+   workflow_dispatch:
+     inputs:
+       tag: { required: true, type: string }
+ permissions:
+   contents: read
+ concurrency:
+   group: ${{ github.workflow }}-${{ github.ref }}
+   cancel-in-progress: false
+ jobs:
+   verify-tag:             # NEW: tag/SHA validation, source archive
+     steps:
+       - actions/checkout@<pinned-sha> # v4 with ref=refs/tags/${{ inputs.tag || github.ref_name }}, fetch-depth=1
+       - run: test "$(git rev-parse HEAD)" = "${GITHUB_SHA}"
+       - run: git archive -o dist/KisakCOD-${VERSION}-source.tar.gz HEAD
+       - actions/upload-artifact@<pinned-sha> # v4 with name=KisakCOD-${VERSION}-source

+   build-release:          # NEW: matrix of windows-{x86,amd64,arm64}, linux-{amd64,arm64}, macos-arm64
+     needs: verify-tag
+     uses: ./.github/workflows/ci.yml
+     with: { release: true }

+   checksums:              # NEW: download all artifacts, sorted SHA256SUMS, dedupe by basename+content
+     needs: [build-release]
+     if: always()
+     steps:
+       - actions/download-artifact@<pinned-sha> # v4
+       - run: find artifacts -type f -exec sha256sum {} + | sort -k2 > SHA256SUMS.txt
+       - actions/upload-artifact@<pinned-sha> # v4 with name=SHA256SUMS

+   publish:                # replaces the existing windows-x86's publish step
+     needs: [verify-tag, checksums]
+     permissions: { contents: write }
+     if: github.event_name == 'release'
+     steps:
+       - actions/download-artifact@<pinned-sha> # v4
+       - softprops/action-gh-release@<pinned-sha> # v2 with fail_on_unmatched_files: true
```

## Acceptance criteria check

- "Audit document records every gap" — gap table items 1-16 above each cite the
  maintained pattern, the current KisakCOD file and line, and the concrete
  change required. Every maintained-pattern claim is traceable to a specific
  workflow file.
- "Concrete patch proposal diff is included" — the diff-shaped sketch in this
  document covers the top-level structure of the new `ci.yml` and
  `release.yml` (top-level permissions, concurrency, new jobs, de-duplicated
  matrix, aggregator, tag/SHA validation, source archive, SHA256SUMS,
  publish-job-only write permissions).
- "No source changes made in this bead" — confirmed: this bead only writes
  `docs/CI_RELEASE_WORKFLOW_AUDIT.md`; the audit will be rebased onto master
  unchanged.