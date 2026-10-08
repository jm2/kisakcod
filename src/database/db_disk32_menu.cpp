#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/24-menu.schema
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

// Menu (menuDef_t), a wave-4 parse family (docs/design/FASTFILE_LOADER.md):
// only the client's UI reads menus. The header slot and the inserted pointer
// step are generated from its schema entry; the record body below is custom.
// It mirrors Load_menuDef_t in db_load.cpp: the 284-byte record streams into
// the temp block, then with block 4 pushed its window (name, group,
// background material), its strings, its key-handler chain, its
// statements, whose expression entries convert into zone-lifetime native
// storage, and its items, which load the same way, with their type data.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using Disk = disk32::MenuDefDisk32;

// Load_windowDef_t: the scalars, then the name, group and background.
// record: where the window streamed in block 4, or null for the menu's own
// window, which streams in the temp block.
bool LoadWindow(const disk32::WindowDisk32 &disk, const std::uint8_t *record, windowDef_t *out)
{
    CopyWindowScalars(disk, out);
    if (!LoadXString(disk.name, &out->name) || !LoadXString(disk.group, &out->group))
        return false;
    out->background = nullptr;
    if (record)
        LoadMaterialPtr(disk.background.token, &out->background, SlotOf(record, disk, disk.background));
    else
        LoadMaterialPtr(disk.background.token, &out->background);
    return true;
}

// The next 4-aligned record at the current position, into its mirror.
template <typename Disk32>
bool StreamRecord(Disk32 &disk, const std::uint8_t **at = nullptr)
{
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (at)
        *at = record;
    return true;
}

// One native T in zone-lifetime storage, zeroed (padding too: native storage
// starts as junk).
template <typename T>
T *AllocZeroed()
{
    T *const native = AllocNative<T>(1);
    if (native)
        std::memset(native, 0, sizeof(T));
    return native;
}

// Load_ItemKeyHandler: any non-null token means a 12-byte handler follows
// 4-aligned, then its action; its next token says whether another follows.
// The chain loads in a loop, so its length costs no stack.
bool LoadKeyHandlers(disk32::PointerToken token, ItemKeyHandler **out)
{
    *out = nullptr;
    ItemKeyHandler **slot = out;
    while (!token.isNull())
    {
        disk32::ItemKeyHandlerDisk32 disk{};
        ItemKeyHandler *const handler = StreamRecord(disk) ? AllocZeroed<ItemKeyHandler>() : nullptr;
        if (!handler)
            return false;
        CopyItemKeyHandlerScalars(disk, handler);
        if (!LoadXString(disk.action, &handler->action))
            return false;
        *slot = handler;
        slot = &handler->next;
        token = disk.next.token;
    }
    return true;
}

// Load_expressionEntry: a 12-byte entry 4-aligned. An operator (type 0) is
// within the build's operator range; an operand's string loads, and its int
// or float bits copy.
bool LoadEntry(expressionEntry **out)
{
    disk32::ExpressionEntryDisk32 disk{};
    expressionEntry *const entry = StreamRecord(disk) ? AllocZeroed<expressionEntry>() : nullptr;
    if (!entry)
        return false;
    entry->type = disk.type;
    *out = entry;
    if (!disk.type)
    {
        if (disk.data.dataType < 0 || disk.data.dataType >= NUM_OPERATORS)
            return Drop("Invalid fast-file menu expression operator");
        entry->data.op = static_cast<operationEnum>(disk.data.dataType);
        return true;
    }
    Operand &operand = entry->data.operand;
    operand.dataType = static_cast<expDataType>(disk.data.dataType);
    if (operand.dataType == VAL_STRING)
        return LoadXString(disk.data.string, &operand.internals.string);
    std::memcpy(&operand.internals.intVal, &disk.data.string, sizeof(operand.internals.intVal));
    return true;
}

// The entries the tokens name, in order, each non-null and following in turn.
bool LoadEntries(const std::uint8_t *tokens, std::int32_t count, expressionEntry **entries)
{
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::PointerToken token{};
        std::memcpy(&token, tokens + static_cast<std::size_t>(index) * sizeof(token), sizeof(token));
        entries[index] = nullptr;
        if (token.isNull())
            return Drop("Fast-file menu statement has a null entry");
        if (!LoadEntry(&entries[index]))
            return false;
    }
    return true;
}

// Load_statement: any non-null entries token means numEntries entry tokens
// follow 4-aligned, each non-null, each entry following in turn. A statement
// with entries has the tokens.
bool LoadStatement(const disk32::StatementDisk32 &disk, statement_s *out)
{
    CopyStatementScalars(disk, out);
    out->entries = nullptr;
    const std::int32_t count = disk.numEntries;
    std::int32_t bytes = 0;
    if ((count > 0 && disk.entries.token.isNull())
        || !db::validation::CheckedArrayBytes(count, sizeof(disk32::PointerToken), &bytes))
    {
        return Drop("Invalid fast-file menu statement entry count");
    }
    if (disk.entries.token.isNull())
        return true;
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    if (!StreamBytes(tokens, bytes))
        return false;
    // An empty statement still points at its stream position, as on x86.
    expressionEntry **const entries =
        count ? AllocNative<expressionEntry *>(count) : reinterpret_cast<expressionEntry **>(tokens);
    if (!entries)
        return false;
    out->entries = entries;
    return LoadEntries(tokens, count, entries);
}

// Load_itemDefData_t's types that hold data: a list box (6), an edit field
// (0, 4, 9, 10, 11, 14, 16, 17 and 18), a multi-value (12) or an enum dvar
// (13).
constexpr std::int32_t kMaxListBoxColumns = std::extent_v<decltype(listBoxDef_s::columnInfo)>;
constexpr std::int32_t kMaxMultiValues = std::extent_v<decltype(multiDef_s::dvarList)>;
constexpr std::int32_t kItemTypeListBox = 6;
constexpr std::int32_t kItemTypeMulti = 12;
constexpr std::int32_t kItemTypeEnumDvar = 13;

bool TypeHasData(std::int32_t type)
{
    constexpr std::uint32_t kTypesWithData = 1u << 0 | 1u << 4 | 1u << 6 | 1u << 9 | 1u << 10 | 1u << 11 | 1u << 12
        | 1u << 13 | 1u << 14 | 1u << 16 | 1u << 17 | 1u << 18;
    return type >= 0 && type < 32 && (kTypesWithData >> type & 1u);
}

// Load_listBoxDef_t: a 340-byte list box 4-aligned, then its double-click
// script and select icon. Its columns number at most 16, columnInfo's size.
bool LoadListBox(listBoxDef_s **out)
{
    disk32::ListBoxDisk32 disk{};
    const std::uint8_t *record = nullptr;
    listBoxDef_s *const listBox = StreamRecord(disk, &record) ? AllocZeroed<listBoxDef_s>() : nullptr;
    if (!listBox)
        return false;
    *out = listBox;
    CopyListBoxScalars(disk, listBox);
    if (disk.numColumns > kMaxListBoxColumns)
        return Drop("Invalid fast-file menu list-box column count");
    if (!LoadXString(disk.doubleClick, &listBox->doubleClick))
        return false;
    LoadMaterialPtr(disk.selectIcon.token, &listBox->selectIcon, SlotOf(record, disk, disk.selectIcon));
    return true;
}

// Load_editFieldDef_t: a 32-byte edit field 4-aligned, which holds no pointer.
bool LoadEditField(editFieldDef_s **out)
{
    disk32::EditFieldDisk32 disk{};
    editFieldDef_s *const editField = StreamRecord(disk) ? AllocZeroed<editFieldDef_s>() : nullptr;
    if (!editField)
        return false;
    *out = editField;
    CopyEditFieldScalars(disk, editField);
    return true;
}

// Load_multiDef_t: a 392-byte multi-value 4-aligned, then its 32 dvar names
// and its 32 strings, in order. Its values number at most 32, its arrays'
// size.
bool LoadMulti(multiDef_s **out)
{
    disk32::MultiDefDisk32 disk{};
    multiDef_s *const multi = StreamRecord(disk) ? AllocZeroed<multiDef_s>() : nullptr;
    if (!multi)
        return false;
    *out = multi;
    CopyMultiDefScalars(disk, multi);
    if (disk.count > kMaxMultiValues)
        return Drop("Invalid fast-file menu multi-value count");
    for (int index = 0; index < kMaxMultiValues; ++index)
    {
        if (!LoadXString(disk32::Ptr32<const char>{disk.dvarList[index].token}, &multi->dvarList[index]))
            return false;
    }
    for (int index = 0; index < kMaxMultiValues; ++index)
    {
        if (!LoadXString(disk32::Ptr32<const char>{disk.dvarStr[index].token}, &multi->dvarStr[index]))
            return false;
    }
    return true;
}

// Load_itemDefData_t: by the item's type, any non-null token means a list
// box, edit field or multi-value follows; an enum dvar's name is a string
// token. Any other type's data is no token, and stays null.
bool LoadTypeData(const disk32::ItemDisk32 &disk, itemDef_s *out)
{
    const disk32::PointerToken token = disk.typeData.token;
    out->typeData.data = nullptr;
    if (!TypeHasData(disk.type) || token.isNull())
        return true;
    switch (disk.type)
    {
    case kItemTypeListBox:
        return LoadListBox(&out->typeData.listBox);
    case kItemTypeMulti:
        return LoadMulti(&out->typeData.multi);
    case kItemTypeEnumDvar:
        return LoadXString(disk32::Ptr32<const char>{token}, &out->typeData.enumDvarName);
    default:
        return LoadEditField(&out->typeData.editField);
    }
}

// Load_itemDef_t: the scalars and window, the strings, the key handlers, the
// focus sound (Sound's step), the type data, then the statements.
bool LoadItem(const disk32::ItemDisk32 &disk, const std::uint8_t *record, itemDef_s *out)
{
    CopyItemScalars(disk, out);
    const std::pair<disk32::Ptr32<const char>, const char **> strings[] = {
        {disk.text, &out->text}, {disk.mouseEnterText, &out->mouseEnterText},
        {disk.mouseExitText, &out->mouseExitText}, {disk.mouseEnter, &out->mouseEnter},
        {disk.mouseExit, &out->mouseExit}, {disk.action, &out->action}, {disk.onAccept, &out->onAccept},
        {disk.onFocus, &out->onFocus}, {disk.leaveFocus, &out->leaveFocus}, {disk.dvar, &out->dvar},
        {disk.dvarTest, &out->dvarTest}};
    if (!LoadWindow(disk.window, SlotOf(record, disk, disk.window), &out->window))
        return false;
    for (const auto &[field, slot] : strings)
    {
        if (!LoadXString(field, slot))
            return false;
    }
    if (!LoadKeyHandlers(disk.onKey.token, &out->onKey) || !LoadXString(disk.enableDvar, &out->enableDvar))
        return false;
    LoadSndAliasListPtr(disk.focusSound.token, &out->focusSound, SlotOf(record, disk, disk.focusSound));
    if (!LoadTypeData(disk, out))
        return false;
    const std::pair<const disk32::StatementDisk32 *, statement_s *> statements[] = {
        {&disk.visibleExp, &out->visibleExp}, {&disk.textExp, &out->textExp},
        {&disk.materialExp, &out->materialExp}, {&disk.rectXExp, &out->rectXExp}, {&disk.rectYExp, &out->rectYExp},
        {&disk.rectWExp, &out->rectWExp}, {&disk.rectHExp, &out->rectHExp},
        {&disk.forecolorAExp, &out->forecolorAExp}};
    for (const auto &[field, statement] : statements)
    {
        if (!LoadStatement(*field, statement))
            return false;
    }
    return true;
}

// The items the tokens name, in order, each non-null, each 372-byte item
// following 4-aligned in turn, converted into native storage.
bool LoadItemArray(const std::uint8_t *tokens, std::int32_t count, itemDef_s **items)
{
    for (std::int32_t index = 0; index < count; ++index)
    {
        disk32::PointerToken token{};
        std::memcpy(&token, tokens + static_cast<std::size_t>(index) * sizeof(token), sizeof(token));
        items[index] = nullptr;
        if (token.isNull())
            return Drop("Fast-file menu has a null item");
        disk32::ItemDisk32 disk{};
        const std::uint8_t *record = nullptr;
        items[index] = StreamRecord(disk, &record) ? AllocZeroed<itemDef_s>() : nullptr;
        if (!items[index] || !LoadItem(disk, record, items[index]))
            return false;
    }
    return true;
}

// Load_itemDef_ptrArray: any non-null items token means itemCount item
// tokens follow 4-aligned.
bool LoadItems(disk32::PointerToken token, std::int32_t count, itemDef_s ***out)
{
    *out = nullptr;
    if (token.isNull())
        return true;
    std::int32_t bytes = 0;
    if (!db::validation::CheckedArrayBytes(count, sizeof(disk32::PointerToken), &bytes))
        return Drop("Invalid fast-file menu item count");
    std::uint8_t *const tokens = DB_AllocStreamPos(3);
    if (!StreamBytes(tokens, bytes))
        return false;
    // An empty array still points at its stream position, as on x86.
    itemDef_s **const items = count ? AllocNative<itemDef_s *>(count) : reinterpret_cast<itemDef_s **>(tokens);
    if (!items)
        return false;
    *out = items;
    return LoadItemArray(tokens, count, items);
}

// Load_menuDef_t's first half: the window, font, open, close and escape
// scripts, then the key handlers. The pool hashes the window's name.
bool LoadMenuHead(const Disk &disk, menuDef_t *out)
{
    if (!LoadWindow(disk.window, nullptr, &out->window))
        return false;
    if (!out->window.name)
        return Drop("Fast-file menu has no name");
    return LoadXString(disk.font, &out->font) && LoadXString(disk.onOpen, &out->onOpen)
        && LoadXString(disk.onClose, &out->onClose) && LoadXString(disk.onESC, &out->onESC)
        && LoadKeyHandlers(disk.onKey.token, &out->onKey);
}

// Its second half: the visibility statement, the binding and sound names,
// the rectangle statements, then the items.
bool LoadMenuTail(const Disk &disk, menuDef_t *out)
{
    return LoadStatement(disk.visibleExp, &out->visibleExp)
        && LoadXString(disk.allowedBinding, &out->allowedBinding) && LoadXString(disk.soundName, &out->soundName)
        && LoadStatement(disk.rectXExp, &out->rectXExp) && LoadStatement(disk.rectYExp, &out->rectYExp)
        && LoadItems(disk.items.token, disk.itemCount, &out->items);
}
} // namespace

// The record at the temp block's position, its item count, then its parts
// with block 4 pushed. A menu with items has the token.
bool LoadMenuDef(menuDef_t *out)
{
    Disk disk{};
    std::uint8_t *const record = DB_GetStreamPos();
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
    if (disk.itemCount < 0 || (disk.itemCount > 0 && disk.items.token.isNull()))
        return Drop("Invalid fast-file menu item count");
    CopyMenuDefScalars(disk, out);
    DB_PushStreamPos(kVirtualBlock);
    if (!LoadMenuHead(disk, out) || !LoadMenuTail(disk, out))
        return false;
    DB_PopStreamPos();
    return true;
}

} // namespace db::disk32_load

void __cdecl DB_LoadMenuDefPtrDisk32(bool atStreamStart, menuDef_t **slot)
{
    db::disk32_load::LoadMenuDefHeaderSlot(atStreamStart, slot);
}

#endif // KISAK_ARCH_64BIT
