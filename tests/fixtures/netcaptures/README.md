# Commercial-reference network captures

Reference evidence for the wire-format certification gate
(`ctest -R net-capture-certification`, issue #127, governed by #122 and
`docs/NETWORK_COMPATIBILITY.md`).

## Doctrine

1. **Fail closed.** Missing or malformed evidence is a blocker to
   certification. The gate never passes on absent evidence and never skips
   silently: it prints every blocker on stderr and exits `77`, which the
   ctest registration (`SKIP_RETURN_CODE`) renders as a named **Not Run**.
   Evidence that is present but divergent is worse: the gate fails hard
   (exit `1`) with the first differing invariant byte and bit.
2. **No fork baselines.** Only captures recorded from the UNMODIFIED
   commercial references may live here (commercial 1.7 and Steam commercial
   1.8, per #122). Bytes produced by KisakCOD or CoD4x are never a baseline;
   CoD4x is explicitly not a substitute for the commercial 1.8 reference.
3. **Explicit variable capture fields.** Every capture declares the spans
   that legitimately vary between sessions (challenge values, sequence
   numbers, qport, ...). Undeclared variables make a capture malformed;
   declared spans are masked on BOTH sides of the compare so invariant
   drift still fails.

## Layout

```
netcaptures/
  commercial-1.7/           # one directory per commercial profile
    MANIFEST.txt
    01-scalar-sequence.bin  # capture files named in the manifest
    ...
  commercial-1.8/
    MANIFEST.txt
    ...
```

Both profiles are REQUIRED. A fresh checkout ships neither; that state is
reported as BLOCKED, which is correct: wire equivalence with the commercial
references is unproven until an operator records the evidence.

## Manifest format

`MANIFEST.txt` is line-based `key = value` (`#` starts a comment):

```
format_version = 1
source_build   = <exact commercial build + how identity was verified>
sanitized_by   = <who removed operator-identifying data, and when>

capture = 01-scalar-sequence.bin
kind    = scalar-sequence
verify  = encode
input   = sequence = 0x1A2B3C4D
input   = acknowledge = 517
var     = sequence@0:4
var     = acknowledge@4:4
notes   = netchan message-front longs
```

Header keys: `format_version` (must be `1`), `source_build` and
`sanitized_by` (provenance, mandatory). Each `capture = <file>` starts a
record; the following `kind`, `verify`, `input`, `var`, `notes` lines belong
to it.

`var` syntax is `name@byte:len` (decimal offsets). `input` values are
decimal or `0x` hex integers, or hex byte strings for `*_hex` names.

## Capture kinds and their mandated variable fields

The authoritative table is `kKindRules` in `tests/net_capture_fixtures.hpp`;
the gate rejects any capture that omits a mandated declaration.

| kind             | verify           | mandated `var` declarations | recorded `input`s |
|------------------|------------------|-----------------------------|-------------------|
| `scalar-sequence`| `encode`         | `sequence`, `acknowledge`   | `sequence`, `acknowledge` |
| `huffman-block`  | `encode`         | (none)                      | `payload_hex` |
| `usercmd-delta`  | `decode-reencode`| `key`, `from_hex`           | `key`, `from_hex` (32-byte `usercmd_s` image, little-endian, the ILP32 wire layout pinned by the net-wire contracts) |

### How to record each kind

* `scalar-sequence` — the two leading 32-bit sequence/acknowledge longs of a
  commercial server message front. Record their values as `input`s; the gate
  re-encodes them with the production `MSG_WriteLong` and byte-compares.
* `huffman-block` — a contiguous Huffman-compressed block as emitted by the
  commercial binary's `MSG_WriteBitsCompress` path, together with the
  plaintext payload (`payload_hex`) it was produced from. Fixed adversarial
  payloads (all-zero, all-FF, alternating, real infostrings) are welcome;
  several captures of this kind may be listed.
* `usercmd-delta` — one encoded usercmd delta block
  (`MSG_WriteDeltaUsercmdKey` output) from a commercial session, with the
  delta `key` and the full 32-byte `from` command state (`from_hex`) it was
  encoded against. The gate decodes with the production reader, re-encodes,
  and byte-compares.

### Sanitization rules

Strip operator-identifying metadata (player names, GUIDs, IPs) from TEXT
capture kinds before committing; codec-level captures carry none. Record in
`sanitized_by` what was removed. Variable fields like challenge/qport stay —
they are exactly what the `var` declarations exist to document.

## Certification protocol

```
ctest -R net-capture-certification -V          # blocked state -> "Not Run"
KISAKCOD_NETCAPTURES_DIR=/path/to/captures ctest -R net-capture-certification
```

Exit codes: `0` all reference captures match (wire-format equivalence
evidenced — **not** a merge approval and not a substitute for the
session-layer certification in `docs/NETWORK_COMPATIBILITY.md`);
`77` evidence missing/malformed (blockers listed); `1` evidence present but
the production codec diverges (retail drift — a failure).

Later layers extend this directory (netchan packet kinds on the ILP32
engine target, session-layer gamestate/getstatus exchanges); they must land
as new `kKindRules` entries with their variable-field specs before any
consumer reads them.
