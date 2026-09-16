# Derived Shader Cache Policy (A09 / issue #131)

This document records the backend/translation policy for retail DX9 shader
content and the content-addressed derived shader cache that preserves the
original archives. It is governed by issue
[#131](https://github.com/jm2/kisakcod/issues/131), the renderer feasibility
item `A09` in `docs/ROADMAP_EXPANSION_PROPOSAL.md`, the shader pipeline in
`docs/PORTING.md`, and the mandatory compatibility rules in
`docs/NETWORK_COMPATIBILITY.md` (issue #122).

It does **not** claim that native Vulkan or MoltenVK feasibility is proven, and
it does not claim original-commercial interop. Those require runtime and
licensed-reference evidence listed under "Evidence status" below.

## Required policy

The policy is fixed by `docs/ROADMAP_EXPANSION_PROPOSAL.md:92` and
`docs/PORTING.md` (shader pipeline section):

1. Load original fast-files unchanged.
2. Identify bytecode by content hash.
3. Translate and cache without rewriting the user's retail archive.
4. Version the translator and the cache format.
5. Regenerate from the original inputs.
6. Reject unsupported programs clearly.

Generated HLSL is not automatically a replacement for every retail or mod
shader whose original source is unavailable, so the cache keys on the original
compiled bytecode rather than on regenerated source.

## Content addressing

The portable core is `src/database/shader_cache.{h,cpp}`. It has no D3D9 or
Win32 surface and performs no file I/O; translation and storage stay at the
platform boundary where the retail archives are read (never rewritten). This
change enrolls the core in the database/engine source manifest
(`scripts/common_files.cmake`) and exercises it on the Linux host; enrolling it
at the D3D9 shader creation boundary (the production call site) is future
renderer integration and is not wired here.

* Every digest is a domain-separated SHA-256 over an explicit, width-tagged,
  little-endian stream (`db::graph_hash::GraphHashBuilder`). The domain tag is
  `kisakcod/derived-shader-cache/v1` (`kHashDomain`). Digests are therefore
  independent of host byte order and struct layout.
* `SourceIdentity` records stage, `loadForRenderer`, original dword count, the
  original-bytecode content checksum (`contentHash`), and a full cache key
  (`cacheKey`). The original retail FF bytes are retained and never modified;
  `contentHash` is the retained content checksum.
* Only bytecode that passes `db::validation::MaterialShaderLoadDefValid` and
  `db::validation::D3D9ShaderBytecodeValid` (SM2/SM3 token stream, exact END
  token) can mint an identity. Anything else is `UnsupportedSource` and must be
  rejected by the caller with `kUnsupportedProgramMessage`.

## Sidecar framing

A sidecar is a fixed 100-byte header followed immediately by the derived
artifact payload. Fields are serialized little-endian, one at a time.

| Offset | Field | Purpose |
|-------:|-------|---------|
| 0 | magic `KSDShdr1` | Reject foreign/truncated files |
| 8 | `formatVersion` | Sidecar framing identity |
| 12 | `converterVersion` | Translator identity |
| 16 | `stage` | Vertex / pixel |
| 20 | `loadForRenderer` | Shader model (SM2 / SM3) |
| 24 | `sourceDwordCount` | Original bytecode size |
| 28 | `sourceHash` | Original-bytecode content checksum |
| 60 | `artifactKind` | Derived artifact kind (`SpirV` today) |
| 64 | `artifactSize` | Payload byte count |
| 68 | `artifactHash` | Payload content checksum |

## Invalidation and regeneration

`db::shader_cache::LookupDerivedShader` classifies evidence as one of:

* `Hit` — header matches the original bytecode and the payload hash verifies.
* `Missing` — no usable sidecar (translate and store).
* `NeedsRegeneration` — a sidecar exists but the format version, converter
  version, artifact kind, source identity, or payload hash does not match; the
  derived artifact is rebuilt from the untouched original inputs.
* `UnsupportedSource` — the original program is not valid SM2/SM3 bytecode;
  reject rather than cache.

Because `formatVersion` and `converterVersion` participate in the cache
directory name and the header, a version bump lands in a fresh
`derived-shader-cache/formatN-converterM/` directory and never reuses stale
artifacts. No version bump ever modifies a retail archive.

## Evidence status

The following are required before A09 can be reported as feasible/complete and
are **not** satisfied by this policy and core alone:

| Evidence | Status |
|----------|--------|
| Bounded retail shader/scene experiment (constants, skinning, alpha, shadows, post, RTs, device/resize) | Pending — needs GPU runner and retail content |
| D3D9 parity reference and optional dxvk-native differential | Pending |
| Native Vulkan on Linux and MoltenVK/Apple Silicon; unsupported features, fallbacks, perf/memory, warm/cold cache | Pending — needs Apple/Linux GPU runners |
| Windows ARM64 shipping backend and device capability evidence | Pending — needs ARM64 hardware |
| Renderer changes preserve asset checksums and network-visible behavior (#122) | Pending — needs original-commercial reference peers |
| Content-addressed derived shader cache/sidecar definition and portable implementation | **Done in this change** (core, wiring, contract tests) |

Missing commercial reference or runner evidence remains an explicit blocker to
certification; helper-only coverage must not be reported as a proven milestone.
