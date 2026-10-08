#include "r_shader_reflect.h"

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

std::uint32_t ReadU32(const std::uint8_t *bytes, std::uint32_t offset)
{
    std::uint32_t value;
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

std::uint16_t ReadU16(const std::uint8_t *bytes, std::uint32_t offset)
{
    std::uint16_t value;
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
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
} // namespace

bool R_ShaderFindConstantTable(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    ShaderConstantTableView *table)
{
    if (!program || !table || dwordCount < 2)
        return false;

    const std::uint32_t version = program[0];
    const std::uint32_t major = (version >> 8) & 0xFFu;
    const std::uint32_t versionType = version & kVersionTypeMask;
    if ((versionType != kVertexVersionType && versionType != kPixelVersionType) || major < 2 || major > 3)
        return false;

    ShaderConstantTableView found{};
    bool haveTable = false;
    std::uint32_t cursor = 1;
    while (cursor < dwordCount)
    {
        const std::uint32_t token = program[cursor];
        if (token == kEndToken)
        {
            if (!haveTable || cursor + 1 != dwordCount)
                return false;
            *table = found;
            return true;
        }

        if ((token & kOpcodeMask) == kEndToken)
            return false;
        const bool comment = (token & kOpcodeMask) == kCommentOpcode;
        const std::uint32_t payloadDwords = comment
            ? (token & kCommentLengthMask) >> kCommentLengthShift
            : (token & kInstructionLengthMask) >> kInstructionLengthShift;
        if (payloadDwords > dwordCount - cursor - 1)
            return false;

        if (comment && !haveTable && payloadDwords >= 1 && program[cursor + 1] == kCtabFourCC)
        {
            const auto *data = reinterpret_cast<const std::uint8_t *>(&program[cursor + 2]);
            const std::uint32_t size = (payloadDwords - 1) * 4;
            std::uint32_t constantCount = 0;
            if (!ConstantTableValid(data, size, &constantCount))
                return false;
            found = { data, size, constantCount };
            haveTable = true;
        }
        cursor += payloadDwords + 1;
    }
    return false;
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
