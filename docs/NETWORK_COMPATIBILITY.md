# Commercial network compatibility contract

Status: mandatory requirement; implementation and reference-binary validation are incomplete.
Established by the project owner's September 9, 2026 instruction. This contract
supersedes earlier statements that retail interoperability is optional, a
non-goal, or satisfied by KisakCOD-to-KisakCOD self-consistency.
Tracked by [compatibility parent #122](https://github.com/jm2/kisakcod/issues/122)
and the dependent work in [task.md](task.md#current-priorities-and-gas-city-handoff).

## Required outcome

The MP client and headless dedicated server must retain perfect network
compatibility with the original, unmodified commercial Call of Duty 4 1.7 and
Steam-distributed commercial 1.8 binary releases. Each of the five requested
OS/architecture targets must pass the same applicable requirements. Preserve
Windows x86 as an additional regression/reference platform; it is not the sole
compatibility oracle. Native SP remains separately deferred.

Commercial Steam 1.8 and the community CoD4x products called 1.8 are distinct
references. CoD4x is not a substitute for either required commercial binary.
The [CoD4x project's own installation documentation](https://github.com/callofduty4x/cod4x-docs/blob/master/cod4x-client/installation.md)
describes patching a client and changing its displayed version to 1.8; a version
label alone therefore cannot establish binary identity. No executable hash,
Steam depot manifest, protocol number or patch build is invented in this plan.

Perfect compatibility means identical specified wire encodings for identical
inputs and matching valid-peer protocol behavior, not byte equality of two live
captures containing different challenges, timestamps or identities. Record every
such variable field and its invariant explicitly. Do not normalize away a
genuine gameplay, serialization or protocol mismatch.

## Reference manifest and current blockers

The remote Gas City operator must establish a reproducible manifest for **both**
commercial references: acquisition/provenance, Steam app/depot/manifest or patch
identifier where available, executable SHA-256, displayed/file version, client
and dedicated/listen-server launch modes, relevant data/language hashes, observed
protocol behavior and test configuration. Keep licensed binaries, game data,
CD keys, account identifiers and authentication tickets out of the public repo
and issue attachments. Store sanitized fixtures and evidence metadata only.

Both required reference entries are **pending** in this checkout. Missing
references or unavailable infrastructure are blockers to compatibility
certification, never passing or silently skipped results. Preserve the licensed
workflow's existing deferral until its protected runner/content are provisioned.

Current production code contains explicit work to resolve:

- `src/server_mp/sv_client_mp.cpp`, `SV_DirectConnect`, accepts protocol `1`.
- `src/server_mp/sv_init_mp.cpp`, `SV_Init`, registers protocol `1`.
- `src/client_mp/cl_main_mp.cpp`, `CL_CheckForResend`, advertises protocol `1`
  and sends a custom ticket/identity `getchallenge` tuple.
- `src/server_mp/sv_client_mp.cpp`, `SV_GetChallenge`, implements the matching
  fork-specific identity/ticket expectations.

Changing one version constant is not an interoperability fix. Determine each
commercial dialect from authentic references and audit both ends of discovery,
challenge, authorization, connection, state transfer and ongoing gameplay.
Existing comments claiming no-Steam cross-play refer to the fork's own paths;
they are not evidence that original commercial clients can connect.

## Invariants for every issue and implementation

1. Preserve valid commercial packet layouts, field widths/order, endian rules,
   bit packing, Huffman coding, quantization/rounding, numeric enums, sequence and
   reliable-acknowledgment semantics, fragmentation and checksums. Native pointers,
   host padding and widened runtime sizes never become wire data.
2. Preserve commercial discovery/status, challenge/connect/authentication,
   gamestate/configstrings/baselines, snapshots/deltas, usercmds, reliable commands,
   downloads/pure checks, stats and voice codec/framing behavior. Map changes,
   reconnects, loss/reordering and timeouts must remain compatible.
3. Original peers must work without replacement executables, injected DLLs,
   CoD4x installation, a protocol bump, forced upgrade/downgrade, or a new
   KisakCOD-only authentication convention. Do not bypass authentication or claim
   unavailable external authentication services are passing tests. Identify
   provider/configuration constraints and preserve them in the result.
4. Security fixes may reject malformed/invalid input while retaining every valid
   commercial message and its semantics. Do not require reproducing memory
   corruption. Prove valid boundary behavior against the references; do not use
   broad new limits, dvar allowlists, rounding changes or transport/codec swaps
   that reject or alter legitimate retail behavior. An unresolved tradeoff is a
   blocker to acceptance, not a silent compatibility exception.
5. Optional modern protocols, gameplay changes, identity schemes and codec
   upgrades belong in separately scoped features and cannot replace or alter the
   required default commercial-compatible behavior.
6. Record supported behavior separately for commercial 1.7 and Steam 1.8. Establish
   the commercial-to-commercial baseline first. If their dialects differ, design
   explicit tested compatibility profiles or valid negotiation; never infer
   interchangeability from the shared product name. Mixed-version claims require
   their own evidence. Both required commercial profiles remain release gates.

## Required evidence

| Layer | Required tests | What it proves |
|---|---|---|
| Authentic baseline | Pin both commercial builds; run each reference client against its reference server; capture sanitized known-input transactions. | Correct reference identity, working configuration and version-specific behavior. |
| Production wire contracts | Link actual MSG/netchan encoders/decoders; byte-compare fixed inputs with reference fixtures, validate round trips and boundaries on x86/x64/ARM64. | Wire fidelity; a struct-size test alone cannot satisfy it. |
| Real bidirectional sessions | Each KisakCOD server target with each original commercial client; each KisakCOD client target with each original commercial server profile, including dedicated/listen modes where applicable. | Actual interoperation without patched retail peers. |
| Session behavior | Discover, authorize, join, download/pure-check, enter a match, exchange commands/snapshots/stats/voice, change maps, reconnect and disconnect; test supported loss/reorder/fragmentation boundaries. | Ongoing compatibility beyond a successful handshake or getstatus response. |
| Simulation | Fixed initial state, ticks, usercmds and RNG; compare network-visible movement/physics/events with the commercial baseline and across native architectures. | Behavioral compatibility; playback of recorded positions alone cannot prove it. |
| Release | Attach exact engine/reference hashes, data manifest, compiler/configuration, target hardware, commands, expected outcomes, logs and deviations to the release capability record. | Reproducible certification for the actual artifact. |

Test non-network rendering/audio details with suitable tolerances, but retain
exactness wherever they affect packet contents or network-visible behavior.
Separate graph/hash/decoder instruments from captures using real production
consumers. Sanitized redistributable synthetic fixtures support early work;
licensed original-binary sessions remain necessary for the final gate.

No network-affecting implementation may be declared compatibility-complete
without the relevant commercial reference evidence. Unrelated portable
foundations can progress while full integration is blocked, provided their
reported scope stays accurate and existing validated behavior is preserved.
No target/release is fully delivered while either required commercial profile
remains unvalidated. Compatibility is a required result still to prove, not a
claim made by this documentation change.

## Gas City handoff

Gas City runs on another machine. Fork GitHub issues and `docs/task.md` are the
handoff surface. Cross-link existing `ki-*` work and add dependent acceptance
work instead of recreating active implementations. The operator should attach
the local bead/worker ownership and evidence after ingestion. Closing a helper,
backend or scaffold PR must not close its commercial-compatibility parent.
Split reference capture, harness construction, production integration and final
session acceptance into ordered beads; cross-linked capability issues must not
be imported as mutually blocking whole-issue dependencies.
