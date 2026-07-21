# Upstream reconciliation ledger through `4ad0a2e2`

This ledger records the complete upstream delta after the previously integrated
`2164cd1a` checkpoint. The useful content was curated rather than merged raw;
checkpoint `1b1ab3d086d1584bc52fe2438bc1ced60489de63`
records exact ancestry without importing deferred or superseded hunks.

| Upstream commit | Disposition | Retained boundary |
|---|---|---|
| `1c30dda2` memory-tree type names | Selectively adapted | Added exact `int`-backed accounting names and converted built callsites, including the corrected scripted-save category. Public `MT_*`/`SL_*` signatures and hardened allocation/string control flow remain unchanged. Raw MaterialVertexDeclaration sizing, stream-delay assertion-only handling, and assertion-only configstring access were rejected in favor of the existing Disk32/runtime-error boundaries. |
| `601ddcc4` database header-static relocation | Deferred | A wholesale linkage move is unsafe without a dedicated cross-translation-unit audit against this port's substantially changed database boundary. No source hunk was imported. |
| `e3dd4ccb` SP DObj archive loops | Adapted | Save/load loops now use `STATIC_MAX_LOCAL_CLIENTS`, `CLIENT_DOBJ_HANDLE_MAX`, and `MAX_GENTITIES`, preserving existing archive behavior while removing MP-sized literals. |
| `4ad0a2e2` upstream merge | Ancestry only | This merge joins `e3dd4ccb` with already-audited `2164cd1a`; it contains no fourth content batch. |

The checkpoint's first parent is `ea2312392eb1d0119452692d7886aa971f309394`,
its second parent is exact upstream `4ad0a2e2b9d1f40e0e1f79365ff5ad048fdce19c`,
and both the checkpoint and first parent have tree
`14c6575badd6a375d1140782d47bcd6262de7791`. Therefore the checkpoint diff is
empty. The curated source contract passes, including mutation checks, and
`git diff --check` is clean. Hosted exact-head review/CI and merge evidence are
recorded in `docs/task.md` when available.
