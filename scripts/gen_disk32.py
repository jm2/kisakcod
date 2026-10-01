#!/usr/bin/env python3
"""Generate the disk32 mirrors and 64-bit loaders from src/database/db_disk32.schema and its includes."""

# A retail fast-file stores each record in its 32-bit (ILP32) layout
# (docs/design/FASTFILE_LOADER.md). For every schema record this emits the
# disk32 mirror struct (pointer fields are 4-byte disk32::Ptr32 tokens) with
# ONDISK_SIZE/ONDISK_OFFSET asserts on the declared retail values, and
# RUNTIME_SIZE/RUNTIME_OFFSET asserts on the native struct the mirror converts
# into, computed from the same field list, so a native struct that drifts from
# its schema stops the build. Declared offsets that break the ILP32 layout
# rules fail here. For every record with an asset line it also emits that
# family's 64-bit loader steps, as the schema's header describes. The output
# is a build artifact, never committed (AGENTS.md rule 8).

import re
import sys
from pathlib import Path
from string import Template
from typing import NoReturn

# kind: (mirror type, native size and alignment at ILP32, at 64-bit)
KINDS = {
    'i32': ('std::int32_t', 4, 4),
    'u32': ('std::uint32_t', 4, 4),
    'f32': ('float', 4, 4),
    'i16': ('std::int16_t', 2, 2),
    'u16': ('std::uint16_t', 2, 2),
    'u8': ('std::uint8_t', 1, 1),
    'bool': ('std::uint8_t', 1, 1),  # any disk byte; the loader tests != 0
    'xstring': ('Ptr32<const char>', 4, 8),
    'bytes': ('Ptr32<const char>', 4, 8),
    'xstrings': ('Ptr32<Ptr32<const char>>', 4, 8),
    'pointer': ('Ptr32<void>', 4, 8),  # a token only a custom body loads
}
COUNTED = ('bytes', 'xstrings')
SCALARS = ('i32', 'u32', 'f32', 'i16', 'u16', 'u8', 'bool')
ASSET_KEYS = {'member', 'pool', 'kind', 'alias', 'label', 'name', 'body'}


def fail(where, message) -> NoReturn:
    sys.exit(f'{where}: {message}')


def attributes(words, where, allowed, flags=()):
    """key=value words and bare flags, each allowed and given at most once."""
    attrs = {}
    for word in words:
        key, value = word.split('=', 1) if '=' in word else (word, True)
        if key in attrs or key not in allowed or (value is True) != (key in flags):
            fail(where, f'unexpected {word!r}; allowed: {sorted(allowed)}')
        attrs[key] = value
    return attrs


def parse_record(words, where):
    attrs = dict(word.split('=', 1) for word in words[3:] if '=' in word)
    if len(words) < 3 or len(attrs) != len(words) - 3 \
            or not {'runtime', 'header'} <= attrs.keys() <= {'runtime', 'header', 'same'}:
        fail(where, 'expected: record <Name> <size> runtime=<type> header=<path> [same=<constant>]')
    return {'name': words[1], 'size': int(words[2], 0), 'fields': [], 'asset': None, 'where': where, **attrs}


def parse_asset(record, words, where):
    asset = attributes(words[1:], where, ASSET_KEYS)
    if record['asset'] or not {'member', 'pool', 'kind', 'alias', 'label'} <= asset.keys() \
            or asset['alias'] not in ('inserted', 'completed') or asset.get('body', 'custom') != 'custom':
        fail(where, 'expected one line: asset member= pool= kind= alias=<inserted|completed> label= '
                    '[name=] [body=custom]')
    record['asset'] = {**asset, 'where': where}


def parse_field(words, where):
    if len(words) < 3 or words[2] not in KINDS:
        fail(where, f'expected: <offset> <field> <kind> [attributes]; kinds {sorted(KINDS)}')
    kind = words[2]
    allowed = ({'count', 'terminated', 'paired', 'label'} if kind == 'bytes'
               else {'count'} if kind in COUNTED + SCALARS else set())
    attrs = attributes(words[3:], where, allowed, flags=('terminated', 'paired'))
    count = attrs.get('count', '')
    if kind in COUNTED and not count:
        fail(where, f'count=<expression> is required for {COUNTED}')
    if kind in SCALARS and count and not (count.isdigit() and int(count) > 1):
        fail(where, 'a scalar takes count=<n>, n > 1, as a fixed array of n')
    return {'offset': int(words[0], 0), 'name': words[1], 'kind': kind,
            'count': count if kind in COUNTED else '', 'length': int(count) if kind in SCALARS and count else 1,
            'terminated': 'terminated' in attrs, 'paired': 'paired' in attrs, 'label': attrs.get('label'),
            'where': where}


def parse_include(path, words, where, records, root):
    """Append the records of the files an include line names, in name order."""
    included = sorted(path.parent.glob(words[1])) if root and len(words) == 2 else []
    if not included:
        fail(where, 'expected, in the root schema only: include <glob matching schema files>')
    for schema in included:
        parse(schema, records, root=False)


def parse_line(file_records, words, where):
    """Add one record, asset or field line to the records of the file being read."""
    if words[0] == 'record':
        file_records.append(parse_record(words, where))
    elif not file_records:
        fail(where, 'a field must follow a record line in the same file')
    elif words[0] == 'asset':
        parse_asset(file_records[-1], words, where)
    else:
        file_records[-1]['fields'].append(parse_field(words, where))


def parse(path, records, root=True):
    """Append path's records; only the root schema may include other files, each read in full."""
    file_records = []  # this file's records since its last include line
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        where, words = f'{path}:{number}', raw.split('#', 1)[0].split()
        if words and words[0] == 'include':
            records += file_records
            file_records = []
            parse_include(path, words, where, records, root)
        elif words:
            parse_line(file_records, words, where)
    records += file_records
    return records


def layout(fields, width):
    """Natural-alignment offsets and size; width 1 is ILP32, 2 is 64-bit."""
    offsets, offset, biggest = [], 0, 1
    for field in fields:
        size = KINDS[field['kind']][width]
        offset = (offset + size - 1) // size * size
        offsets.append(offset)
        offset += size * field['length']
        biggest = max(biggest, size)
    return offsets, (offset + biggest - 1) // biggest * biggest


def emit(records, schema_name):
    out = [f'// Generated by scripts/gen_disk32.py from {schema_name}. Do not edit;',
           '// it is rebuilt from the schema and never committed (AGENTS.md rule 8).',
           '#pragma once', '', '#include <database/db_disk32.h>', '#include <universal/kisak_abi.h>']
    out += [f'#include <{header}>' for header in sorted({record['header'] for record in records})]
    out += ['', '#include <cstddef>', '#include <cstdint>', '#include <type_traits>', '', 'namespace disk32', '{']
    for record in records:
        mirror = record['name'] + 'Disk32'
        out += [f'// Retail {record["name"]}: 0x{record["size"]:02X} bytes.', f'struct {mirror}', '{']
        for field in record['fields']:
            count = f' // count: {field["count"]}' if field['count'] else ''
            length = f'[{field["length"]}]' if field['length'] > 1 else ''
            out.append(f'    {KINDS[field["kind"]][0]} {field["name"]}{length};{count}')
        out += ['};', f'ONDISK_SIZE({mirror}, 0x{record["size"]:02X});']
        out += [f'ONDISK_OFFSET({mirror}, {field["name"]}, 0x{field["offset"]:02X});'
                for field in record['fields']]
        out += [f'static_assert(alignof({mirror}) == 4 && std::is_trivially_copyable_v<{mirror}>',
                f'    && std::is_standard_layout_v<{mirror}>);']
        if 'same' in record:
            out.append(f'static_assert(sizeof({mirror}) == {record["same"]});')
        out.append('')
    out += ['} // namespace disk32', '', '// The native struct each mirror converts into, from the same field list.']
    for record in records:
        (off32, size32), (off64, size64) = layout(record['fields'], 1), layout(record['fields'], 2)
        out.append(f'RUNTIME_SIZE({record["runtime"]}, 0x{size32:02X}, 0x{size64:02X});')
        out += [f'RUNTIME_OFFSET({record["runtime"]}, {field["name"]}, 0x{a:02X}, 0x{b:02X});'
                for field, a, b in zip(record['fields'], off32, off64)]
    return '\n'.join(out) + '\n'


# The loader steps. Each mirrors its 32-bit Load_* in db_load.cpp.
HEADER_SLOT = Template('''\
// $Name's header slot: the zero-extended disk32 token on entry, the native
// pointer on return.
inline void Load${Name}HeaderSlot(bool atStreamStart, $Runtime **slot)
{
    // Asset headers arrive inside the already-streamed XAsset array; a native
    // slot is never the 4-byte disk slot at the stream position.
    if (atStreamStart || !slot)
    {
        Drop("Invalid 64-bit fast-file $label header request");
        return;
    }
    std::uintptr_t raw = 0;
    std::memcpy(&raw, slot, sizeof(raw));
    *slot = nullptr;
    if (raw > UINT32_MAX)
    {
        Drop("Fast-file $label header slot holds no disk32 token");
        return;
    }
    Load${Name}Ptr(disk32::PointerToken{static_cast<std::uint32_t>(raw)}, slot);
}
''')

INSERTED_PTR = Template('''\
// $Name's pointer step (alias=inserted): an offset token names a pooled
// alias; -1 and -2 convert the record into a native temporary that the pool
// call copies, and -2 registers the pooled pointer. As in the 32-bit step,
// the record streams into the temp block whichever block the referrer is in,
// so a header slot and a reference nested in another record call it alike.
inline void Load${Name}Ptr(disk32::PointerToken token, $Runtime **slot)
{
    if (token.isOffset())
    {
        std::uintptr_t pointer = 0;
        const db::relocation::Status status =
            DB_ResolveInsertedPointer(token, DBAliasKind::$kind, 0, &pointer);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<$Runtime *>(pointer);
        return;
    }
    if (token.isNull())
        return;
    DB_PushStreamPos(kTempBlock);
    if (!DB_AllocStreamPos(3))
        return;
    const DBAliasHandle inserted =
        token.isSharedInline() ? DB_InsertPointer(DBAliasKind::$kind) : DBAliasHandle{};
    $Runtime native{};
    if ((token.isSharedInline() && !inserted) || !Load$Name(&native))
        return;
    XAssetHeader header;
    header.$member = &native;
    $pool(&header);
    *slot = header.$member;
    if (inserted)
        DB_SetInsertedPointer(inserted, DBAliasKind::$kind, header.$member);
    DB_PopStreamPos();
}
''')

INSERTED_BODY = Template('''\
// $Name's record body, hand-written in its TU: streams the record at the
// temp block's position and converts it into *out, the native temporary the
// pool call copies.
bool Load$Name($Runtime *out);
''')

COMPLETED_PTR = Template('''\
// $Name's record body, hand-written in its TU: streams the record at
// `record` and converts it into zone-lifetime native storage, *out.
bool Load$Name(std::uint8_t *record, $Runtime **out);

// $Name's pointer step (alias=completed): the disk32 record stays the
// completed-object identity that later offset tokens name, and the alias
// registry maps it to the native object, so no alias points at disk bytes.
inline void Load${Name}Ptr(disk32::PointerToken token, $Runtime **slot)
{
    constexpr auto kRecordBytes = static_cast<std::uint32_t>(sizeof(disk32::${Name}Disk32));
    if (token.isNull())
        return;
    if (!token.isInline())
    {
        // The 32-bit loader sends every other token, -2 included, to the
        // alias registry; it must name a completed record in block 4.
        std::uintptr_t native = 0;
        const db::relocation::Status status =
            DB_ResolveCompletedObjectNative(token, DBAliasKind::$kind, kRecordBytes, &native);
        if (status != db::relocation::Status::Ok)
        {
            Com_Error(ERR_DROP, "Invalid fast-file alias offset: %s", db::relocation::StatusName(status));
            return;
        }
        *slot = reinterpret_cast<$Runtime *>(native);
        return;
    }
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!record)
        return;
    const DBAliasHandle completed = DB_RegisterPointerSlot(record, DBAliasKind::$kind);
    $Runtime *object = nullptr;
    if (!completed || !Load$Name(record, &object)
        || !DB_CompleteObject(completed, DBAliasKind::$kind, record, kRecordBytes, kRecordBytes, object))
    {
        return;
    }
    XAssetHeader header;
    header.$member = object;
    $pool(&header);
    if (!header.$member)
    {
        Drop("Fast-file $noun was not registered");
        return;
    }
    *slot = header.$member;
}
''')

FLAT_BODY = Template('''\
// $Name's record body: the retail record into its mirror, then each field
// converted into the native temporary.
inline bool Load$Name($Runtime *out)
{
    disk32::${Name}Disk32 disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
${scalars}    DB_PushStreamPos(kVirtualBlock);
${pointers}    DB_PopStreamPos();
    return true;
}
''')

XSTRING = Template('''\
    if (!LoadXString(disk.$field, &out->$field))
        return false;
''')

NAME_CHECK = Template('''\
    if (!out->$field)
        return Drop("Fast-file $noun has no name"); // the asset pool hashes it
''')

# paired: the bytes are present exactly when their count field is nonzero.
PAIRED = Template('''\
    if (disk.$field.token.isNull() && disk.$count != 0)
        return Drop("Fast-file $owner has a count but no $noun");
''')

# As in the 32-bit loader, any non-null token means the bytes follow inline.
TERMINATED_BYTES = Template('''\
    if (!disk.$field.token.isNull())
    {
        std::int32_t count = 0;
        std::uint8_t *const bytes = DB_AllocStreamPos(0);
        if (!db::validation::CheckedCountSum(disk.$count, $extra, &count) || count < 1)
            return Drop("Invalid fast-file $label length");
        if (!StreamBytes(bytes, count))
            return false;
        if (bytes[count - 1] != '\\0')
            return Drop("Fast-file $noun is not terminated");
        out->$field = reinterpret_cast<decltype(out->$field)>(bytes);
    }
''')


def noun(label):
    return label.replace('-', ' ')


def scalar_copy(field):
    """A bool converts as != 0, so no disk byte lands in a C++ bool."""
    if field['length'] > 1:
        fail(field['where'], 'a generated body copies single scalars; a fixed array needs body=custom')
    test = ' != 0' if field['kind'] == 'bool' else ''
    return f'    out->{field["name"]} = disk.{field["name"]}{test};\n'


def bytes_count(record, field):
    """A generated body's terminated bytes field: its count field and the constant added to it."""
    kinds = {other['name']: other['kind'] for other in record['fields']}
    count = re.fullmatch(r'(\w+)(?:\+(\d+))?', field['count'])
    if field['kind'] != 'bytes' or not field['terminated'] or not field['label'] or not count \
            or kinds.get(count.group(1)) not in ('i32', 'u32'):
        fail(field['where'], 'a generated body loads scalars, xstrings and terminated bytes '
                             f'(count=<i32 field>[+<n>] terminated label=<noun>), not this {field["kind"]} field')
    return count.group(1), count.group(2) or '0'


def body_step(record, field):
    """The generated body's statements for one pointer field."""
    if field['kind'] == 'xstring':
        step = XSTRING.substitute(field=field['name'])
        if field['name'] == record['asset'].get('name'):
            step += NAME_CHECK.substitute(field=field['name'], noun=noun(record['asset']['label']))
        return step
    count, extra = bytes_count(record, field)
    facts = dict(field=field['name'], count=count, extra=extra, label=field['label'],
                 noun=noun(field['label']), owner=noun(record['asset']['label']))
    return (PAIRED.substitute(facts) if field['paired'] else '') + TERMINATED_BYTES.substitute(facts)


def emit_body(record):
    asset = record['asset']
    if {field['name']: field['kind'] for field in record['fields']}.get(asset.get('name')) != 'xstring':
        fail(asset['where'], 'a generated body needs name=<the xstring the pool hashes>')
    scalars = ''.join(scalar_copy(field) for field in record['fields'] if field['kind'] in SCALARS)
    pointers = ''.join(body_step(record, field) for field in record['fields'] if field['kind'] not in SCALARS)
    return FLAT_BODY.substitute(Name=record['name'], Runtime=record['runtime'], scalars=scalars, pointers=pointers)


def emit_family(record):
    asset = record['asset']
    inserted, custom = asset['alias'] == 'inserted', 'body' in asset
    if not (custom or inserted):
        fail(asset['where'], 'alias=completed takes body=custom: its native storage is family-specific')
    if custom and 'name' in asset:
        fail(asset['where'], 'name= applies to a generated body; a custom body checks its own name')
    facts = dict(Name=record['name'], Runtime=record['runtime'], kind=asset['kind'], member=asset['member'],
                 pool=asset['pool'], label=asset['label'], noun=noun(asset['label']))
    # COMPLETED_PTR declares its own custom body.
    body = [INSERTED_BODY.substitute(facts) if custom else emit_body(record)] if inserted else []
    return body + [(INSERTED_PTR if inserted else COMPLETED_PTR).substitute(facts), HEADER_SLOT.substitute(facts)]


def emit_loaders(records, schema_name):
    out = [f'// Generated by scripts/gen_disk32.py from {schema_name}. Do not edit;',
           '// it is rebuilt from the schema and never committed (AGENTS.md rule 8).',
           '#pragma once', '',
           '// The 64-bit family loaders (docs/design/FASTFILE_LOADER.md). Each step mirrors',
           '// its 32-bit Load_* in db_load.cpp: the same stream pushes, alignments and',
           '// reads, so every block offset stays the retail one, while each record is',
           '// read through its disk32 mirror and every pointer resolves at full width.',
           "// Everything is inline, so a family's TU emits only the steps it calls.",
           '// Frames hold no destructors, since a production Com_Error(ERR_DROP)',
           '// longjmps out of them; an error return may leave a stream pushed, and',
           '// DB_InitStreams resets the stack for the next zone.', '',
           '#include <universal/kisak_abi.h>', '', '#if KISAK_ARCH_64BIT', '',
           '#include <database/db_disk32_load_internal.h>', '#include <database/db_disk32_mirrors.h>',
           '#include <database/db_validation.h>', '', '#include <cstdint>', '#include <cstring>', '',
           'namespace db::disk32_load', '{']
    for record in records:
        if record['asset']:
            out += emit_family(record)
    out += ['} // namespace db::disk32_load', '', '#endif // KISAK_ARCH_64BIT']
    return '\n'.join(out) + '\n'


def write(output, text):
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: gen_disk32.py <schema> <mirrors header> <loaders header>')
    schema, mirrors, loaders = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    records = parse(schema, [])
    if not records:
        fail(schema, 'no records')
    names = [record['name'] for record in records]
    for record in records:
        if names.count(record['name']) > 1:
            fail(record['where'], f'record {record["name"]} is declared more than once')
        offsets, size = layout(record['fields'], 1)
        declared = [field['offset'] for field in record['fields']]
        if offsets != declared or size != record['size']:
            fail(record['where'], f'{record["name"]}: declared offsets {declared} and size {record["size"]} '
                                  f'break the ILP32 layout {offsets} / {size}')
    text = emit_loaders(records, schema.name)  # checks every asset line before anything is written
    write(mirrors, emit(records, schema.name))
    write(loaders, text)


if __name__ == '__main__':
    main()
