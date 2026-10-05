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

import math
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
    'u64': ('Packed64', 8, 8),  # 8-aligned at both widths; only a custom body loads it
    'i16': ('std::int16_t', 2, 2),
    'u16': ('std::uint16_t', 2, 2),
    'u8': ('std::uint8_t', 1, 1),
    'bool': ('std::uint8_t', 1, 1),  # any disk byte; the loader tests != 0
    'xstring': ('Ptr32<const char>', 4, 8),
    'bytes': ('Ptr32<const char>', 4, 8),
    'xstrings': ('Ptr32<Ptr32<const char>>', 4, 8),
    'pointer': ('Ptr32<void>', 4, 8),  # a token a custom body loads, or asset=<family> a generated one
    'rawptr': ('std::uint32_t', 4, 8),  # pointer bytes that are no token; the loader nulls them
    'array': ('Ptr32<{of}Disk32>', 4, 8),  # count records of=<a nested record>
    'struct': ('{of}Disk32', 0, 0),  # a nested record inline; its layout gives size and alignment
    'pad': ('std::uint8_t', 1, 1),  # bytes with no native member: no RUNTIME_OFFSET, never loaded
    'run': ('std::uint32_t', 4, 4),  # run[<n words>] to=<last member>: native members laid out alike at both widths
}
COUNTED = ('bytes', 'xstrings', 'array')
SCALARS = ('i32', 'u32', 'f32', 'i16', 'u16', 'u8', 'bool')
ARRAYABLE = ('i32', 'u32', 'f32', 'i16', 'u16', 'u8')  # fixed arrays copy as bytes; a bool needs != 0
ASSET_KEYS = {'member', 'pool', 'kind', 'alias', 'label', 'name', 'body', 'check', 'slot'}


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
            or not {'runtime', 'header'} <= attrs.keys() <= {'runtime', 'header', 'same', 'copy'} \
            or attrs.get('copy', 'scalars') != 'scalars':
        fail(where, 'expected: record <Name> <size> runtime=<type> header=<path> [same=<constant>] '
                    '[copy=scalars]')
    return {'name': words[1], 'size': int(words[2], 0), 'fields': [], 'asset': None, 'where': where, **attrs}


def parse_asset(record, words, where):
    asset = attributes(words[1:], where, ASSET_KEYS)
    if record['asset'] or not {'member', 'pool', 'kind', 'alias', 'label'} <= asset.keys() \
            or asset['alias'] not in ('inserted', 'completed') or asset.get('body', 'custom') != 'custom' \
            or asset.get('check', 'custom') != 'custom' or asset.get('slot', 'const') != 'const':
        fail(where, 'expected one line: asset member= pool= kind= alias=<inserted|completed> label= '
                    '[name=] [body=custom] [check=custom] [slot=const]')
    record['asset'] = {**asset, 'where': where}


def field_shape(words, where):
    """Return a field line's kind and its fixed-array dimensions, if any."""
    shape = re.fullmatch(r'(\w+)((?:\[[1-9]\d*\])*)', words[2]) if len(words) >= 3 else None
    fixed = ARRAYABLE + ('pointer', 'struct', 'pad', 'run')  # a custom body loads a fixed array of tokens or records
    if not shape or shape.group(1) not in KINDS or (shape.group(2) and shape.group(1) not in fixed):
        fail(where, f'expected: <offset> <field> <kind>[<n>]... [attributes]; kinds {sorted(KINDS)}, '
                    f'fixed arrays of {fixed}')
    return shape.group(1), [int(n) for n in re.findall(r'\d+', shape.group(2))]


# The attributes each kind takes after it; the others take none.
FIELD_ATTRIBUTES = {'bytes': {'count', 'terminated', 'paired', 'label'}, 'xstrings': {'count'},
                    'array': {'count', 'of', 'label'}, 'struct': {'of'}, 'pointer': {'asset'}, 'run': {'to'}}


def parse_field(words, where):
    kind, dims = field_shape(words, where)
    attrs = attributes(words[3:], where, FIELD_ATTRIBUTES.get(kind, set()), flags=('terminated', 'paired'))
    if (kind in COUNTED) != ('count' in attrs) or (kind in ('array', 'struct')) != ('of' in attrs):
        fail(where, f'count=<expression> is required for {COUNTED} and only for them, and of=<record> '
                    'for array and struct and only for them')
    if (kind == 'run') != ('to' in attrs) or (kind == 'run' and len(dims) != 1):
        fail(where, 'a run is run[<words>] to=<its last native member>, and to= is only for a run')
    if not re.fullmatch(r'[A-Za-z_]\w*(\.[A-Za-z_]\w*)*', words[1]):
        fail(where, 'a field is a member name, or a dotted path to a member of a nested native struct')
    return {'offset': int(words[0], 0), 'name': words[1], 'kind': kind, 'dims': dims,
            'count': attrs.get('count', ''), 'of': attrs.get('of'),
            'terminated': 'terminated' in attrs, 'paired': 'paired' in attrs, 'label': attrs.get('label'),
            'asset': attrs.get('asset'), 'to': attrs.get('to'), 'where': where}


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


def field_layout(field, width):
    """Return a field's size and alignment; width 1 is ILP32, 2 is 64-bit."""
    if field['kind'] == 'struct':
        nested = field['record']['fields']
        return (layout(nested, width)[1] * math.prod(field['dims']),
                max((field_layout(f, width)[1] for f in nested), default=1))
    align = KINDS[field['kind']][width]
    return align * math.prod(field['dims']), align


def layout(fields, width):
    """Natural-alignment offsets and size; width 1 is ILP32, 2 is 64-bit."""
    offsets, offset, biggest = [], 0, 1
    for field in fields:
        size, align = field_layout(field, width)
        offset = (offset + align - 1) // align * align
        offsets.append(offset)
        offset += size
        biggest = max(biggest, align)
    return offsets, (offset + biggest - 1) // biggest * biggest


def member(field):
    """Return the mirror member of a field: the last name of a dotted native path."""
    return field['name'].rsplit('.', 1)[-1]


def emit_mirror(record):
    """Return the lines of one record's disk32 mirror struct and its ONDISK_* asserts."""
    mirror = record['name'] + 'Disk32'
    out = [f'// Retail {record["name"]}: 0x{record["size"]:02X} bytes.', f'struct {mirror}', '{']
    for field in record['fields']:
        count = f' // count: {field["count"]}' if field['count'] else ''
        dims = ''.join(f'[{n}]' for n in field['dims'])
        mirror_type = KINDS[field['kind']][0].format(of=field['of'])
        out.append(f'    {mirror_type} {member(field)}{dims};{count}')
    out += ['};', f'ONDISK_SIZE({mirror}, 0x{record["size"]:02X});']
    out += [f'ONDISK_OFFSET({mirror}, {member(field)}, 0x{field["offset"]:02X});' for field in record['fields']]
    out += [f'static_assert(alignof({mirror}) == 4 && std::is_trivially_copyable_v<{mirror}>',
            f'    && std::is_standard_layout_v<{mirror}>);']
    if 'same' in record:
        out.append(f'static_assert(sizeof({mirror}) == {record["same"]});')
    return out + ['']


def emit(records, schema_name):
    out = [f'// Generated by scripts/gen_disk32.py from {schema_name}. Do not edit;',
           '// it is rebuilt from the schema and never committed (AGENTS.md rule 8).',
           '#pragma once', '', '#include <database/db_disk32.h>', '#include <universal/kisak_abi.h>']
    out += [f'#include <{header}>' for header in sorted({record['header'] for record in records})]
    out += ['', '#include <cstddef>', '#include <cstdint>', '#include <type_traits>', '', 'namespace disk32', '{']
    for record in records:
        out += emit_mirror(record)
    out += ['} // namespace disk32', '', '// The native struct each mirror converts into, from the same field list.']
    for record in records:
        (off32, size32), (off64, size64) = layout(record['fields'], 1), layout(record['fields'], 2)
        out.append(f'RUNTIME_SIZE({record["runtime"]}, 0x{size32:02X}, 0x{size64:02X});')
        out += [f'RUNTIME_OFFSET({record["runtime"]}, {field["name"]}, 0x{a:02X}, 0x{b:02X});'
                for field, a, b in zip(record['fields'], off32, off64) if field['kind'] != 'pad']
    return '\n'.join(out) + '\n'


# The loader steps. Each mirrors its 32-bit Load_* in db_load.cpp.
HEADER_SLOT = Template('''\
// $Name's header slot: the zero-extended disk32 token on entry, the native
// pointer on return.
inline void Load${Name}HeaderSlot(bool atStreamStart, $Slot **slot)
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
inline void Load${Name}Ptr(disk32::PointerToken token, $Slot **slot)
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
    {
        Drop("Failed to load fast-file $noun");
        return;
    }
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
inline void Load${Name}Ptr(disk32::PointerToken token, $Slot **slot)
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
        Drop("Failed to load fast-file $noun");
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
${pointers}${check}    DB_PopStreamPos();
    return true;
}
''')

FIXED_ARRAY = Template('''\
    static_assert(sizeof(out->$field) == sizeof(disk.$member)
        && std::is_same_v<std::remove_all_extents_t<decltype(out->$field)>,
                          std::remove_all_extents_t<decltype(disk.$member)>>);
    std::memcpy(out->$field, disk.$member, sizeof(disk.$member));
''')

# A run: the native members from its first through `to` sit together and
# hold no pointer, so they copy as one block of the retail words.
RUN = Template('''\
    static_assert(offsetof($Runtime, $to) + sizeof(out->$to) - offsetof($Runtime, $field) == sizeof(disk.$field));
    std::memcpy(reinterpret_cast<std::uint8_t *>(out) + offsetof($Runtime, $field), disk.$field,
                sizeof(disk.$field)); // Flawfinder: ignore (the assert sizes the native run)
''')

# copy=scalars: a record's scalars for a hand-written body to call.
COPY_SCALARS = Template('''\
// $Name's scalars from its mirror into the native record: each scalar (a bool
// as != 0) and fixed array, and null for each rawptr, whose bytes are no token.
inline void Copy${Name}Scalars(const disk32::${Name}Disk32 &disk, $Runtime *out)
{
${copies}}
''')

# check=custom: the 32-bit loader's acceptance rules beyond the layout.
CHECK_DECL = Template('''\
// $Name's check, hand-written in its TU: the 32-bit loader's rules for the
// converted record beyond its layout.
bool Check$Name(const $Runtime &record);

''')

CHECK_CALL = Template('''\
    if (!Check$Name(*out))
        return Drop("Invalid fast-file $noun");
''')

XSTRING = Template('''\
    if (!LoadXString(disk.$disk, &out->$field))
        return false;
''')

# asset=<family>: the referenced family's pointer step pushes the temp block
# itself, so the child streams where the 32-bit loader streams it.
ASSET_POINTER = Template('''\
    Load${Of}Ptr(disk.$disk.token, &out->$field);
''')

POINTER_DECL = Template('''\
inline void Load${Name}Ptr(disk32::PointerToken token, $Slot **slot);
''')

NAME_CHECK = Template('''\
    if (!out->$field)
        return Drop("Fast-file $noun has no name"); // the asset pool hashes it
''')

# As in the 32-bit loader, any non-null token means count elements follow
# inline; each element's strings follow all the elements.
ARRAY = Template('''\
    if (!disk.$field.token.isNull())
    {
        static_assert(std::is_same_v<decltype(out->$field), $Element *>);
        const auto count = static_cast<std::int32_t>(disk.$count);
        std::int32_t bytes = 0;
        std::uint8_t *const elements = DB_AllocStreamPos(3);
        if (!db::validation::CheckedArrayBytes(count, sizeof(disk32::${Of}Disk32), &bytes))
            return Drop("Invalid fast-file $label count");
        if (!StreamBytes(elements, bytes))
            return false;
        // An empty array still points at its stream position, as on x86.
        out->$field = count ? AllocNative<$Element>(count)
                            : reinterpret_cast<$Element *>(elements);
        if (!out->$field)
            return false;
        for (std::int32_t index = 0; index < count; ++index)
        {
            disk32::${Of}Disk32 element{};
            const std::size_t offset = static_cast<std::size_t>(index) * sizeof(element);
            std::memcpy(&element, elements + offset, sizeof(element));
            if (!Load${Of}Element(element, &out->$field[index]))
                return false;
        }
    }
''')

ELEMENT = Template('''\
// $Of's element conversion: one retail element of an array into its native
// twin, its strings loaded at the current stream position.
inline bool Load${Of}Element(const disk32::${Of}Disk32 &disk, $Element *out)
{
${scalars}${pointers}    return true;
}
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
    """Copy a scalar (a bool as != 0) or a fixed array, after checking its native type matches the mirror's."""
    name, mirror = field['name'], field.get('disk', member(field))
    if field['dims']:
        return FIXED_ARRAY.substitute(field=name, member=mirror)
    test = ' != 0' if field['kind'] == 'bool' else ''
    return f'    out->{name} = disk.{mirror}{test};\n'


def bytes_count(record, field):
    """A generated body's terminated bytes field: its count field and the constant added to it."""
    kinds = {other['name']: other['kind'] for other in record['fields']}
    count = re.fullmatch(r'(\w+)(?:\+(\d+))?', field['count'])
    if field['kind'] != 'bytes' or not field['terminated'] or not field['label'] or not count \
            or kinds.get(count.group(1)) not in ('i32', 'u32'):
        fail(field['where'], 'a generated body loads scalars, xstrings and terminated bytes '
                             f'(count=<i32 field>[+<n>] terminated label=<noun>), not this {field["kind"]} field')
    return count.group(1), count.group(2) or '0'


def body_step(record, field, records):
    """The generated body's statements for one pointer field."""
    if field['kind'] == 'array':
        element = element_record(records, record, field)
        return ARRAY.substitute(field=field['name'], count=array_count(record, field), Of=element['name'],
                                Element=element['runtime'], label=field['label'])
    if field['kind'] == 'pointer':
        return ASSET_POINTER.substitute(Of=asset_record(records, field)['name'], disk=field['disk'],
                                        field=field['name'])
    if field['kind'] == 'xstring':
        step = XSTRING.substitute(field=field['name'], disk=field['disk'])
        if field['name'] == record['asset'].get('name'):
            step += NAME_CHECK.substitute(field=field['name'], noun=noun(record['asset']['label']))
        return step
    count, extra = bytes_count(record, field)
    facts = dict(field=field['name'], count=count, extra=extra, label=field['label'],
                 noun=noun(field['label']), owner=noun(record['asset']['label']))
    return (PAIRED.substitute(facts) if field['paired'] else '') + TERMINATED_BYTES.substitute(facts)


def asset_record(records, field):
    """Return the record with an asset line that a pointer field's asset= names."""
    target = next((other for other in records if other['name'] == field['asset'] and other['asset']), None)
    if not target:
        fail(field['where'], 'a generated body loads a pointer only with asset=<a record with an asset line>')
    return target


def element_record(records, record, field):
    """Return the earlier, asset-free record of scalars, fixed arrays and xstrings an array's of= names."""
    earlier = {other['name']: other for other in records[:records.index(record)]}
    element = earlier.get(field['of'])
    if not element or element['asset'] or not field['label'] \
            or any(f['kind'] not in SCALARS + ('xstring',) or '.' in f['name'] for f in element['fields']):
        fail(field['where'], f'of={field["of"]} must name an earlier record without an asset line whose fields '
                             'are scalars, fixed arrays and xstrings, and the array needs label=<noun>')
    return element


def array_count(record, field):
    kinds = {other['name']: other['kind'] for other in record['fields']}
    if kinds.get(field['count']) not in ('i32', 'u32'):
        fail(field['where'], f'count={field["count"]} must name an i32 or u32 field of {record["name"]}')
    return field['count']


def emit_element(element):
    scalars = ''.join(scalar_copy(field) for field in element['fields'] if field['kind'] in SCALARS)
    pointers = ''.join(XSTRING.substitute(field=field['name'], disk=field['name']) for field in element['fields']
                       if field['kind'] == 'xstring')
    return ELEMENT.substitute(Of=element['name'], Element=element['runtime'], scalars=scalars, pointers=pointers)


def emit_copy_scalars(record):
    copies = ''.join(scalar_copy(field) for field in record['fields'] if field['kind'] in SCALARS)
    copies += ''.join(RUN.substitute(Runtime=record['runtime'], field=field['name'], to=field['to'])
                      for field in record['fields'] if field['kind'] == 'run')
    copies += ''.join(f'    out->{field["name"]} = nullptr;\n'
                      for field in record['fields'] if field['kind'] == 'rawptr')
    if 'disk.' not in copies:
        copies = '    static_cast<void>(disk); // a record of no scalars\n' + copies
    return COPY_SCALARS.substitute(Name=record['name'], Runtime=record['runtime'], copies=copies)


def flat_fields(fields, native='', disk=''):
    """Each field with its native and mirror paths; a struct field yields its record's fields under it."""
    for field in fields:
        if field['kind'] == 'struct':
            yield from flat_fields(field['record']['fields'], f'{native}{field["name"]}.', f'{disk}{member(field)}.')
        else:
            yield {**field, 'name': native + field['name'], 'disk': disk + member(field)}


def check_generated_body(record):
    """Fail unless a generated body can load the record: a name= xstring and no dotted field."""
    asset = record['asset']
    if {field['name']: field['kind'] for field in record['fields']}.get(asset.get('name')) != 'xstring':
        fail(asset['where'], 'a generated body needs name=<the xstring the pool hashes>')
    if any('.' in field['name'] or field['kind'] == 'run' for field in record['fields']):
        fail(asset['where'], 'a generated body loads top-level members; a dotted field or run needs body=custom')
    if any(field['kind'] not in SCALARS + ('xstring', 'pointer', 'struct') for field in flat_fields(record['fields'])
           if '.' in field['name']):
        fail(asset['where'], 'a generated body loads scalars, xstrings and asset pointers from a struct field')


def emit_body(record, records):
    asset = record['asset']
    check_generated_body(record)
    fields = list(flat_fields(record['fields']))
    scalars = ''.join(scalar_copy(field) for field in fields if field['kind'] in SCALARS)
    pointers = ''.join(body_step(record, field, records) for field in fields if field['kind'] not in SCALARS)
    facts = dict(Name=record['name'], Runtime=record['runtime'], noun=noun(asset['label']))
    check = CHECK_CALL.substitute(facts) if 'check' in asset else ''
    body = FLAT_BODY.substitute(facts, scalars=scalars, pointers=pointers, check=check)
    return (CHECK_DECL.substitute(facts) if 'check' in asset else '') + body


def emit_family(record, records):
    asset = record['asset']
    inserted, custom = asset['alias'] == 'inserted', 'body' in asset
    if not (custom or inserted):
        fail(asset['where'], 'alias=completed takes body=custom: its native storage is family-specific')
    if custom and ('name' in asset or 'check' in asset):
        fail(asset['where'], 'name= and check= apply to a generated body; a custom body does its own checks')
    facts = dict(Name=record['name'], Runtime=record['runtime'], Slot=slot_type(record), kind=asset['kind'],
                 member=asset['member'], pool=asset['pool'], label=asset['label'], noun=noun(asset['label']))
    # COMPLETED_PTR declares its own custom body.
    body = [INSERTED_BODY.substitute(facts) if custom else emit_body(record, records)] if inserted else []
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
           '#include <database/db_validation.h>', '', '#include <cstdint>', '#include <cstring>',
           '#include <type_traits>', '',
           'namespace db::disk32_load', '{']
    elements = {field['of'] for record in records for field in record['fields'] if field['kind'] == 'array'}
    out += pointer_declarations(records)
    for record in records:
        if record['name'] in elements:
            out.append(emit_element(record))
        if 'copy' in record:
            out.append(emit_copy_scalars(record))
        if record['asset']:
            out += emit_family(record, records)
    out += ['} // namespace db::disk32_load', '', '#endif // KISAK_ARCH_64BIT']
    return '\n'.join(out) + '\n'


def pointer_declarations(records):
    """Declare each pointer step an asset= field names, so schema order stays free."""
    named = {asset_record(records, field)['name'] for record in records for field in record['fields']
             if field['asset']}
    return [POINTER_DECL.substitute(Name=r['name'], Slot=slot_type(r)) for r in records if r['name'] in named]


def slot_type(record):
    """The pointee of a family's header slot: its native struct, const with slot=const."""
    return ('const ' if 'slot' in record['asset'] else '') + record['runtime']


def write(output, text):
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)


def resolve_structs(records):
    """Link each struct field to the earlier, asset-free record its of= names."""
    for index, record in enumerate(records):
        earlier = {other['name']: other for other in records[:index] if not other['asset']}
        for field in record['fields']:
            if field['kind'] == 'struct' and field['of'] not in earlier:
                fail(field['where'], f'of={field["of"]} must name an earlier record without an asset line')
            field['record'] = earlier.get(field['of'])


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: gen_disk32.py <schema> <mirrors header> <loaders header>')
    schema, mirrors, loaders = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    records = parse(schema, [])
    if not records:
        fail(schema, 'no records')
    names = [record['name'] for record in records]
    resolve_structs(records)
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
