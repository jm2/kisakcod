#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_loaders.h> // generated from disk32/24-menu.schema
#include <database/db_validation.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Menu (menuDef_t), a wave-4 parse family (docs/design/FASTFILE_LOADER.md):
// only the client's UI reads menus. The header slot and the inserted pointer
// step are generated from its schema entry; the record body below is custom.
// It mirrors Load_menuDef_t in db_load.cpp: the 284-byte record streams into
// the temp block, then with block 4 pushed its window (name, group,
// background material), its strings, its key-handler chain, and its
// statements, whose expression entries convert into zone-lifetime native
// storage. Items do not load yet, so a menu that names any fails closed.
// Frames hold no destructors, since a production ERR_DROP longjmps out.
namespace db::disk32_load
{
namespace
{
using Disk = disk32::MenuDefDisk32;

// Load_windowDef_t: the scalars, then the name, group and background.
bool LoadWindow(const disk32::WindowDisk32 &disk, windowDef_t *out)
{
    CopyWindowScalars(disk, out);
    if (!LoadXString(disk.name, &out->name) || !LoadXString(disk.group, &out->group))
        return false;
    out->background = nullptr;
    LoadMaterialPtr(disk.background.token, &out->background);
    return true;
}

// The next 4-aligned record at the current position, into its mirror.
template <typename Disk32>
bool StreamRecord(Disk32 &disk)
{
    std::uint8_t *const record = DB_AllocStreamPos(3);
    if (!StreamBytes(record, static_cast<std::int32_t>(sizeof(disk))))
        return false;
    std::memcpy(&disk, record, sizeof(disk));
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

// Load_expressionEntry: a 12-byte entry 4-aligned. An operator (type 0)
// must name one the evaluator defines; an operand's string loads, and its
// int or float bits copy.
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
// follow 4-aligned, each non-null, each entry following in turn. The
// evaluator reads every entry, so a statement with entries has the tokens.
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

// The items, which do not load yet.
bool LoadItems(disk32::PointerToken token, itemDef_s ***out)
{
    *out = nullptr;
    return token.isNull() || Drop("Fast-file menu items have no 64-bit loader yet");
}

// Load_menuDef_t's first half: the window, font, open, close and escape
// scripts, then the key handlers. The pool hashes the window's name.
bool LoadMenuHead(const Disk &disk, menuDef_t *out)
{
    if (!LoadWindow(disk.window, &out->window))
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
        && LoadItems(disk.items.token, &out->items);
}
} // namespace

// The record at the temp block's position, its item count, then its parts
// with block 4 pushed. Load_MenuAsset reads every item, so a menu with items
// has the token.
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
