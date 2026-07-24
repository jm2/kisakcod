# CI and release workflow audit

**Scope:** `.github/workflows/ci.yml` and `.github/workflows/release.yml`, compared with the maintained multi-platform workflow patterns in `jm2/CroMagRally` and `jm2/tributary`. The checked-out KisakCOD tree currently contains neither workflow file, so the findings below are based on the workflow requirements recorded in `docs/task.md` and the maintained-pattern requirements named by M14.

## Current-state gaps

| Area | KisakCOD gap | Maintained pattern / required outcome | Proposed change |
| --- | --- | --- | --- |
| Supported target coverage | No checked-in workflows currently define jobs for the five requested production pairs: Windows amd64, Windows ARM64, Linux amd64, Linux ARM64, and macOS ARM64. | Explicit OS/architecture jobs, not a nominal matrix whose steps silently skip unsupported targets. | Add one production job per target, with `runs-on`, toolchain, and artifact naming fixed by target. Keep portable utility jobs separate from production jobs. |
| Build versus release coverage | Existing status claims portable utility coverage, but `docs/task.md` still marks every production target and M14 production CI incomplete. | Maintained workflows distinguish test/build, package, and release jobs. | Add target-specific build and packaging jobs to CI; make release jobs consume those exact artifacts rather than rebuild independently. |
| Architecture truth | A matrix alone does not prove that the requested architecture was built; hosted runner defaults and cross-compilers can produce the host architecture. | Each job records and verifies the target triple/architecture. | Add an explicit architecture assertion after configure/build (`cmake -LA`, compiler target query, and artifact binary inspection). Fail on mismatch. |
| Smoke/parity gates | No workflow evidence currently proves licensed gameplay smoke, map/demo startup, or x86/reference parity for each production target. | Production jobs include a runnable smoke/parity gate before artifacts are publishable. | Add a target-labeled smoke job per artifact; retain Windows x86 reference checks and compare deterministic/version/hash evidence where runtime is available. |
| Packaging gates | No checked-in workflow currently establishes target-specific package contents, executable permissions, dependency closure, or reproducible archives. | Packaging is an explicit gated stage, not an upload side effect of compilation. | Add package manifests, clean staging directories, dependency checks, archive creation, and package-content assertions for every target. |
| Provenance | No workflow evidence requires immutable source identity, toolchain metadata, or build inputs in the artifact. | Artifacts carry commit SHA, tag, target, compiler/toolchain, and reproducibility metadata. | Generate a provenance JSON/SBOM-like manifest from immutable `${GITHUB_SHA}` and checked-in toolchain versions; include it in every package. Reject non-SHA release refs. |
| Release ref safety | A release workflow must not publish from a mutable branch or moving tag. | Maintained release patterns validate the tag against the checked-out commit. | Require a semantic version tag, fetch the tag object, verify it resolves to `${GITHUB_SHA}`, and use least-privilege permissions only in the publish job. |
| Artifact identity | Generic artifact names can collide across OS/architecture/configuration and hide overwrites. | Artifact names are target/configuration-specific and immutable. | Use names such as `kisakcod-windows-amd64`, `kisakcod-linux-arm64`, and `kisakcod-macos-arm64`, including version and SHA in release archives. |
| Checksum aggregation | No evidence currently creates one aggregate checksum file for all supported release artifacts. | Release produces a single `SHA256SUMS` covering every package and the reproducible source archive. | Download all target artifacts in a final job, generate sorted `SHA256SUMS`, verify archive names are unique, and publish the checksum file with the release. |
| Source archive | No workflow evidence creates a reproducible source archive. | Release includes a source archive derived from the immutable tag and normalized file metadata. | Add a source-archive job using `git archive` from the verified tag, then record its checksum in `SHA256SUMS`. |
| Failure propagation | Matrix-only workflows commonly allow experimental/unsupported entries to pass with skipped steps. | Unsupported or failed targets fail the aggregate workflow. | Remove `continue-on-error` from production/package jobs; add an explicit required `production-complete` aggregator that requires every target and each smoke/package gate. |
| Retention and handoff | Artifact retention and release promotion are not established for the requested target set. | CI artifacts are retained long enough for review; release promotion is a separate, protected step. | Set a documented retention period, upload only after package/smoke gates, and make publication depend on the aggregate gate and verified provenance. |

## Concrete patch proposal

The following is the proposed workflow shape. It is intentionally a proposal rather than an implementation because this bead is an audit and the current tree has no `.github/workflows/` files.

```diff
+ .github/workflows/ci.yml
+ jobs:
+   portable:
+     strategy:
+       matrix:
+         include:
+           - target: windows-amd64
+             runner: windows-2022
+             preset: portable-windows-amd64
+           - target: windows-arm64
+             runner: windows-2022
+             preset: portable-windows-arm64
+           - target: linux-amd64
+             runner: ubuntu-24.04
+             preset: portable-linux-amd64
+           - target: linux-arm64
+             runner: ubuntu-24.04-arm
+             preset: portable-linux-arm64
+           - target: macos-arm64
+             runner: macos-14
+             preset: portable-macos-arm64
+     steps:
+       - checkout at the immutable commit
+       - configure the matrix target explicitly
+       - assert host/compiler target architecture
+       - build and run the affected portable tests
+       - upload target-labeled test evidence
+
+   production:
+     strategy:
+       fail-fast: false
+       matrix:
+         include: <same five explicit target records>
+     steps:
+       - checkout at the immutable commit
+       - install the pinned target toolchain/dependencies
+       - configure production client/server or headless target
+       - assert target architecture and dependency closure
+       - build
+       - run target smoke/parity gate
+       - package and validate package contents
+       - write provenance manifest containing SHA, target, toolchain, and config
+       - upload immutable target-labeled package
+
+   production-complete:
+     needs: [portable, production]
+     steps:
+       - require successful result for every matrix target
+       - verify all five target artifacts exist
+
+ .github/workflows/release.yml
+ on:
+   push:
+     tags: ['v*.*.*']
+ permissions:
+   contents: read
+ jobs:
+   verify-tag:
+     steps:
+       - checkout the tag and verify it resolves to GITHUB_SHA
+       - verify the tag is annotated/signed according to project policy
+   build-release:
+     needs: verify-tag
+     uses: ./.github/workflows/ci.yml
+     with: release: true
+   checksums:
+     needs: build-release
+     steps:
+       - download all five target packages
+       - create reproducible source archive from the verified tag
+       - generate sorted SHA256SUMS
+       - verify target names, provenance manifests, and nonempty packages
+   publish:
+     needs: [checksums]
+     permissions:
+       contents: write
+     steps:
+       - publish the five packages, source archive, SHA256SUMS, and provenance
+```

## Recommended implementation order

1. Restore/check in `ci.yml` with the five explicit portable and production target records.
2. Add architecture assertions and make every production build fail closed on mismatch.
3. Add smoke, package-content, and dependency-closure gates; aggregate them in a required job.
4. Add provenance and target-specific immutable artifacts.
5. Implement `release.yml` tag/SHA verification, source archive, aggregate `SHA256SUMS`, and least-privilege publication.
6. Run licensed gameplay smoke and release packaging on the exact release commit; record the run IDs and artifact checksums in `docs/task.md`.

This fulfills the audit acceptance requirement: every named gap is recorded, each gap is tied to a maintained multi-platform workflow expectation, and the concrete patch shape is provided without modifying source or workflow files.
