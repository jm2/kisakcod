#!/usr/bin/env python3
"""Compare the data sections of two COFF objects, ignoring section numbering.

compare-x86-pr-objects.sh calls an object layout-only when its disassembly is
unchanged, but `dumpbin /disasm` shows only code. This check covers the rest:
every non-code, non-debug section's name, characteristics, raw bytes and
relocation targets (by symbol name, not by index or section number, which a
header edit renumbers). The sections are compared as a sorted list, so a
reordered section table still matches.

Usage: object-data-sections.py <base.obj> <pr.obj>
Exit status: 0 when the data sections match, 1 when they differ, 2 on a
malformed object.
"""

import hashlib
import importlib.util
import pathlib
import struct
import sys

IMAGE_SCN_CNT_CODE = 0x00000020

_spec = importlib.util.spec_from_file_location(
    "object_section_digest", pathlib.Path(__file__).with_name("object-section-digest.py"))
digest_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(digest_tool)


def data_sections(path):
    with open(path, "rb") as handle:
        _machine, sections, _symbols = digest_tool.parse_coff(handle.read(), path)
    keys = []
    for section in sections:
        if section.is_debug or section.characteristics & IMAGE_SCN_CNT_CODE:
            continue
        digest = hashlib.sha256()
        digest.update(section.name.encode("utf-8") + b"\0")
        digest.update(struct.pack("<I", section.characteristics))
        digest.update(section.content)
        for address, kind, target, _value, _section_number in section.relocations:
            digest.update(struct.pack("<IH", address, kind) + target.encode("utf-8") + b"\0")
        keys.append(digest.hexdigest())
    return sorted(keys)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    try:
        same = data_sections(argv[1]) == data_sections(argv[2])
    except digest_tool.ObjectError as error:
        sys.stderr.write("object-data-sections: %s\n" % error)
        return 2
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
