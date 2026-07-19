# KisakCOD Codebase Audit — Security / Logic / Functionality / Build

**Basis:** whole-tree analysis of `master`. Security findings were run through an adversarial
verification pass (each finding a reviewer *tried to refute*); the eight top security items below all
**survived and were confirmed at high confidence**. Line numbers are as of the analyzed tree.

**Counts:** 3 critical · 10 high · 15 medium · 19 low (47 total, after noting duplicates) — this is
the *original* inventory, not a live count of open items.

> **How to read this document.** This is a point-in-time audit, not a live tracker. Many findings
> below have since been remediated, and the finding bodies deliberately preserve the original defect
> description and line numbers so the history stays legible. **Do not treat a finding here as open
> work.** Check the remediation status below and the inline `**Status:**` markers.
>
> `docs/task.md` is the live checkpoint and tracks current status far more closely than this file —
> but it may lag the implementation. **When the
> docs disagree with the tree, the tree wins.** Confirm against the source and `git log` before acting
> on any doc. Several line references in this file are stale by construction.

---

## Remediation status (July 18, 2026)

Fixed in the initial porting implementation:

- C1/C3: Huffman decoding now has explicit input/output bounds; malformed or
  oversized messages are rejected by both client and server.
- C2: download blocks are range-checked before the 2 KB copy.
- H1: stats packets accept only indices 0–6 and use unconditional range checks.
- H5/H6/H7: CMake no longer shadows `WIN32`, platform selection is explicit,
  unsupported targets fail clearly, and CI uses the actual DirectX NuGet layout.
- Fast-file offset fixups now validate block indices and byte spans before
  dereferencing zone memory.
- A dependency-free Windows x86 headless dedicated profile now compiles and
  links in CI with null GPU/audio asset realization; the protected licensed
  map-load/network smoke remains the release gate.
- Native time, synchronization, event, thread-lifecycle, scheduling, and
  cooperative worker-gate services now run on all five portable utility targets.
- Merged referenced-file hardening makes SYSTEMINFO/IWD producers
  bounded and failure-atomic, validates remote metadata before mutation, and
  restricts in-band server downloads to exact published active-mod `.iwd`/`.ff`
  names with protocol-length, server-only, path-namespace, pre-open
  revalidation, and resource-cleanup guards. HTTP/www redirect transport remains
  nonfunctional and is still tracked under H4.
- PR #55, merged as `f39e0e4a` from final implementation head `e9051955`, extends the script-string ownership foundation:
  opaque `RefString` mutation
  authority is private, character folding is defined for every byte value, complete batch admission validates debug
  ownership per live ID, and all nested allocator-lease entries require an exact non-forgeable capability. Both
  namespace-visible friend authority shims are removed; capability storage is constant-initialized and trivially
  destructible, with no guarded first use or shutdown registration. Exact-head run **29657884407** passed all nine jobs
  and exact-head hosted Codex review was clean. The merged batch remains production-neutral; lifecycle and enrollment
  status belongs in `docs/task.md`.
- Fixed-width atomic migrations cover dvar/script/XAnim/DObj/database/IWD/network,
  skeleton/pose, worker queues, bounded renderer reservations, and DObj/model-surface streams. The live
  debt ledger and current validation evidence are maintained in `docs/task.md`.

The macro-off `EffectsCore/fx_physics_sidecar.h` test-friend authority leak is fixed by merged PR #56. Both
forward/friend declarations are test-macro gated, and a normal positive-build test independently proves an external
same-name definition cannot access either private sidecar field. GCC/Clang Release pass **118/118** tests, strict
i386/AArch64 compilation is clean, and the same test fails both assertions against the old friend-bearing baseline.
Exact final head `c2613282` passed all nine hosted jobs in run **29658932268**, exact-head Codex review was clean, and
PR #56 squash-merged as `6159275e`; authoritative post-merge master run **29659347033** also passed all nine jobs.
PR #57 then sealed mutable runtime-table authority behind ten exact-key pre/post-authenticated adapters. The public table
still exposes no mutable entry, terminal statuses use an explicit allowlist, and a macro-off positive seal denies all five
private capabilities to a recreated test-helper name. Exact-head run **29659895814** and post-merge run **29660281653**
passed all nine jobs; exact-head Codex review was clean, and it squash-merged as `57e2b1a2` without production enrollment.

Merged PR #58 removes duplicated Win32 standard-handle writes and introduces allocation-free bounded
line input plus Win32/POSIX backends. Its hardening specifically contains POSIX `SIGPIPE` per calling thread without a
process-global disposition change, avoids blocking fatal output on Win32 pipe flush, preserves message-mode pipe bytes,
and drains embedded-NUL/overlong commands across a fixed work budget. Native Win32 character-console input remains
unsupported in the headless profile; redirected disk/pipe input works and the limitation is tracked in `docs/task.md`.
Exact-head run **29666269398** passed all nine jobs, exact-head Codex review was clean, the real nullable fatal-message
finding was fixed, and the batch squash-merged as `9fb46bea`. Authoritative post-merge run **29670244884** also passed
all nine jobs at that exact merge commit.

Merged PR #59 replaces an implicit future layout with one
canonical, checked 32-bit-offset slab plan for the script-string journal/entries, native FX arena, Disk32 adapter
workspace, and aligned arena backing. Its stable noncopyable handle authenticates the exact plan and pointers outside the
slab, prevalidates alignment/capacity/address overlap before placement writes, and permits teardown only after the
journal, adapter, and arena reach exact safe terminal states. A narrow FX bridge and read-only destruction predicates do
not expose mutation or generation authority. The exact replay passes GCC Release **131/131**, GCC i386 runtime,
AArch64 GCC strict syntax for all batch translation units and the fixture, and Clang ASan+UBSan runtime with leak
detection disabled because LSan is unavailable under ptrace; headless/debt, ABI, source, and diff gates pass. Exact head
`8cec770d` passed all nine jobs in run **29671392540**, and exact-head Codex, Gemini, and independent audits were clean.
This production-neutral resource foundation squash-merged as `ff61504e`: no loader/lifecycle caller consumes it, it does
not claim a runtime-table generation or PMem scope, and retail bytes are unchanged. Authoritative post-merge run
**29671849514** at exact `ff61504e` passed all nine jobs.

The active audited checked-PMem foundation at rebased head `6c05a372` adds report-free `TryBegin`, `TryEnd`, and
`TryFree` over exact typed allocation entries. It authenticates both complete 32-entry prim topologies before mutation,
including the low-prim base, monotonic low/high positions, legitimate middle holes, and typed tail collapse. Its
stable-address authority is noncopyable, nonmovable, nontrivial, self-authenticating, single-use, and carries a phase
witness. The contract requires external serialization, mutually disjoint control/receipt/managed-backing storage, stable
name identity, and no legacy bypass, entry replacement, or reinitialization while the scope is owned. The exact replay on
`ff61504e` passes GCC Release **134/134**; focused GCC/Clang, Clang ASan+UBSan, Clang static analysis, strict native i386
and AArch64, source, API-seal, security, and diff evidence all pass. It has no production caller and leaves retail
bytes unchanged. Legacy native64-invalid `PMem_FreeIndex`/`PMem_EndAllocInPrim` handling and the `$init` lifecycle remain
blockers before production enrollment.
Still open: the broader release-disabled assertion audit (H2), reflection/rate
limiting, HTTP downloads, dependency replacement/upgrades, protected headless
runtime smoke, the porting-era findings below, and the remaining medium/low findings.

The first H2 parser conversions are complete: invalid entity references,
24-bit flag indices, last-changed field indices, and HUD field counts now set
message overflow unconditionally instead of relying on release-disabled
assertions. Huffman compression output is also capacity-checked, and internal
tree nodes no longer write past the `loc[257]` table during construction.

---

## 0. Current state

KisakCOD is a **Hex-Rays decompile-and-reconstruct** of the retail COD4 (2007) multiplayer `.exe`,
~632k LOC, **32-bit x86 / MSVC / DirectX 9 / Windows only**. It is a *buildable, MP-first, playable*
reimplementation — **not feature-complete**. Maturity markers:

- The renderer (`gfx_d3d`, ~96k LOC) is a near-1:1 decompile; 95 files still carry `[esp+..]`/`[ebp-..]`
  register-slot comments, and ~249 `static_assert(sizeof==N)` freeze the 32-bit struct layout.
- SP (single-player) lags MP: save/load uses admittedly-wrong sizing (`g_save.cpp:2488`), AI/gameplay
  has unresolved flag-decode gaps (`g_active.cpp:241`), LiveStorage stat writing is broken
  (`win_storage.cpp:634`).
- Anti-cheat (PunkBuster) is removed, replaced by rudimentary Steam-ticket auth
  (`cl_main_mp.cpp:1051`).
- The README's own warning — *"~20 year old game with some known exploits … non-zero chance of binary
  exploitation … use a sandbox"* — is accurate. The findings below make it concrete.

---

## 1. CRITICAL — remotely exploitable memory corruption (fix before anything else)

These are reachable from the network by an ordinary peer, corrupt memory with attacker-controlled
bytes, and are one-line-to-a-few-lines to fix. They are independent of the port.

### C1. Server: Huffman decompression overflows a 2 KB global (client → server RCE)
`src/server_mp/sv_client_mp.cpp:1583` (buffer declared `:1575`)

**Status: fixed.** The decoder is bounded by compressed bits and destination
capacity, the server buffer is `MAX_MSGLEN`-sized, and overflow drops the client.

`SV_ExecuteClientMessage()` decompresses attacker-controlled packet data into a fixed
`uint8_t msgCompressed_buf_0[2048]`:
```cpp
MSG_Init(&msgCompressed, msgCompressed_buf_0, 2048);
msgCompressed.cursize = MSG_ReadBitsCompress(&msg->data[msg->readcount],
                                             msgCompressed_buf_0,
                                             msg->cursize - msg->readcount);
```
`MSG_ReadBitsCompress()` (`src/qcommon/msg_mp.cpp:239`) takes only the **input** size and has **no
output bound** — its loop `while (bit < bits)` writes one output byte per decoded symbol
(`*data++ = get;`). Because frequent Huffman symbols are only a few bits, output can be many times the
input. The incoming `msg` lives in `serverCommonMsgBuf[0x20000]` (`common.cpp:942`), so
`msg->cursize - msg->readcount` can be tens of KB. Unlike the client path, there is **no input-size
check at all** here, and the destination is only 2048 bytes.

**Impact:** a connected (or qport/address-matching) client sends one crafted UDP packet; the
decompressed stream walks past `msgCompressed_buf_0` into adjacent BSS with attacker-influenced bytes →
server crash / likely RCE. Even a ~300-byte payload of the shortest symbol exceeds 2048 output bytes.
This is the classic idTech3/CoD Huffman-decompress overflow, here with a *smaller* buffer and *no*
guard.

**Fix:** give `MSG_ReadBitsCompress` an explicit output-buffer-size parameter and stop (set
`overflowed`) when output reaches it; size `msgCompressed_buf_0` to `MAX_MSGLEN` (0x20000) like the
rest of the code; reject client messages whose decompressed length would exceed `MAX_MSGLEN`. Also add
a bounds check inside `get_bit`/`Huff_offsetReceive` (`huffman.cpp:7-35`) against the input length.

### C2. Client: `CL_ParseDownload` copies attacker length into a 2 KB global (server → client RCE)
`src/client_mp/cl_parse_mp.cpp:404` (buffer `char parseDownloadData[2048]` declared `:365`)

**Status: fixed.** Negative and oversized blocks now produce `ERR_DROP`.

```cpp
size = MSG_ReadShort(msg);            // sign-extended __int16 -> up to 32767
if (size > 0)
    MSG_ReadData(msg, (uint8_t *)parseDownloadData, size);
```
There is **no `size <= sizeof(parseDownloadData)` check** — upstream Quake3/ioquake3 has exactly this
guard (`if (size < 0 || size > sizeof(data)) Com_Error(...)`) and it is missing. `MSG_ReadData`
(`msg_mp.cpp:523`) writes `size` bytes in all paths (including `memset(data,0xFF,len)` on the
short-packet path), so the overflow happens regardless of how much real data is present. The client
message buffer is 128 KB, so real attacker bytes can be supplied.

**Impact:** a malicious/MITM server sends an `svc_download` block with `size` 2049..32767 and overflows
the client global by up to ~30 KB of chosen bytes → client RCE/crash, triggered during a normal
map/mod download when joining a hostile server.

**Fix:**
```cpp
if (size < 0 || size > (int)sizeof(parseDownloadData)) {
    Com_Error(ERR_DROP, "CL_ParseDownload: invalid block size %d", size);
    return;
}
```

### C3. Client: `CL_ParseServerMessage` Huffman decompression can exceed the 128 KB buffer
`src/client_mp/cl_parse_mp.cpp:485` (buffer `msgCompressed_buf[0x20000]` declared `:464`)

**Status: fixed.** The same bounded decoder rejects output larger than the
message buffer.

```cpp
if ((uint32_t)(msg->cursize - msg->readcount) > sizeof(msgCompressed_buf))
    Com_Error(ERR_DROP, "Compressed msg overflow in CL_ParseServerMessage");
msgCompressed.cursize = MSG_ReadBitsCompress(&msg->data[msg->readcount], msgCompressed_buf,
                                             msg->cursize - msg->readcount);
```
The guard bounds the **input** to 0x20000, but the buffer is *also* 0x20000 and the decompressor
expands ~3–4×. A ~64 KB unfragmented UDP datagram of minimal-length symbols decodes to ~190 KB into a
128 KB buffer.

**Impact:** malicious server → client overflow/RCE. Lower likelihood than C2 (needs a large packet),
same corruption primitive. **Fix:** bound by **output** bytes written, not input bits consumed — pass
`sizeof(msgCompressed_buf)` into `MSG_ReadBitsCompress` and stop/flag on reaching it. (This is the
same root cause as C1; fix `MSG_ReadBitsCompress` once and both call sites are covered.)

---

## 2. HIGH

### H1. `SV_ReceiveStats` accepts `packetNum == 7` → OOB write + ~4 GB memcpy
`src/server_mp/sv_client_mp.cpp:304`

**Status: fixed.** Packet 7 is rejected and range validation is unconditional.

Reads `packetNum = MSG_ReadByte(msg)` and gates on `packetNum < 8`, but the valid range is 0..6
(`MAX_STATPACKETS == 7`, `stats[8192]`, chunk 1240). For `packetNum == 7`: `start = 1240*7 = 8680`
(already past the 8192-byte array) and remaining `= 0x2000 - 8680 = -488`. The only guards for
`size <= 0` / `start+size > 0x2000` are `MyAssertHandler()` calls (`:313,:317`) — **no-ops in
release/dedicated builds** (see H3). Execution reaches `MSG_ReadData(msg, &cl->stats[8680], -488)`;
the negative length is used as `size_t` in `memcpy` (`msg_mp.cpp:552`) → ~4 GB copy.

**Impact:** a client holding a netchan slot (reachable during the connect handshake via the
connectionless `stats` packet, `SV_ConnectionlessPacket:724`) guarantees a remote server crash and an
OOB write. **Fix:** gate `packetNum < MAX_STATPACKETS` (< 7) and replace the assert guards with real
runtime checks that drop the packet.

### H2. Security-relevant validation relies on asserts that are compiled out in shipped builds
`src/universal/assertive.h:26`

`USE_ASSERTS` is defined only under `_DEBUG` or `RELEASE_ASSERTS` (`assertive.h:3-5`). The MP target
(`scripts/mp/CMakeLists.txt:60`) and dedi target (`scripts/dedi/CMakeLists.txt:59`) define **neither**,
and `KISAK_PURE` is unset — so `iassert/vassert/bcassert` expand to nothing and `MyAssertHandler()` is
an empty body (`assertive.cpp:685`). Numerous **network-input** validations depend solely on these:
`MSG_ReadEntityIndex` `lastEntityRef >= 0` (`msg_mp.cpp:912`), `MSG_ReadLastChangedField`
`lastChanged <= totalFields` (`:1373`), `MSG_Read24BitFlag` `bitChanged <= 24` (`:1230`), and the
`SV_ReceiveStats` guards (H1).

**Impact:** conditions the original devs treated as "can't happen" are silently skipped in production,
turning trusted-length/index parser assumptions into exploitable OOB reads/writes under hostile input.
**Fix:** audit every `MyAssertHandler`/`iassert` used on network-derived lengths/indices and convert
the security-relevant ones to unconditional runtime checks present in all build configs. *(This is the
systemic root cause behind H1 and several medium items — highest-leverage single fix.)*

### H3. Master/auth server hardcoded to defunct `cod4master.activision.com`
`src/qcommon/common.cpp:1539` (`:1541,:1546`)

`com_masterServerName`/`com_authServerName` default to Activision's long-dead master. The internet
browser's `getservers` (`cl_main_pc_mp.cpp:447`) goes nowhere. Both are `DVAR_CHEAT`, so nothing points
them at a live replacement out of the box.

**Impact:** the public server browser returns nothing; discovery works only via LAN / Steam / manual
direct-connect. **Fix:** default to a maintained community master (dpmaster-style) or route discovery
through Steam.

### H4. HTTP/www-redirect download subsystem is fully stubbed
`src/qcommon/dl_main.cpp:111`

`DL_BeginDownload` hardcodes `return 0` (real body under `#if 0`), `DL_InitDownload`/`DL_DownloadLoop`/
`DL_InProgress`/`DL_CancelDownload` are stubs. It's still wired into connect: a server redirect calls
`DL_BeginDownload` (`cl_parse_mp.cpp:284`), which always fails → `wwwdl fail`.

**Impact:** clients cannot www-download missing custom maps/mods; joining modded servers without
pre-installed content fails at the download step. **Fix:** reimplement against libcurl/WinHTTP (the
original libwww is gone), or document that only in-band UDP downloads + pre-installed content work.

### H5. `set(WIN32 …)` clobbers CMake's built-in `WIN32`, defeating every `if(WIN32)` guard
`scripts/common_files.cmake:558`

**Status: fixed.** The variable is now `PLATFORM_WIN32` (`common_files.cmake:587`); the cited line
number is stale and now points at an unrelated source entry.

*Original finding, preserved:* The source-file list is named `WIN32`, shadowing CMake's reserved boolean. `common_files.cmake` is
included before `pre_build.cmake`, whose entire DirectX/SDK/link block is wrapped in `if(WIN32)`
(`:2,:28`). Since `WIN32` now holds a non-empty list, **`if(WIN32)` is true on all platforms** — the
Windows-only path runs unconditionally, and on Windows it works only by accident.

**Impact:** silently breaks any future non-Windows build; a foundational bug for the port. **Fix:**
rename to `WIN32_SRC` (update `source_groups.cmake` and the three `add_executable` lists). Never name a
CMake var `WIN32/UNIX/APPLE/MSVC`.

### H6. No functional non-Windows build; linux `platform.cmake` is empty and `KISAK_PLATFORM` is hardcoded
`scripts/platform/linux/platform.cmake:1`, `CMakeLists.txt:10`

**Status: fixed (scaffolding).** `KISAK_PLATFORM` is now detected from `CMAKE_SYSTEM_NAME`
(`CMakeLists.txt:40`), `scripts/platform/linux/platform.cmake` is populated, and `src/_platform/`
exists with POSIX backends. Note the *engine* is still deliberately gated off on non-Windows by an
explicit `FATAL_ERROR` (`CMakeLists.txt:50-57`) — that gate is intended, not a defect.

*Original finding, preserved:* `set(KISAK_PLATFORM win32)` is hardcoded; `win32/platform.cmake` `FATAL_ERROR`s otherwise; the linux
file is 0 bytes; `src/_platform/` (the override root) doesn't exist. The README's "fully-buildable" is
Windows/MSVC/DX9-only in practice.

**Impact:** zero Linux/macOS build capability behind scaffolding that implies otherwise. **Fix:** detect
from `CMAKE_SYSTEM_NAME`, populate or delete the linux stub, gate DirectX behind the platform.

### H7. CI Debug matrix job points at a nonexistent DXSDK lib path
`scripts/pre_build.cmake:45`

**Status: fixed.** `pre_build.cmake:47` now uses `${DXSDK_DIR}/release/lib/x86` with `d3dx9.lib` for
both configurations; both Debug and Release engine jobs link green.

*Original finding, preserved:* CI extracts `build\native\release\lib\x86` into `GITHUB_ENV` but never passes it; `pre_build.cmake:45`
independently derives `${DXSDK_DIR}/${CMAKE_BUILD_TYPE}/lib/x86` → `build/native/Debug/lib/x86` for the
Debug entry, and selects `d3dx9d.lib` (`:55`). The NuGet package ships only a lowercase `release` lib
dir and **no** debug import lib.

**Impact:** the Debug half of CI cannot link; the workflow's `libDir`/`incDir` extraction is dead code
masking that the real logic lives in `pre_build.cmake`. **Fix:** point `DXSDK_LIB_DIR` at the actual
NuGet layout regardless of config; link the release `d3dx9.lib` for both configs; drop the dead env
extraction.

*(H-level duplicates C1–C3 from a second reviewer are folded in above.)*

---

## 3. MEDIUM (selected)

- **Spoofed reflection/amplification DoS** — unauthenticated `getstatus`/`getinfo`
  (`sv_main_mp.cpp:691`) reply to a spoofed source with a larger response. Add per-source rate limiting
  / challenge, cap response size.
- **Dedicated server is not headless** — **Status: fixed.** A dependency-free `KISAK_DEDI_HEADLESS`
  profile now compiles and links as its own green CI job. `${EFFECTSCORE}` (`dedi_sources.cmake:41`)
  and `${GFX_D3D}` (`:49`) are added only in the non-headless `else()` branch (`:38-58`), and
  `kisakcod_assert_headless_dedi_sources` (`:63-77`) raises `FATAL_ERROR` (`:71`) if any
  client/media source — or a Bink/Miles dependency (`:74`) — leaks back into the headless list.
  *Original:* `dedi` links the full client incl. D3D9 + Miles (`scripts/dedi/CMakeLists.txt:40`);
  wastes deps and blocks a clean Linux server port.
- **VS multi-config vs `CMAKE_BUILD_TYPE`** — lib selection and DLL copy key off `CMAKE_BUILD_TYPE`
  under a multi-config generator (`post_build.cmake:6`); should use `$<CONFIG>` /
  `$<TARGET_FILE_DIR:..>`. (`build-win.ps1` already passes `--config` to compensate.)
- **`/we4700` warnings-as-error applied globally** to vendored third-party code
  (`platform/win32/platform.cmake:18`). **Status: fixed** — `/we4700` no longer appears in `scripts/`.
- **`target_compile_definitions(KISAK_EXTENDED)` missing scope keyword** — enabling the feature flag
  breaks configure (`pre_build.cmake:18`). **Status: fixed** — now `PUBLIC` (`pre_build.cmake:12`).
- **SP correctness debt** — save/load wrong array sizing/typing (`g_save.cpp:2488`); AI flag-decode
  gaps (`g_active.cpp:241`); broken LiveStorage stat writing (`win_storage.cpp:634`); many decompiled
  hot paths flagged unverified (union fields, arg counts, casts — `scr_evaluate.cpp:945`).
  **Status: deferred by scope.** Single-player is out of scope for the port (MP client + headless
  dedicated only) and is built by **no** CI job (`-DKISAK_BUILD_SP=OFF` everywhere), so the ~97
  SP-only translation units are not compiled or syntax-checked. Treat SP findings as unvalidated.
- **Mic mixer setup fails** → voice capture unconfigured (`win_voice.cpp:124`).
- **Hardcoded absolute dev paths** in `milesEq/build.bat:5`.
- **Dev-experience/CI gaps** — **Status: fixed.** CI is now 9 jobs (5 portable-test targets + 4
  Windows x86 engine builds) across `ci.yml`/`release.yml`, with 32 ctest tests and 14-day artifact
  retention. *Original:* single Windows job, no tests/lint/format, stale README build steps,
  throwaway 1-day artifacts (`.github/workflows/build-kisarcod-win.yaml:78` — that workflow file no
  longer exists).

## 4. LOW (selected)

- `muteplayer`/`unmuteplayer` off-by-one OOB write into `client_t` (`ucmds.cpp:33`).
- `rcon` uses non-constant-time password compare + single global throttle
  (`sv_main_pc_mp.cpp:260`) — timing/brute-force exposure.
- `Info_NextPair` copies key/value with no destination bounds (latent overflow) (`q_shared.cpp:627`).
- Team-chat color guarded by an always-false pointer comparison (`cg_draw_mp.cpp:292`).
- Control-detail buffers leaked on voice mixer error paths (`win_voice.cpp:149`).
- Gamepad/rumble disabled (`cl_main.cpp:655`); `R_ResolveSection` unimplemented (`rb_backend.cpp:907`).
- Auto-config fatally aborts on unrecognized CPU/GPU (`com_playerprofile.cpp:546`).
- zlib pinned to 1.1.4 (2002) with known post-1.1.4 CVEs — upgrade to 1.3.1 (DEFLATE output unchanged,
  so `.iwd`/`.ff` compat preserved).
- Build hygiene: dead `/intentionallbreakthisshit` flag, undefined `MSVC_WARNING_DISABLES`, ASAN wiring
  only in MP, redundant `CMAKE_GENERATOR_PLATFORM` after `project()`, build-number increment path dead,
  `.gitignore` misses `build-Debug/`-style dirs, Tracy duplicated across subprojects.

---

## 5. Porting-era renderer/concurrency findings (July 12, 2026)

These findings were exposed while replacing Windows atomics and widening runtime
layouts. They are additional to the original 47-item count above.

### P1. Renderer worker queue race and 64-bit truncation are repaired

The worker-queue batch removes all 25 native calls and the lossy minimum-type hint,
using a deterministic 17-type scan plus short fixed-width producer/consumer guards.
This also closes a wrapped read-cursor ABA that could copy and execute a stale slot
twice. One full-lifetime outstanding count covers queued, executing, recursive, and
inline-fallback work; impossible transitions fail closed. All 17 payload descriptors
derive native size/count from typed buffers, dequeue storage is aligned and bounded,
and typed shadow-cookie/DPVS/model dispatch preserves 64-bit pointers. Eight-producer/
eight-consumer exact-once wrap stress runs under TSan. Residual debt: the shared
manual event retains a 1 ms bounded poll and renderer workers/events remain process-
lifetime objects without teardown or safe reinitialization. Full-ring inline fallback
can overtake older same-type queued work, and handler longjmp bypasses ordinary
completion accounting; both need runtime/error-unwind fixtures before behavioral change.

### P2. DObj/model-surface stack, arena, and asset hazards are repaired

The fixed 3,600-byte stack overlay is replaced by a two-pass checked plan that
constructs aligned native 4/8, 24/40, and 56/72-byte records directly into an
owned scene-arena slice. Exact bounded reservations cover scene and output vertex
bytes; second-pass totals, bone spans, selected LODs, materials, surface identity,
and required part bits must match before publication. Worker and scene parsers
validate exact framing, owner frame, published cursors, contiguous outputs, and
record semantics before use. Fast-file completion now rejects malformed skin
buckets/weights, rigid coverage, XModel skeleton parents/classifications/LODs,
and collision counts/spans/bones before asset publication. Static-cache bases/capacity,
finite/canonical vertex/base-pose/bone/model data, and aggregate collision contents are
also checked. Runtime DObj accessors repeat safety-critical model/count/parent/capacity
checks for non-fast-file paths. DObj create, clone, and unarchive now prepare fallible
allocation/string work before pool reservation or object locking and publish through an
assignment-only transaction, preventing recoverable `Com_Error` longjmps from stranding
locks or allocated-but-unmapped pool slots.
Residuals: reserving the independent surface slice before a later failed vertex
reservation wastes bounded capacity until the next frame, static XModel `surfId`
consumers still need the shared arena-bound accessor, and load-object model readers
remain based on unbounded, potentially unaligned `Buf_Read<T>` operations.

### P3. FX packed status/free lists/visibility require protocol rewrites

**Status: fixed.** Landed across PRs #2–#4 (`036ddaf8`, `facbfb12`, `3c542f20`). `EffectsCore` now has
**zero** native atomic calls and no `volatile long`/`LONG` in any FX struct; the nineteen atomic words
are explicit `int32_t` in `EffectsCore/fx_runtime.h` with dual 32/64-bit size contracts. The packed
status field uses bounded CAS helpers that cannot carry into adjacent fields; freelist allocation and
free validate heads, links, strides, counts, and ownership before mutation; the visibility blocker
protocol validates finite packed inputs, uses all 256 slots, and publishes payloads before counts.
The prescription below was followed in essentially this order. **Do not re-do this work** — see
`docs/task.md` for what remains (Disk32 FX archive/fast-file conversion, camera/scalar publication,
and production fixtures).

*Original finding, preserved:* `EffectsCore` retains 61 native atomic calls and 35 load-bearing
`long`/`LONG`
uses. `FxEffect::status` combines a 16-bit refcount, owned-child count, pending and
flag bits, plus an additive bit-29 lock: unchecked add/subtract can carry into
adjacent flags, and enough contenders can corrupt/false-acquire the lock. Pool
head/count assertions continue into out-of-bounds access on corruption. The
visibility blocker writer is off by one, can omit the last record, permits
multiple producers to reserve the same slot, and packs invalid/zero-opacity
floating-point inputs unsafely. The signed effect ring also relies on overflowing
ever-increasing cursors. Land exact-width FX layouts first, then iterator/lifecycle,
worker-independent scalar, pool, camera, visibility, ring, and packed-status
helpers with standalone sanitizer/contention tests; do not blind-substitute the
native calls.

### P4. First renderer reservation family is repaired

The bounded `r_drawsurf` batch replaces split load/exchange and overshooting
fetch-add reservations with a bounded exact-width CAS helper. It rejects invalid
regions/ranges, supports exact capacity, leaves counters unchanged on failure,
and validates published code-mesh record/argument/index spans before backend use.
The DObj scene byte arenas and cull state are now exact-width and bounded as well.
Static-model draw-list provenance, the small remaining scene/backend native calls,
worker lifecycle, and FX families above remain explicit debt.

## 6. Recommended fix order

1. **C1–C3, H1, H2** — the remote RCE/DoS set. Small diffs, port-independent; fixing
   `MSG_ReadBitsCompress` (output bound) + `CL_ParseDownload` (size check) + `SV_ReceiveStats` (range) +
   converting security asserts to runtime checks closes the exploitable surface. Ship first.
2. **H5, H6** — the CMake `WIN32` collision and platform selectability. Prerequisites for the port
   (Phase 0 in `docs/PORTING.md`).
3. **H3, H4** — make multiplayer usable out of the box (server discovery + downloads).
4. **H7 + medium build items** — green, trustworthy CI (add a Linux job once H6 lands).
5. **Medium/low correctness + SP debt** — ongoing; prioritize anything on a network-reachable path.
