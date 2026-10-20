#!/usr/bin/env python3
"""Compare the data of two COFF objects, ignoring section numbering and the
order of variables inside a section.

compare-x86-pr-objects.sh calls an object layout-only when its disassembly is
unchanged, but `dumpbin /disasm` shows only code. This check covers the rest:
every non-code, non-debug section, split at the symbols defined in it. Each
piece is keyed by its section name, characteristics and symbol names, and
compared by its bytes and its relocations' targets (by symbol name, not by
index or section number, which a header edit renumbers). The pieces are
compared as a multiset, so a reordered section table, or MSVC emitting the
same variables in another order inside one section, still matches; any
changed byte or relocation target does not.

Compiler-numbered labels ($SG123 string literals and the like) are renumbered
by unrelated edits, so a piece named only by them is keyed by its bytes, and a
relocation to one names the bytes it points at (or the label itself, when
another numbered label holds the same bytes).

COFF symbols carry no size, so a piece runs to the next symbol and keeps the
alignment padding after its variable. A reorder that moves padding between
variables therefore still differs: the check errs toward reporting.

Usage: object-data-sections.py <base.obj> <pr.obj>
Exit status: 0 when the data matches, 1 when it differs, 2 on a malformed
object.
"""

import collections
import hashlib
import importlib.util
import itertools
import pathlib
import re
import sys

IMAGE_SCN_CNT_CODE = 0x00000020
NUMBERED_LABEL = re.compile(r"^\$[A-Za-z]*\d+$")

_spec = importlib.util.spec_from_file_location(
    "object_section_digest", pathlib.Path(__file__).with_name("object-section-digest.py"))
digest_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(digest_tool)


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def defined_symbols(sections, records):
    """Map each section number to {offset: [symbol names defined there]}.

    A section's own symbol (named after it) is left out.
    """
    defined = collections.defaultdict(lambda: collections.defaultdict(list))
    for name, value, number, _storage, _type, _aux in records:
        if 1 <= number <= len(sections) and name != sections[number - 1].name:
            defined[number][value].append(name)
    return defined


def data_runs(sections, records):
    """List (section, start, end, names) for each run of every data section.

    A run goes from one symbol offset to the next (the bytes before the first
    symbol are a run too), so it keeps any alignment padding after its
    variable. names are the symbols defined at start.
    """
    defined = defined_symbols(sections, records)
    runs = []
    for number, section in enumerate(sections, 1):
        if section.is_debug or section.characteristics & IMAGE_SCN_CNT_CODE:
            continue
        size = max(section.size, len(section.content))
        offsets = sorted(offset for offset in defined[number] if 0 <= offset <= size)
        bounds = ([0] if not offsets or offsets[0] != 0 else []) + offsets + [size]
        runs.extend((section, start, end, sorted(defined[number].get(start, [])))
                    for start, end in itertools.pairwise(bounds))
    return runs


def numbered_label_identities(runs):
    """Map each numbered label to the bytes it labels, when those are unique.

    Two numbered labels over identical bytes keep their names, so a pointer
    moved from one to the other still differs.
    """
    labels = collections.defaultdict(list)
    for section, start, end, names in runs:
        for name in filter(NUMBERED_LABEL.match, names):
            labels["%s:%d" % (_sha(section.content[start:end]), end - start)].append(name)
    return {names[0]: identity for identity, names in labels.items() if len(names) == 1}


def data_pieces(path):
    """Return the multiset of an object's data pieces, as compared by main()."""
    with open(path, "rb") as handle:
        _machine, sections, records = digest_tool.parse_coff(handle.read(), path)
    runs = data_runs(sections, records)
    label_bytes = numbered_label_identities(runs)

    pieces = collections.Counter()
    for section, start, end, names in runs:
        stable = tuple(name for name in names if not NUMBERED_LABEL.match(name))
        content = section.content[start:end]
        relocations = tuple(sorted(
            (address - start, kind, label_bytes.get(target, target))
            for address, kind, target, _value, _target_section in section.relocations
            if start <= address < end))
        # The COMDAT selection is link-significant; its checksum and length
        # restate the bytes compared below.
        selection = section.comdat[0] if section.comdat else None
        key = (section.name, section.characteristics, selection, stable or ("bytes", _sha(content)))
        pieces[(key, _sha(content), end - start, relocations)] += 1
    return pieces


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    try:
        same = data_pieces(argv[1]) == data_pieces(argv[2])
    except digest_tool.ObjectError as error:
        sys.stderr.write("object-data-sections: %s\n" % error)
        return 2
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
