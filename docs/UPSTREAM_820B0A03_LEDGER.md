# Upstream reconciliation ledger through `820b0a03`

This ledger records the disposition of every upstream commit after the
previously integrated `4ad0a2e2` checkpoint. Useful behavior was adapted onto
the stronger portable tree rather than merged raw. Tree-neutral checkpoint
`6e0e61076d66a43a388c0a0141f903cd65cffafa` records exact ancestry through
`820b0a030a5a797306f65c3b8fe93767e136a900` without importing deferred,
rejected, or superseded hunks.

Disposition terms:

- **Adapted**: the useful behavior is implemented through a reviewed portable
  boundary on this branch.
- **Deferred**: the change needs a dedicated design, dependency, oracle, or
  cross-platform validation batch before it can be accepted safely.
- **Rejected**: the upstream form is unsafe, unportable, outside the committed
  runtime scope, or weaker than the implementation retained here.

## Commit-by-commit disposition

| # | Upstream commit | Disposition | Evidence and retained boundary |
|---:|---|---|---|
| 1 | `af866142` `refactor 'q_shared.h' as the 1st .h file in every .cpp file (non-library)` | Selectively adapted; remainder rejected | Retained checked pre-tess index packing, overflow-safe mip/image dimensions, complete-width material state-flag initialization, bounded capsule-contact cleanup, fresh-width animation-tree flags, and checked weapon-model indexing. The 497-file include-order churn and raw narrowing/pointer-decay forms were rejected. Runtime and mutation-sensitive source contracts seal the retained value and model boundaries. |
| 2 | `6ead6eec` `scr_parsetree touchups` | Rejected | The parser/debugger rewrite changes pointer/union ownership and retains pointer-through-integer debugger state. It needs a separate native-width parser ABI design and executable fixtures; no source hunk was imported. |
| 3 | `66b37ca5` `add SSE skinning and backport BLOPS tweaks` | Deferred | The implementation is x86 SSE/MMX and MSVC oriented, adds unrelated Tracy instrumentation, and has no scalar/ARM implementation or golden-output parity oracle. It cannot be the portable renderer path for Windows ARM64, Linux ARM64, or macOS ARM64. |
| 4 | `8f966011` `SP/MP Dvar Split & disableWeapons fix (#86)` | Selectively adapted | SP weapon input now suppresses both firing and melee for the friendly-fire `0x08` and disable-weapons `0x80` flags through one tested helper. MP behavior is unchanged. The broad dvar/input/sound churn remains deferred pending profile-specific production compilation and gameplay parity evidence. |
| 5 | `c182ac29` `Clean up snd` | Selectively adapted | Corrected the looping diagnostic channel and source indices without importing the roughly thousand-line sound rewrite. The larger audio lifecycle change remains deferred to the portable OpenAL Soft backend work. |
| 6 | `aab8b80d` `More assert cleanup.` | Rejected | This is broad mechanical sound assertion churn without a behavioral contract. Existing checked runtime boundaries and assertions are retained. |
| 7 | `3691203c` `snd_driver.cpp assert cleanup` | Rejected | This is another mechanical sound assertion batch without portable backend or runtime evidence. Existing validation remains in place. |
| 8 | `6a85d702` `PM_AddEvent & (Menu & UI) fixes (#87)` | Selectively adapted and hardened | Retained the UI blink intent with floating-point arithmetic plus explicit zero-duration, rewind, and signed timer-wrap handling. Savegame menu/list access now uses the live 256-entry capacity while preserving the frozen 512-entry serialized layout and fails closed on every dynamic index. Broad compass, UI, and movement rewrites were not imported. |
| 9 | `6501b04f` `OpenAL Sound Implementation (#88)` | Deferred epic | The approximately 17,000-line backend/vendor batch needs a dedicated license and supply-chain review, ownership/lifecycle design, fuzzing, and Linux/Windows/macOS plus amd64/ARM64 validation. The audited version retains Win32/x86 assumptions and unchecked truncation, channel, and allocation paths, so none of it was imported here. |
| 10 | `6cf492ad` `More fixes (#90)` | Selectively adapted and hardened | Corrected the explicit SP movement-state layout and linked/dead transitions while compile-time seals preserve the distinct MP layout. Retained the ragdoll secondary-bone correction with optional-index validation and fixed the menu fade duration through the intended integer value. Unrelated fastfile, save, mod, subtitle, and replay changes remain deferred. |
| 11 | `9d75dbdf` `fix physics trace mask SP vs. MP` | Adapted and hardened | Named exact SP `0x280E491` and MP `0x2806C91` masks in the physics-local profile boundary and applied them to all five consumers. Source contracts reject swapped, raw, or missing profile masks. |
| 12 | `9a9c9621` `fix some ragdoll int divisions` | Adapted and hardened | Retained both promoted floating-point divisions in their existing nonzero branches, avoiding the upstream integer truncation while preserving surrounding control flow. |
| 13 | `820b0a03` `kisak radiant` | Selectively adapted; remainder rejected/deferred | Retained only the runtime renderer correction as a typed local `AllowAllStaticModels` callback. The roughly 96,000-line Windows MFC/D3D editor import and its shared-header/API churn are outside the committed MP client/server runtime scope, lack a Linux/macOS/ARM64 design, and require separate provenance, security, dependency, and packaging review. |

## Local validation and ancestry evidence

The curated branch contains no raw merge of the divergent upstream tree. It
passes the complete local GCC Release CTest suite (**208/208**), including
runtime value/UI/weapon-input/weapon-model tests and mutation-sensitive source
contracts. Focused strict Clang, genuine i386, and AArch64 compile/contract
gates for the affected portable helpers also pass.
`git diff --check` is clean.

Checkpoint `6e0e61076d66a43a388c0a0141f903cd65cffafa` has:

- first parent
  `4f14cc0d58cb9149dd979f55266313e310d5e524`;
- second parent, exact upstream tip
  `820b0a030a5a797306f65c3b8fe93767e136a900`; and
- tree `02d3a86b0d76bf20bc6d5bfe154f2500eedc0d3e`, exactly matching
  its first parent's tree.

The checkpoint's complete content diff from its first parent is empty. Hosted
repair head `5f9321f3a464b467f220a7395037da36e0ba7b1a` passed all 11
jobs in run **30383465761**. Exact-head Claude review found no new correctness,
security, or performance issue; CodeRabbit completed successfully; and all 13
earlier review threads were answered and resolved. The subsequent valid
test-harness findings reject empty count needles, pin call-shaped savegame
boundaries, reduce formatting-sensitive CI-name checks, document slice anchors,
and seal the decimal `128` raw-mask spelling; all four affected local source
contracts pass. Final exact-head CI/review, merge-commit retention, and
authoritative post-merge CI remain required before this reconciliation is
complete.
