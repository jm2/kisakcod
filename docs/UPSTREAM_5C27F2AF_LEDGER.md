# Upstream reconciliation ledger through `5c27f2af`

This ledger records the disposition of the 16 upstream commits after the
previously audited `c6be07a2` checkpoint. The fork's runtime, serialized ABI,
network protocol, and portability contracts have diverged substantially from
upstream, so naming refactors are not imported without profile-specific and
wire-format validation. Tree-neutral checkpoint
`86459c535ee78043b1938131c61ad25aa68bbe72` records exact ancestry through
`5c27f2afb55bdfe5ccf1f14e84c6251a1292b4a0` without importing deferred hunks.

`Deferred` means the proposed naming or behavior may be useful, but it needs a
dedicated ABI/profile review and targeted tests before adoption. None of these
commits fixes an independently demonstrated defect that justifies combining
its broad symbol migration with the upstream checkpoint.

## Commit-by-commit disposition

| # | Upstream commit | Disposition | Evidence and retained boundary |
|---:|---|---|---|
| 31 | `14900b6b` `refactor(database): name DB zone flags (#94)` | Deferred | Replaces raw zone-mask values across database, client, server, renderer, and command paths. This fork has a substantially hardened registry and zone-lifecycle surface; adopt only with exact flag-value, caller, and archive-lifecycle contracts. |
| 32 | `37947908` `refactor: use gfx aspect ratio enum (#95)` | Deferred | A small naming-only migration, but it changes a shared renderer dvar type. It offers no behavior fix and should land independently with value/layout assertions. |
| 33 | `90b7a52a` `refactor: use perks enum (#96)` | Deferred | Moves perk identifiers into a shared enum and rewrites script/server consumers. Perk indices cross gameplay and script boundaries; require exact numeric and profile-specific coverage. |
| 34 | `2f4eb59e` `refactor: name player state stat indices (#97)` | Deferred | Replaces numeric stat indices in 27 network, prediction, message, and gameplay files. The values are protocol-visible and need MP/SP serialization parity tests before migration. |
| 35 | `2a0747b7` `refactor: name damage flags (#98)` | Deferred | Renames gameplay damage-mask bits across SP and MP. Preserve exact bit values and mixed-mask behavior through focused damage tests before adoption. |
| 36 | `8b23c310` `refactor: name means of death values (#99)` | Deferred | Broadly renames means-of-death values crossing scripts, gameplay, and network state. It is behavior-neutral only if every numeric value remains exact; no such local oracle accompanies the upstream change. |
| 37 | `0f139a7f` `refactor: name weapon states (#100)` | Deferred | Rewrites a large weapon-state surface across input, prediction, gameplay, and HUD code. This fork's SP/MP layouts and native-width work require a dedicated state-value audit. |
| 38 | `c0c19174` `refactor: name turret flags (#101)` | Deferred | Replaces turret bit constants across actor, script, SP, and MP paths. Needs exact bitmask and save/network contract checks rather than inclusion in an ancestry-only batch. |
| 39 | `441e1e44` `refactor: name entity types (#102)` | Deferred | Entity type IDs are savegame and snapshot visible. The 16-file replacement needs explicit frozen-value and serialization evidence. |
| 40 | `25fa0e8a` `refactor: name usercmd button flags (#103)` | Deferred | Migrates input bits across 23 client, movement, gameplay, and message files. User-command bits are network protocol, so raw textual equivalence is insufficient. |
| 41 | `355b68a0` `refactor: name collision contents and surface flags (#104)` | Deferred | This is not naming-only: it consolidates MP/SP masks and changes a helicopter water-surface comparison, while touching 63 files. It overlaps this fork's separately sealed profile masks and needs dedicated collision/gameplay parity review. |
| 42 | `df46aa63` `refactor: name vehicle manual modes (#106)` | Deferred | Moves vehicle mode numbers into a shared enum across script and MP/SP gameplay. Require frozen script values and both-profile tests. |
| 43 | `008cfc53` `refactor: name helicopter damage stages (#107)` | Deferred | Introduces shared names for MP helicopter damage stages. The network/game enum values need explicit compatibility assertions first. |
| 44 | `b77b84fb` `refactor: name vehicle sound slots (#108)` | Deferred | Replaces vehicle sound indices in script and gameplay code. Defer until exact asset/script slot contracts cover the migration. |
| 45 | `c2543fd2` `refactor: name csp field types (#109)` | Deferred | Includes roughly 1,000 lines of loader-table churn plus shared field-type changes. It intersects fast-file parsing and must be reviewed against the fork's bounded loader and Disk32 work. |
| 46 | `5c27f2af` `refactor: name weapon animation values (#110)` | Deferred | Adds and propagates weapon-animation names through animation, event, and weapon paths. Adopt only with frozen numeric values and MP/SP animation parity coverage. |

## Ancestry evidence

Checkpoint `86459c535ee78043b1938131c61ad25aa68bbe72` has:

- first parent `f2408924520ae2a331b9e8ae9170be1256c15b4f`;
- second parent, exact upstream tip
  `5c27f2afb55bdfe5ccf1f14e84c6251a1292b4a0`; and
- tree `898ed947915351b95c58c0ed49f27ab56561ae26`, exactly matching
  its first parent's tree.

The checkpoint's complete content diff from its first parent is empty. The
curated security changes through `c6be07a2`, including explicit rejection of
truncated or negative reliable acknowledgements, remain in the first-parent
tree. Local and hosted validation are recorded on PR #105.
