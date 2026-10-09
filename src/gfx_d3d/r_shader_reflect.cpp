#include "r_shader_reflect.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr std::uint32_t kVertexVersionType = 0xFFFE0000u;
constexpr std::uint32_t kPixelVersionType = 0xFFFF0000u;
constexpr std::uint32_t kVersionTypeMask = 0xFFFF0000u;
constexpr std::uint32_t kOpcodeMask = 0x0000FFFFu;
constexpr std::uint32_t kCommentOpcode = 0x0000FFFEu;
constexpr std::uint32_t kEndToken = 0x0000FFFFu;
constexpr std::uint32_t kCommentLengthMask = 0x7FFF0000u;
constexpr std::uint32_t kCommentLengthShift = 16;
constexpr std::uint32_t kInstructionLengthMask = 0x0F000000u;
constexpr std::uint32_t kInstructionLengthShift = 24;
constexpr std::uint32_t kCtabFourCC = 0x42415443u; // 'C' 'T' 'A' 'B'

// D3DXSHADER_CONSTANTTABLE, _CONSTANTINFO and _TYPEINFO sizes and the byte
// offsets of the fields read from them.
constexpr std::uint32_t kTableHeaderSize = 28;
constexpr std::uint32_t kTableSizeField = 0;
constexpr std::uint32_t kTableConstantsField = 12;
constexpr std::uint32_t kTableConstantInfoField = 16;
constexpr std::uint32_t kConstantInfoSize = 20;
constexpr std::uint32_t kInfoNameField = 0;
constexpr std::uint32_t kInfoRegisterSetField = 4;
constexpr std::uint32_t kInfoRegisterIndexField = 6;
constexpr std::uint32_t kInfoRegisterCountField = 8;
constexpr std::uint32_t kInfoTypeInfoField = 12;
constexpr std::uint32_t kTypeInfoSize = 16;
constexpr std::uint32_t kTypeClassField = 0;
constexpr std::uint32_t kTypeTypeField = 2;

// Unaligned little-endian field reads; every caller has bounds-checked the
// field against the payload first.
template <typename T>
T ReadField(const std::uint8_t *bytes, std::uint32_t offset)
{
    T value{};
    std::copy_n(bytes + offset, sizeof(value), reinterpret_cast<std::uint8_t *>(&value));
    return value;
}

std::uint32_t ReadU32(const std::uint8_t *bytes, std::uint32_t offset)
{
    return ReadField<std::uint32_t>(bytes, offset);
}

std::uint16_t ReadU16(const std::uint8_t *bytes, std::uint32_t offset)
{
    return ReadField<std::uint16_t>(bytes, offset);
}

// [offset, offset + length) lies inside a payload of `size` bytes.
bool RangeInside(std::uint32_t offset, std::uint32_t length, std::uint32_t size)
{
    return offset <= size && length <= size - offset;
}

bool NameInside(const std::uint8_t *data, std::uint32_t offset, std::uint32_t size)
{
    return offset < size && std::memchr(data + offset, '\0', size - offset) != nullptr;
}

bool ConstantTableValid(const std::uint8_t *data, std::uint32_t size, std::uint32_t *constantCount)
{
    if (size < kTableHeaderSize || ReadU32(data, kTableSizeField) != kTableHeaderSize)
        return false;

    const std::uint32_t count = ReadU32(data, kTableConstantsField);
    const std::uint32_t infoOffset = ReadU32(data, kTableConstantInfoField);
    if (count > size / kConstantInfoSize || !RangeInside(infoOffset, count * kConstantInfoSize, size))
        return false;

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const std::uint32_t info = infoOffset + index * kConstantInfoSize;
        if (!NameInside(data, ReadU32(data, info + kInfoNameField), size)
            || !RangeInside(ReadU32(data, info + kInfoTypeInfoField), kTypeInfoSize, size))
        {
            return false;
        }
    }
    *constantCount = count;
    return true;
}

// A vs or ps version token's major version; 0 for any other token.
std::uint32_t VersionMajor(std::uint32_t version)
{
    const std::uint32_t versionType = version & kVersionTypeMask;
    if (versionType != kVertexVersionType && versionType != kPixelVersionType)
        return 0;
    return (version >> 8) & 0xFFu;
}

bool VersionSupported(std::uint32_t version)
{
    const std::uint32_t major = VersionMajor(version);
    return major >= 2 && major <= 3;
}

// Shader model 1 instruction tokens carry no length (bits 24-27 are
// reserved), so a 1.x program is read as D3D9ShaderBytecodeValid reads it:
// the comment blocks after the version token, at least one instruction, and
// the end token as the last dword. Calls visit for those comments only; the
// compiler puts a 1.x program's constant table there.
template <typename Visit>
bool WalkLeadingComments(const std::uint32_t *program, std::uint32_t dwordCount, Visit &&visit)
{
    if (!program || dwordCount < 2 || VersionMajor(program[0]) != 1)
        return false;

    std::uint32_t cursor = 1;
    // A comment's opcode is never the end token's, so this stops at END too.
    while (cursor < dwordCount && (program[cursor] & kOpcodeMask) == kCommentOpcode)
    {
        const std::uint32_t payloadDwords = (program[cursor] & kCommentLengthMask) >> kCommentLengthShift;
        if (payloadDwords > dwordCount - cursor - 1 || !visit(program[cursor], &program[cursor + 1], payloadDwords))
            return false;
        cursor += payloadDwords + 1;
    }
    return cursor < dwordCount - 1 && program[cursor] != kEndToken && program[dwordCount - 1] == kEndToken;
}

// Calls visit(token, payload, payloadDwords) for each comment and instruction
// of a vs/ps 2.x or 3.x program. True only when every visit returned true and
// the stream ends in its end token at dwordCount.
template <typename Visit>
bool WalkProgram(const std::uint32_t *program, std::uint32_t dwordCount, Visit &&visit)
{
    if (!program || dwordCount < 2 || !VersionSupported(program[0]))
        return false;

    std::uint32_t cursor = 1;
    while (cursor < dwordCount)
    {
        const std::uint32_t token = program[cursor];
        if (token == kEndToken)
            return cursor + 1 == dwordCount;
        if ((token & kOpcodeMask) == kEndToken)
            return false;

        const std::uint32_t payloadDwords = (token & kOpcodeMask) == kCommentOpcode
            ? (token & kCommentLengthMask) >> kCommentLengthShift
            : (token & kInstructionLengthMask) >> kInstructionLengthShift;
        if (payloadDwords > dwordCount - cursor - 1 || !visit(token, &program[cursor + 1], payloadDwords))
            return false;
        cursor += payloadDwords + 1;
    }
    return false;
}

// Takes the first CTAB comment of the stream; false only for a CTAB that does
// not validate, which fails the whole lookup.
bool VisitConstantTable(
    std::uint32_t token,
    const std::uint32_t *payload,
    std::uint32_t payloadDwords,
    ShaderConstantTableView *found)
{
    if ((token & kOpcodeMask) != kCommentOpcode || found->data || payloadDwords < 1 || payload[0] != kCtabFourCC)
        return true;

    const auto *data = reinterpret_cast<const std::uint8_t *>(&payload[1]);
    const std::uint32_t size = (payloadDwords - 1) * 4;
    std::uint32_t constantCount = 0;
    if (!ConstantTableValid(data, size, &constantCount))
        return false;
    *found = { data, size, constantCount };
    return true;
}
} // namespace

bool R_ShaderFindConstantTable(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    ShaderConstantTableView *table)
{
    if (!table || !program || dwordCount < 2)
        return false;

    ShaderConstantTableView found{};
    auto visit = [&found](std::uint32_t token, const std::uint32_t *payload, std::uint32_t payloadDwords) {
        return VisitConstantTable(token, payload, payloadDwords, &found);
    };
    const bool walked = VersionMajor(program[0]) == 1
        ? WalkLeadingComments(program, dwordCount, visit)
        : WalkProgram(program, dwordCount, visit);
    if (!walked || !found.data)
        return false;
    *table = found;
    return true;
}

bool R_ShaderGetConstantDesc(
    const ShaderConstantTableView &table,
    std::uint32_t index,
    ShaderConstantDesc *desc)
{
    if (!table.data || !desc || index >= table.constantCount)
        return false;

    const std::uint8_t *data = table.data;
    const std::uint32_t info = ReadU32(data, kTableConstantInfoField) + index * kConstantInfoSize;
    const std::uint32_t typeInfo = ReadU32(data, info + kInfoTypeInfoField);
    desc->name = reinterpret_cast<const char *>(data + ReadU32(data, info + kInfoNameField));
    desc->registerSet = ReadU16(data, info + kInfoRegisterSetField);
    desc->registerIndex = ReadU16(data, info + kInfoRegisterIndexField);
    desc->registerCount = ReadU16(data, info + kInfoRegisterCountField);
    desc->typeClass = ReadU16(data, typeInfo + kTypeClassField);
    desc->typeType = ReadU16(data, typeInfo + kTypeTypeField);
    return true;
}
