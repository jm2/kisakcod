#!/usr/bin/env python3
"""Digest the retail bytes of compiled objects for the byte-parity gate.

An object compiled from one translation unit with identical code-generation
flags can still differ byte-for-byte across build trees because MSVC records
build-tree-specific metadata inside its CodeView debug sections: S_OBJNAME
(the .obj path), S_COMPILE3/S_ENVBLOCK (the command line, including the
per-target preprocessor definitions and the /Fd PDB path), and the .debug$T
type-server reference. None of that metadata reaches the linked retail bytes,
so a whole-file hash over-constrains the parity proof and fails for every
variant even when the generated code is identical.

This tool digests only the retail content of a COFF object: every section
except the CodeView ``.debug$*`` sections and MSVC's ``.chks64`` per-section
checksum table (which covers those debug sections). Each retained section contributes
its name, characteristics, raw bytes, and relocations with symbol-table
indices resolved to the target symbol's name and its link-semantic attributes
(storage value and section number). The digest also folds in every
linker-visible symbol *defined* in a retained section (name, storage value and
class, type, and the content identity of its defining section), because a
definition consumed by another translation unit can be placed at a different
offset across variants while this object's own section bytes and relocations
stay identical. Debug-only symbols and definitions in the excluded
``.debug$*``/``.chks64`` sections cannot shift the result. Non-COFF inputs fall
back to a whole-file digest and are labelled as such, so mixing kinds can never
masquerade as parity.

Usage:
  object-section-digest.py [--report] <object>...
  object-section-digest.py --compare <object>...

Output records are tab-separated, one per line:
  section  <object> <name> <included|excluded> <size> <relocs> <sha256>
  digest   <kind> <sha256> <object>
  compare  <name> <identical|DIFFERS|excluded|missing: ...>

Exit status: 0 on success (for --compare: every retail digest agrees);
1 when --compare finds a retail mismatch; 2 on a malformed/unsupported object.
"""

import hashlib
import struct
import sys

COFF_MACHINES = {
    0x014C: "i386",
    0x8664: "amd64",
    0x01C0: "arm",
    0x01C4: "armnt",
    0xAA64: "arm64",
}
DEBUG_SECTION_PREFIX = ".debug$"
# MSVC also emits .chks64, a table of per-section checksums that covers the
# CodeView sections, so it carries the same build-tree-specific metadata and
# must be excluded alongside them.
METADATA_SECTION_NAMES = frozenset([".chks64"])
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
SYMBOL_SIZE = 18
SECTION_HEADER_SIZE = 40
RELOCATION_SIZE = 10


class ObjectError(Exception):
    """A malformed or unsupported object file."""


def _cstring(raw):
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def _sha256(*parts):
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part)
    return digest.hexdigest()


class Section(object):
    __slots__ = ("name", "characteristics", "size", "content", "relocations",
                 "comdat")

    def __init__(self, name, characteristics, size, content, relocations):
        self.name = name
        self.characteristics = characteristics
        self.size = size
        self.content = content
        self.relocations = relocations
        # Normalized COMDAT auxiliary record of this section's definition
        # symbol, set after parsing: (selection, checksum, length, numrelocs,
        # associated_section_key). None when the section has no section symbol.
        self.comdat = None

    @property
    def is_debug(self):
        return (self.name.startswith(DEBUG_SECTION_PREFIX)
                or self.name in METADATA_SECTION_NAMES)

    def content_key(self):
        # Identity of the section for symbol-placement purposes: its name,
        # characteristics, and raw bytes, independent of its relocations (so
        # this can key a symbol's defining section without recursion) and of
        # its position in the section table (so trailing debug sections cannot
        # shift it).
        return _sha256(b"seckey\0", self.name.encode("utf-8"), b"\0",
                       struct.pack("<I", self.characteristics), self.content)

    def digest(self):
        parts = [
            b"section\0",
            self.name.encode("utf-8"),
            b"\0",
            struct.pack("<II", self.characteristics, self.size),
            self.content,
            b"\0relocs\0",
        ]
        for address, kind, symbol, value, section_number in self.relocations:
            parts.append(struct.pack("<IH", address, kind))
            parts.append(symbol.encode("utf-8"))
            parts.append(b"\0")
            # The linker resolves each relocation using the target symbol's
            # Value and SectionNumber, so two objects that share section bytes
            # and symbol names but place a target symbol at a different value
            # or section still link to different retail bytes. Fold those
            # link-semantic attributes in, keyed by the resolved name rather
            # than the raw (build-order-dependent) symbol index.
            parts.append(struct.pack("<ih", value, section_number))
        if self.comdat is not None:
            selection, checksum, length, numrelocs, assoc = self.comdat
            # The COMDAT selection and associative target control how the
            # linker resolves duplicate definitions, so they are part of the
            # section's link identity. checksum/length/numrelocs are derived
            # from the section content (stable across identical compiles); the
            # associative target is keyed by content, not raw index.
            parts.append(b"\0comdat\0")
            parts.append(struct.pack("<BIIH", selection, checksum, length, numrelocs))
            parts.append(assoc.encode("ascii"))
        return _sha256(*parts)


def parse_coff(data, label):
    if len(data) < 20:
        raise ObjectError("%s: too small to be a COFF object" % label)
    if data[:4] == b"\0\0\xff\xff":
        raise ObjectError("%s: /bigobj anonymous objects are not supported" % label)
    (machine, section_count, _timestamp, symbol_offset, symbol_count,
     optional_size, _characteristics) = struct.unpack_from("<HHIIIHH", data, 0)
    if machine not in COFF_MACHINES:
        raise ObjectError("%s: unsupported COFF machine 0x%04x" % (label, machine))

    string_table = b""
    if symbol_offset:
        strings_at = symbol_offset + symbol_count * SYMBOL_SIZE
        if strings_at + 4 <= len(data):
            (string_size,) = struct.unpack_from("<I", data, strings_at)
            string_table = data[strings_at:strings_at + max(string_size, 4)]

    def string_at(offset):
        if offset < 4 or offset >= len(string_table):
            raise ObjectError("%s: string table offset %d out of range" % (label, offset))
        end = string_table.find(b"\0", offset)
        if end == -1:
            end = len(string_table)
        return string_table[offset:end].decode("ascii", "replace")

    symbols = {}
    symbol_records = []
    index = 0
    while index < symbol_count:
        at = symbol_offset + index * SYMBOL_SIZE
        raw = data[at:at + SYMBOL_SIZE]
        if len(raw) < SYMBOL_SIZE:
            raise ObjectError("%s: truncated symbol table" % label)
        if raw[:4] == b"\0\0\0\0":
            (name_offset,) = struct.unpack_from("<I", raw, 4)
            name = string_at(name_offset)
        else:
            name = _cstring(raw[:8])
        # COFF symbol record: Value at 8 (u32), SectionNumber at 12 (i16),
        # Type at 14 (u16), StorageClass at 16 (u8).
        value, section_number, sym_type = struct.unpack_from("<ihH", raw, 8)
        storage_class = raw[16]
        aux_count = raw[17]
        aux_raw = None
        if aux_count:
            aux_raw = data[at + SYMBOL_SIZE:at + SYMBOL_SIZE + SYMBOL_SIZE]
        symbols[index] = (name, value, section_number)
        symbol_records.append(
            (name, value, section_number, storage_class, sym_type, aux_raw))
        index += 1 + aux_count

    sections = []
    for number in range(section_count):
        at = 20 + optional_size + number * SECTION_HEADER_SIZE
        raw = data[at:at + SECTION_HEADER_SIZE]
        if len(raw) < SECTION_HEADER_SIZE:
            raise ObjectError("%s: truncated section table" % label)
        (raw_name, _virtual_size, _virtual_address, raw_size, raw_pointer,
         relocation_pointer, _line_pointer, relocation_count, _line_count,
         characteristics) = struct.unpack_from("<8sIIIIIIHHI", raw, 0)
        name = _cstring(raw_name)
        if name.startswith("/"):
            try:
                name = string_at(int(name[1:]))
            except ValueError:
                raise ObjectError("%s: malformed long section name %r" % (label, name))
        if raw_pointer and raw_size:
            content = data[raw_pointer:raw_pointer + raw_size]
            if len(content) != raw_size:
                raise ObjectError("%s: section %s is truncated" % (label, name))
        else:
            content = b""

        relocations = []
        if relocation_pointer and relocation_count:
            count = relocation_count
            start = relocation_pointer
            if (characteristics & IMAGE_SCN_LNK_NRELOC_OVFL) and relocation_count == 0xFFFF:
                (count, _symbol, _kind) = struct.unpack_from("<IIH", data, relocation_pointer)
                count -= 1
                start = relocation_pointer + RELOCATION_SIZE
            for entry in range(count):
                offset = start + entry * RELOCATION_SIZE
                if offset + RELOCATION_SIZE > len(data):
                    raise ObjectError("%s: truncated relocations in %s" % (label, name))
                address, symbol_index, kind = struct.unpack_from("<IIH", data, offset)
                # Do not shadow the enclosing section's `name` here.
                target_name, target_value, target_section = symbols.get(
                    symbol_index, ("#%d" % symbol_index, 0, 0))
                relocations.append(
                    (address, kind, target_name, target_value, target_section))
        sections.append(Section(name, characteristics, raw_size, content, relocations))

    # Attach each section's COMDAT auxiliary record (from its section-definition
    # symbol) so the digest reflects linker-semantic selection/association.
    IMAGE_SYM_CLASS_STATIC = 3
    IMAGE_COMDAT_SELECT_ASSOCIATIVE = 5
    section_count = len(sections)
    for name, value, section_number, storage_class, sym_type, aux_raw in symbol_records:
        if (storage_class != IMAGE_SYM_CLASS_STATIC or aux_raw is None
                or len(aux_raw) < 15 or section_number < 1
                or section_number > section_count):
            continue
        section = sections[section_number - 1]
        if section.name != name or section.comdat is not None:
            continue
        length, numrelocs = struct.unpack_from("<IH", aux_raw, 0)
        (checksum,) = struct.unpack_from("<I", aux_raw, 8)
        (assoc_number,) = struct.unpack_from("<H", aux_raw, 12)
        selection = aux_raw[14]
        assoc = ""
        if selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE:
            if 1 <= assoc_number <= section_count:
                target = sections[assoc_number - 1]
                assoc = "EXCL" if target.is_debug else target.content_key()
            else:
                assoc = "?"
        section.comdat = (selection, checksum, length, numrelocs, assoc)
    return machine, sections, symbol_records


class ObjectDigest(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as handle:
            data = handle.read()
        self.sections = []
        self.definitions = []
        self.machine = None
        try:
            self.machine, self.sections, symbol_records = parse_coff(data, path)
            self.kind = "coff-retail-sections"
        except ObjectError as error:
            if data[:4] in (b"\x7fELF", b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"!<ar"):
                self.kind = "whole-file"
                self.digest = _sha256(data)
                return
            raise error
        self.definitions = self._collect_definitions(symbol_records)
        parts = [b"coff\0", struct.pack("<H", self.machine)]
        for section in self.sections:
            if section.is_debug:
                continue
            parts.append(section.digest().encode("ascii"))
            parts.append(b"\n")
        parts.append(b"defs\0")
        for name, value, storage_class, sym_type, section_key in self.definitions:
            parts.append(name.encode("utf-8"))
            parts.append(b"\0")
            parts.append(struct.pack("<IHB", value & 0xffffffff, sym_type, storage_class))
            parts.append(section_key.encode("ascii"))
            parts.append(b"\n")
        self.digest = _sha256(*parts)

    def _collect_definitions(self, symbol_records):
        # A symbol DEFINED in a retained section is linker-visible: other
        # translation units resolve to its (section, value) placement, so it
        # must be part of the parity identity even when no relocation in this
        # object targets it. Symbols that are undefined (SectionNumber 0),
        # absolute/compiler-metadata (< 0), or defined inside an excluded
        # .debug$*/.chks64 section are not retail placements and are skipped.
        # The defining section is keyed by its content identity, not its raw
        # table index, so trailing debug sections cannot shift the result.
        section_count = len(self.sections)
        definitions = []
        for record in symbol_records:
            name, value, section_number, storage_class, sym_type = record[:5]
            if section_number < 1 or section_number > section_count:
                continue
            section = self.sections[section_number - 1]
            if section.is_debug:
                continue
            # Key the defining section by its full digest (name, characteristics,
            # bytes, relocations, and COMDAT aux), so two same-named sections
            # with identical bytes but different relocations are distinguished.
            definitions.append(
                (name, value, storage_class, sym_type, section.digest()))
        # Sort so the digest is independent of symbol-table order.
        definitions.sort()
        return definitions

    def report_lines(self):
        for section in self.sections:
            yield "section\t%s\t%s\t%s\t%d\t%d\t%s" % (
                self.path,
                section.name,
                "excluded" if section.is_debug else "included",
                section.size,
                len(section.relocations),
                section.digest(),
            )

    def definitions_digest(self):
        return _sha256(*[
            (b"%s\0" % name.encode("utf-8"))
            + struct.pack("<IHB", value & 0xffffffff, sym_type, storage_class)
            + section_key.encode("ascii")
            for name, value, storage_class, sym_type, section_key in self.definitions
        ]) if self.definitions else "(none)"

    def report_definition_line(self):
        return "definitions\t%s\t%d\t%s" % (
            self.path, len(self.definitions), self.definitions_digest())

    def digest_line(self):
        return "digest\t%s\t%s\t%s" % (self.kind, self.digest, self.path)


def compare(objects):
    order = []
    seen = set()
    for item in objects:
        for section in item.sections:
            if section.name not in seen:
                seen.add(section.name)
                order.append(section.name)
    mismatch = False
    for name in order:
        digests = []
        missing = []
        for item in objects:
            matches = [section for section in item.sections if section.name == name]
            if not matches:
                missing.append(item.path)
                continue
            digests.append(_sha256(*[section.digest().encode("ascii") for section in matches]))
        if name.startswith(DEBUG_SECTION_PREFIX) or name in METADATA_SECTION_NAMES:
            status = "excluded"
        elif missing:
            status = "missing: " + ", ".join(missing)
            mismatch = True
        elif len(set(digests)) > 1:
            status = "DIFFERS"
            mismatch = True
        else:
            status = "identical"
        print("compare\t%s\t%s" % (name, status))
    def_digests = [item.definitions_digest() for item in objects]
    if len(set(def_digests)) > 1:
        # Surface the specific symbols whose placement/attributes diverge.
        keyed = [{d[0]: d for d in item.definitions} for item in objects]
        all_names = sorted(set().union(*[set(k) for k in keyed])) if keyed else []
        diff_names = [n for n in all_names
                      if len(set(k.get(n) for k in keyed)) > 1]
        detail = (": " + ", ".join(diff_names[:8])) if diff_names else ""
        print("compare\tdefinitions\tDIFFERS%s" % detail)
        mismatch = True
    else:
        print("compare\tdefinitions\tidentical")
    kinds = set(item.kind for item in objects)
    retail = set(item.digest for item in objects)
    if len(kinds) > 1:
        print("compare\tdigest-kind\tDIFFERS (%s)" % ", ".join(sorted(kinds)))
        mismatch = True
    print("compare\tretail-digest\t%s" % ("DIFFERS" if len(retail) > 1 else "identical"))
    return not mismatch and len(retail) == 1


def main(argv):
    report = False
    mode_compare = False
    paths = []
    for arg in argv[1:]:
        if arg == "--report":
            report = True
        elif arg == "--compare":
            mode_compare = True
            report = True
        elif arg.startswith("-"):
            sys.stderr.write("object-section-digest: unknown option %s\n" % arg)
            return 2
        else:
            paths.append(arg)
    if not paths:
        sys.stderr.write(__doc__)
        return 2
    objects = []
    try:
        for path in paths:
            objects.append(ObjectDigest(path))
    except (ObjectError, OSError) as error:
        sys.stderr.write("object-section-digest: %s\n" % error)
        return 2
    for item in objects:
        if report:
            for line in item.report_lines():
                print(line)
            if item.kind == "coff-retail-sections":
                print(item.report_definition_line())
        print(item.digest_line())
    if mode_compare:
        return 0 if compare(objects) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
