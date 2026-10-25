# ADR-0009: Retail Steam 1.8 is the behaviour reference

Status: accepted
Date: 2026-10-25

## Context

The decompile has real errors, but code that looks wrong is often what
retail does. In the 2026-10-25 sweep four suspected bugs matched retail
(setDvar's raw value, the finishPlayerDamage clamp, the psoffsettime getter
reading archiveTime, map vehicles keeping their classname); the int-division
sprint meter and `MY_DIMS[idx+4]` were decompile errors.

## Decision

- Before changing x86-visible behaviour, read the retail 1.8 `iw3mp.exe`
  ([ADR-0004](0004-reverse-engineering-scope.md)) on a host-local copy:
  `dumpbin /disasm:nobytes /section:.text` or objdump. Find the function by a
  unique `.rdata` string, a dvar global (the `mov [global],eax` after its
  `Dvar_Register`), a field-table entry or float-table bytes.
- Retail does the same: keep it, and close the issue "matches retail" with
  the address and instruction. Never quote bytes.
- Ours differs by a decompile error: fix it and declare the x86 change.
- A zone-reload regression run: `+loadzone ui_mp +map mp_crash
  +db_graphhash menu list +quit` gives the same 360-menu hash on x86, Win64
  and arm64 headless. The `Mark_*` walkers share the `Load_*` `var*`
  globals, so a legacy `Load_*Ptr` keeps its slot in a local (#764).

## Consequences

Fidelity questions are settled from the retail exe, not from intuition, and
the sweep's "matches retail" closes are re-checkable.
