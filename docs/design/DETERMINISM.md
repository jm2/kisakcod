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
- Elsewhere: `std::nearbyintf` under `FE_TONEAREST`, then an explicit range check on the rounded float before any `int` conversion: NaN and values outside `[-2^31, 2^31-128]` return `INT_MIN` (SSE's "integer indefinite") without an undefined conversion. `net-wire-format-contracts` pins both paths on every portable target.

Nothing may change the FP rounding mode at runtime.

## Comparison policy

| Subject | Comparison |
|---|---|
| Encoders/decoders: `MSG_*` bit I/O, Huffman, netfield deltas, netchan fragments, gamestate/snapshots, `SnapFloatToInt` | Byte-exact against Windows x86 and Steam 1.8 captures |
| Simulation: Pmove, physics, animation, FX | Per-field tolerance; integer and snapped fields exact |
| Voice | Decoder interop with Steam 1.8 peers ([CLIENT.md](CLIENT.md)), not encoder bytes |

The prediction check in [NET_STEAM18.md](NET_STEAM18.md) uses the simulation rule.

## Huffman tie-break

`Huff_BuildFromData` (`huffman.cpp`) sorts with `qsort`, and `msg_hData` repeats two weights (symbols 155/205 and 228/231). Retail compared weights only and took its order of equal elements from the MSVC CRT `qsort`, an unstable median-of-three quicksort. glibc's stable sort swaps the 155/205 codes and macOS's `qsort` swaps 228/231, so Linux and macOS builds emitted non-retail bytes.

The reference is the MSVC CRT order:

- The 1.7 Linux dedicated server calls `ms_qsort`, IW's copy of that `qsort` (disassembly in CoD4x_Server `tools/cod4_dasm`).
- The tree that CoD4x_Server hardcodes has the same 257 codes.
- On the Windows legs, the CRT itself derives the same code book.

`nodeCmp` is now a total order: weight first, then leaves by symbol with 155 and 205 swapped (retail merges 205 first), then internal nodes oldest first. Every `qsort` builds the retail code book. `huffman-wire-format-contracts` pins all 257 codes and byte goldens on the host `qsort`. `huffman-tie-order-independence` rebuilds under a stable sort, a ties-reversed sort and a clone of the CRT `qsort`; on MSVC it also runs the CRT with the weight-only comparator. The weight-only comparator fails both tests on Linux.

Owner action (G4a): confirm the code book against Steam 1.8 captures.
