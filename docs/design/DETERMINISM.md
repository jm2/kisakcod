# Determinism

WS-3 · Gate G2 (wire items: G4) · Related: [NATIVE64.md](NATIVE64.md), [NET_STEAM18.md](NET_STEAM18.md), [ROADMAP](../ROADMAP.md)

Native builds must put the same bytes on the wire as Windows x86, and must simulate closely enough that Steam 1.8 peers can't tell the difference.

## RNG: engine-owned, MSVC-compatible

The code assumes MSVC's `rand` (`RAND_MAX` 32767). glibc and macOS use 2^31−1.

| Site | Today | Off Windows |
|---|---|---|
| `random`, `crandom` (`com_math.cpp`) | `rand() / 32768.0` | Range far above 0–1 |
| `G_flrand`, `G_random` (`g_utils_mp.cpp`) | `G_rand()` / 32768 | Same |
| `G_irand` (`g_utils_mp.cpp`) | `(max - min) * G_rand() / 0x8000` | Wrong range; the product overflows `int` |
| Other `rand`/`srand` calls (client, server, cgame, bgame, FX, DynEntity, UI) | CRT | A different sequence on each platform |

Rule (bead 9): one engine RNG sits behind every engine `rand` and `srand` call:

```c
holdrand = holdrand * 214013 + 2531011;   /* uint32_t */
return (holdrand >> 16) & 0x7FFF;
```

- Seeding is the same as `srand`. MSVC keeps the state per thread; the engine RNG uses `thread_local` to match.
- Vendored Speex keeps its own `rand`.
- `flrand`/`Rand_Init` in `com_math.cpp` already run a separate portable LCG (`>> 17`). It is deterministic, but it isn't MSVC `rand`; leave it alone.

## Floating point

| Rule | Why |
|---|---|
| clang/GCC `-ffp-contract=off`; MSVC `/fp:precise` without `/fp:contract` (check ARM64 codegen) | FMA contraction on arm64 changes results |
| No fast-math anywhere | Reassociation breaks the bit-exact paths |
| Scalar SSE2 on x86 | MSVC default; x87 excess precision isn't reproducible |
| libm transcendentals (`sinf`, `atan2f`, …) differ per platform | Covered by the tolerance rule below |

None of these flags is set today. The bead that adds 64-bit engine presets adds them.

Retail used x87 (`fld`/`fistp`, kept under `KISAK_PURE`). SSE2 differs from x87 only in the low bits of intermediates. That's acceptable because simulation is compared with a tolerance and the wire-visible conversion rounds the same way on both.

### Snapped floats are wire-visible

`SnapFloatToInt` (`qcommon.h`) snaps origins and angles for the wire. It rounds half-to-even:

- x86/x64: `_mm_cvtss_si32`, MXCSR default rounding.
- Elsewhere: `std::nearbyintf` under `FE_TONEAREST`. Out-of-range values and NaN return `INT_MIN`, which is SSE's "integer indefinite".

Nothing may change the FP rounding mode at runtime.

## Comparison policy

| Subject | Comparison |
|---|---|
| Encoders/decoders: `MSG_*` bit I/O, Huffman, netfield deltas, netchan fragments, gamestate/snapshots, `SnapFloatToInt` | Byte-exact against Windows x86 and Steam 1.8 captures |
| Simulation: Pmove, physics, animation, FX | Per-field tolerance; integer and snapped fields exact |
| Voice | Decoder interop with Steam 1.8 peers ([CLIENT.md](CLIENT.md)), not encoder bytes |

The prediction check in [NET_STEAM18.md](NET_STEAM18.md) uses the simulation rule.

## Open item: Huffman tie-break (needs a bead)

`Huff_BuildFromData` (`huffman.cpp`) sorts with the host `qsort`, and `nodeCmp` compares weights only. `msg_hData` repeats two weights (symbols 155/205 and 228/231), and internal nodes can tie as well. The C library chooses the order of equal elements, so the code book depends on the host. glibc matches the reference; MSVC-ARM64 and macOS are unverified. `huffman_wire_contract_tests` prints a note and *passes* when a host disagrees.

Bead scope:

1. Make `nodeCmp` a total order (e.g. break ties on node index) that reproduces the reference code book on any `qsort`.
2. Make the test fail on any host whose code book differs.
3. Confirm the reference against Steam 1.8 captures at G4a.
