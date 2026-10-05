// db_disk32_menu_tests.cpp: the 64-bit Menu loader (NOW row 12) on hand-built
// disk32 zone images (disk32_fixture.hpp), with Material's and Sound's real
// steps for the backgrounds and focus sounds. The retail offsets here are written out by hand, apart from the
// schema. Beyond the fixture's seams, only the asset pools are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <bit>
#include <cstring>
#include <vector>

namespace
{
using namespace disk32_test;

Material g_material; // the alias Zone registers at block-4 offset 0
menuDef_t g_pool[4]; // what Load_MenuAsset published

constexpr std::uint32_t kRecordBytes = 284;

// Every zone starts block 4 with a material alias, as earlier assets leave it.
struct Zone : disk32_test::Zone<2048>
{
    explicit Zone(std::uint32_t tempBytes = 512) : disk32_test::Zone<2048>(tempBytes)
    {
        DB_SetInsertedPointer(DB_InsertPointer(DBAliasKind::Material), DBAliasKind::Material, &g_material);
    }
};

// The record: a named and grouped window on the material alias, a font,
// an open script, an escape script naming the window's name by offset, two
// key handlers, a three-entry visibility statement, a sound name, a one-entry
// x statement and an empty y statement, and no items.
struct Record
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(kRecordBytes);

    Record()
    {
        Set(0x000, kInline).Float(0x004, 1.5f).Set(0x034, kInline).Set(0x038, 7).Set(0x050, 0x10).Float(0x094, 0.75f);
        Set(0x098, VirtualOffset(0)).Set(0x09C, kInline).Set(0x0A0, 1).Set(0x0A8, 2).Float(0x0C0, 0.5f);
        Set(0x0C4, kInline).Set(0x0CC, VirtualOffset(4)).Set(0x0D0, kInline).Set(0x0D4, 3).Set(0x0D8, kInline);
        Set(0x0E0, kInline).Set(0x0E4, 5).Float(0x104, 0.25f).Set(0x108, 1).Set(0x10C, kInline).Set(0x114, kInline);
    }
    Record &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
    Record &Float(std::uint32_t at, float value)
    {
        return Set(at, std::bit_cast<std::uint32_t>(value));
    }
};

// What a test breaks in the stream.
struct Stream
{
    std::uint32_t firstEntry = kInline; // the visibility statement's first token
    std::uint32_t op = 5;               // its operator, OP_ADD
};

struct File : FileBuilder<File>
{
    // A 12-byte expression entry: type, then the operator or operand.
    File &Entry(std::uint32_t type, std::uint32_t dataType, std::uint32_t value)
    {
        return Word(type).Word(dataType).Word(value);
    }
    // The record, then block 4 from 4: the name at 4, group at 9, font at 11,
    // open script at 14; the key handlers 4-aligned at 20 (its action at 32)
    // and 36; the visibility tokens at 48 and entries at 60 (its string at
    // 72), 76 and 88; the sound name at 100; the x token at 104 and entry at
    // 108; the empty y statement at 120.
    File &Write(const Record &record, const Stream &s = {})
    {
        g_file.insert(g_file.end(), record.bytes.begin(), record.bytes.end());
        Text("main").Text("g").Text("fo").Text("open");
        Word(13).Word(kInline).Word(kInline).Text("a1").Word(27).Word(0).Word(0);
        Word(s.firstEntry).Word(kInline).Word(kInline);
        if (s.firstEntry)
            Entry(1, 2, kInline).Text("x");
        Entry(0, s.op, 0).Entry(1, 0, 42).Text("sn");
        return Word(kInline).Entry(1, 1, std::bit_cast<std::uint32_t>(2.5f));
    }
};

menuDef_t *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMenuDefPtrDisk32, slotValue);
}

// The native records in native storage's order: the two key handlers, the
// visibility entry pointers and entries, then the x statement's.
bool KeyHandlersConverted(const Zone &zone, const menuDef_t &menu)
{
    const ItemKeyHandler *const first = menu.onKey;
    return first == reinterpret_cast<const ItemKeyHandler *>(g_arena) && first->key == 13
        && first->action == zone.At(32) && first->next == first + 1 && first->next->key == 27
        && !first->next->action && !first->next->next;
}

// The visibility statement: a string operand, an operator and an int
// operand, in native storage past the key handlers.
bool VisibilityConverted(const Zone &zone, const statement_s &visible)
{
    const auto *const entries = reinterpret_cast<expressionEntry *const *>(g_arena + 48);
    return visible.numEntries == 3 && visible.entries == entries && entries[0]->type == 1
        && entries[0]->data.operand.dataType == VAL_STRING && entries[0]->data.operand.internals.string == zone.At(72)
        && entries[1]->type == 0 && entries[1]->data.op == OP_ADD && entries[2]->data.operand.dataType == VAL_INT
        && entries[2]->data.operand.internals.intVal == 42 && zone.virt[76 + 4] == 5; // the operator, 4-aligned
}

// The x statement's float operand, last in native storage, and the empty y
// statement at its stream position.
bool RectsConverted(const Zone &zone, const menuDef_t &menu)
{
    const expressionEntry *const x = menu.rectXExp.entries ? menu.rectXExp.entries[0] : nullptr;
    return x == reinterpret_cast<const expressionEntry *>(g_arena + 152) && x->data.operand.dataType == VAL_FLOAT
        && x->data.operand.internals.floatVal == 2.5f && !menu.rectYExp.numEntries
        && reinterpret_cast<const std::uint8_t *>(menu.rectYExp.entries) == zone.virt + 120;
}

bool WindowConverted(const windowDef_t &window)
{
    return window.rect.x == 1.5f && window.style == 7 && window.dynamicFlags[0] == 0x10
        && window.outlineColor[3] == 0.75f && window.background == &g_material;
}

bool ScalarsConverted(const menuDef_t &menu)
{
    return WindowConverted(menu.window) && menu.fullScreen == 1 && menu.fontIndex == 2 && menu.blurRadius == 0.5f
        && menu.imageTrack == 5 && menu.disableColor[3] == 0.25f && !menu.itemCount && !menu.items;
}

// The strings, in block 4; the escape script names the window's name.
bool StringsLoaded(const Zone &zone, const menuDef_t &menu)
{
    return menu.window.name == zone.At(4) && !std::strcmp(zone.At(4), "main") && menu.window.group == zone.At(9)
        && menu.font == zone.At(11) && menu.onOpen == zone.At(14) && !menu.onClose && menu.onESC == zone.At(4)
        && menu.soundName == zone.At(100) && !menu.allowedBinding;
}

void TestInlineMenu()
{
    Zone zone;
    File().Write(Record());
    const menuDef_t *const menu = Load(kInline);
    Expect(menu == &g_pool[0] && g_published == 1, "an inline menu publishes one pool entry");
    if (menu != &g_pool[0])
        return;
    Expect(StringsLoaded(zone, *menu), "the strings stream into block 4, and an offset token names an earlier one");
    Expect(ScalarsConverted(*menu), "the window and menu scalars convert, and the background names the alias");
    Expect(KeyHandlersConverted(zone, *menu), "the key handlers chain in native storage");
    Expect(VisibilityConverted(zone, menu->visibleExp) && RectsConverted(zone, *menu),
           "the statements' entries convert into native storage");
    Expect(g_arenaUsed == 176 && g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 120 && zone.virt[103] == 0
               && !std::memcmp(zone.temp, g_file.data(), kRecordBytes),
           "the record streams into the temp block, and block 4 holds exactly the menu");
}

// A bare menu (a name and nothing else) for the breaks that need little stream.
Record Bare()
{
    Record bare;
    bare.Set(0x034, 0).Set(0x09C, 0).Set(0x0C4, 0).Set(0x0CC, 0).Set(0x0D0, 0).Set(0x0D4, 0).Set(0x0D8, 0);
    bare.Set(0x0E0, 0);
    return bare.Set(0x108, 0).Set(0x10C, 0).Set(0x114, 0);
}

// A shared-inline menu registers its alias at block-4 offset 4, so a later
// offset token resolves to the pooled menu.
void TestSharedInlineAndAlias()
{
    Zone zone;
    const Record bare = Bare();
    File file;
    g_file.insert(g_file.end(), bare.bytes.begin(), bare.bytes.end());
    file.Text("bare");
    const menuDef_t *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && shared->window.name == zone.At(8) && g_arenaUsed == 0,
           "a bare shared-inline menu publishes, past its alias slot, with no native storage");
    Expect(Load(VirtualOffset(4)) == shared && g_published == 1, "its alias resolves to the pooled menu");
    Expect(!Load(0) && g_published == 1, "a null token loads nothing");
}

// A 372-byte item: named, of type 1 (no type data, junk in its slot), with
// a text, a test dvar naming the menu's name by offset, a key handler, and a
// one-entry visibility statement.
struct Item
{
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(372);

    Item()
    {
        Set(0x000, kInline).Set(0x0B4, 1).Set(0x0D0, std::bit_cast<std::uint32_t>(0.5f)).Set(0x0E0, kInline);
        Set(0x0E8, 0xDEADBEEF).Set(0x110, VirtualOffset(4)).Set(0x114, kInline);
        Set(0x124, std::bit_cast<std::uint32_t>(2.f)).Set(0x12C, 0x12345678).Set(0x134, 1).Set(0x138, kInline);
    }
    Item &Set(std::uint32_t at, std::uint32_t value)
    {
        for (std::uint32_t byte = 0; byte < 4; ++byte)
            bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
        return *this;
    }
};

// A bare menu named "b" at 4 with two items: their tokens at 8, the first at
// 16 (its name at 388, text at 391, key handler at 396, statement token at 408
// and entry at 412, the entry's string at 424), then a bare list box with no
// type data 4-aligned at 428 (its name at 800).
// typeData, when given, writes the first item's type data after its key
// handler (at 408), or after its text when it has none.
void WriteItems(const Item &first, std::uint32_t secondToken = kInline, void (*typeData)() = nullptr)
{
    const Record menu = Bare().Set(0x0A4, 2).Set(0x118, kInline);
    Item second;
    second.bytes.assign(372, 0);
    second.Set(0x000, kInline).Set(0x0B4, 6);
    g_file.insert(g_file.end(), menu.bytes.begin(), menu.bytes.end());
    File().Text("b").Word(kInline).Word(secondToken);
    g_file.insert(g_file.end(), first.bytes.begin(), first.bytes.end());
    File().Text("i0").Text("t");
    if (first.bytes[0x114])
        File().Word(32).Word(0).Word(0);
    if (typeData)
        typeData();
    File().Word(kInline).Entry(1, 2, kInline).Text("v");
    g_file.insert(g_file.end(), second.bytes.begin(), second.bytes.end());
    File().Text("i1");
}

// The first item, past the item pointers in native storage, then its key
// handler, its entry pointer and entry.
bool FirstItemScalars(const itemDef_s *item)
{
    return item == reinterpret_cast<const itemDef_s *>(g_arena + 16) && item->type == 1 && item->textscale == 0.5f
        && !item->parent && item->special == 2.f && !item->typeData.data;
}

bool FirstItemConverted(const Zone &zone, const itemDef_s *item)
{
    const statement_s &visible = item->visibleExp;
    return FirstItemScalars(item) && item->window.name == zone.At(388) && item->text == zone.At(391)
        && item->dvarTest == zone.At(4) && item->onKey == reinterpret_cast<const ItemKeyHandler *>(g_arena + 544)
        && item->onKey->key == 32 && visible.numEntries == 1
        && visible.entries[0]->data.operand.internals.string == zone.At(424) && !item->forecolorAExp.entries;
}

void TestItems()
{
    Zone zone;
    WriteItems(Item());
    const menuDef_t *const menu = Load(kInline);
    Expect(menu == &g_pool[0] && menu->itemCount == 2 && menu->items == reinterpret_cast<itemDef_s **>(g_arena),
           "a menu's two items publish, their pointers in native storage");
    Expect(!zone.virt[7] && zone.virt[8] == 0xFF, "the item tokens stream 4-aligned past the menu's name");
    if (menu != &g_pool[0])
        return;
    Expect(FirstItemConverted(zone, menu->items[0]), "the first item converts, its parent and type data null");
    const itemDef_s *const second = menu->items[1];
    Expect(second == reinterpret_cast<const itemDef_s *>(g_arena + 600) && second->window.name == zone.At(800)
               && second->type == 6 && !second->typeData.data,
           "the second item follows 4-aligned, a list box without type data");
    Expect(g_arenaUsed == 1128 && g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 803,
           "block 4 holds exactly the menu and its items");
}

struct ItemBreak
{
    const char *what;
    std::uint32_t at;
    std::uint32_t value;
    const char *error;
};

const ItemBreak kItemBreaks[] = {
    {"an unmapped enum dvar", 0x0B4, 13, "string offset"}, // its token is the junk 0x12345678
    {"an unmapped focus sound", 0x120, VirtualOffset(64), "alias offset"},
    {"an unmapped mouse-enter text", 0x0EC, VirtualOffset(900), "string offset"},
    {"an unmapped leave-focus script", 0x108, VirtualOffset(900), "string offset"},
    {"an unmapped enable dvar", 0x118, VirtualOffset(900), "string offset"},
    {"an item statement without a token", 0x16C, 1, "statement entry count"},
};

// A 340-byte list box: two columns (the second 33 wide), a double-click
// script and the material alias for its select icon.
void WriteListBox(std::uint32_t columns, std::uint32_t icon)
{
    File file;
    for (std::uint32_t word = 0; word < 85; ++word)
    {
        const std::uint32_t values[] = {0, 0, 0, 0, std::bit_cast<std::uint32_t>(1.25f), 0, 0, columns, 0, 0, 0, 0, 0,
                                        33};
        file.Word(word == 72 ? kInline : word == 84 ? icon : word < 14 ? values[word] : 0);
    }
    file.Text("dc");
}

// A 392-byte multi-value: its first dvar name and string inline, its last
// dvar name the menu's name by offset, three values (the third 1.5), strings.
void WriteMulti(std::uint32_t count, std::uint32_t lastString)
{
    File file;
    for (std::uint32_t word = 0; word < 98; ++word)
    {
        const bool inlineToken = word == 0 || word == 32;
        file.Word(inlineToken ? kInline
                  : word == 31 ? VirtualOffset(4)
                  : word == 63 ? lastString
                  : word == 66 ? std::bit_cast<std::uint32_t>(1.5f)
                  : word == 96 ? count
                  : word == 97 ? 1
                               : 0);
    }
    file.Text("l0").Text("s0");
}

const itemDef_s *LoadFirstItem()
{
    const menuDef_t *const menu = Load(kInline);
    return menu == &g_pool[0] && g_read == g_file.size() ? menu->items[0] : nullptr;
}

// The first item's type data follows its key handler 4-aligned at 408, or
// its text at 396, and converts into native storage after the item (at 544)
// or its handler (at 568).
void TestListBox()
{
    Zone zone;
    // No key handler: the list box follows the text 4-aligned at 396.
    WriteItems(Item().Set(0x0B4, 6).Set(0x12C, kInline).Set(0x114, 0), kInline,
               [] { WriteListBox(2, VirtualOffset(0)); });
    const itemDef_s *const item = LoadFirstItem();
    const listBoxDef_s *const listBox = item ? item->typeData.listBox : nullptr;
    Expect(listBox == reinterpret_cast<const listBoxDef_s *>(g_arena + 544) && listBox->numColumns == 2
               && listBox->columnInfo[1].width == 33 && listBox->elementWidth == 1.25f
               && listBox->doubleClick == zone.At(736) && listBox->selectIcon == &g_material,
           "a list box converts, its double-click script past it and its icon the alias");
}

void TestEditField()
{
    Zone zone;
    // No key handler: the edit field follows the text 4-aligned at 396.
    WriteItems(Item().Set(0x0B4, 18).Set(0x12C, kInline).Set(0x114, 0), kInline,
               [] { File().Float(0).Float(10).Float(0).Float(0).Word(12).Word(0).Word(0).Word(0); });
    const itemDef_s *const item = LoadFirstItem();
    const editFieldDef_s *const field = item ? item->typeData.editField : nullptr;
    Expect(field == reinterpret_cast<const editFieldDef_s *>(g_arena + 544) && field->maxVal == 10.f
               && field->maxChars == 12 && zone.virt[396 + 16] == 12,
           "an edit field converts, streaming 4-aligned past the text");
}

void TestMulti()
{
    Zone zone;
    WriteItems(Item().Set(0x0B4, 12).Set(0x12C, kInline), kInline, [] { WriteMulti(3, 0); });
    const itemDef_s *const item = LoadFirstItem();
    const multiDef_s *const multi = item ? item->typeData.multi : nullptr;
    Expect(multi == reinterpret_cast<const multiDef_s *>(g_arena + 568) && multi->dvarList[0] == zone.At(800)
               && multi->dvarStr[0] == zone.At(803) && multi->dvarList[31] == zone.At(4) && !multi->dvarList[1]
               && multi->dvarValue[2] == 1.5f && multi->count == 3 && multi->strDef == 1,
           "a multi-value converts, its names before its strings");
}

void TestEnumDvar()
{
    Zone zone;
    WriteItems(Item().Set(0x0B4, 13).Set(0x12C, kInline), kInline, [] { File().Text("ed"); });
    const itemDef_s *const item = LoadFirstItem();
    Expect(item && item->typeData.enumDvarName == zone.At(408), "an enum dvar's name streams after the key handler");
}

// Type data that breaks a rule.
void TestTypeDataBreaksFailClosed()
{
    const std::pair<void (*)(), const char *> breaks[] = {
        {[] { WriteListBox(17, VirtualOffset(0)); }, "list-box column count"},
        {[] { WriteListBox(2, VirtualOffset(64)); }, "alias offset"},
        {[] { WriteMulti(33, 0); }, "multi-value count"},
        {[] { WriteMulti(3, VirtualOffset(1900)); }, "string offset"},
        {[] { WriteListBox(17, VirtualOffset(0)); }, "alias offset"}, // the focus sound loads first
    };
    for (const auto &[write, error] : breaks)
    {
        Zone zone;
        const bool multi = write == breaks[2].first || write == breaks[3].first;
        const bool soundFirst = write == breaks[4].first;
        WriteItems(Item().Set(0x0B4, multi ? 12 : 6).Set(0x12C, kInline).Set(0x120, soundFirst ? VirtualOffset(64) : 0),
                   kInline, write);
        ExpectDrop("malformed type data", error, [] { Load(kInline); });
    }
}

void TestItemBreaksFailClosed()
{
    for (const ItemBreak &test : kItemBreaks)
    {
        Zone zone;
        WriteItems(Item().Set(test.at, test.value));
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    {
        Zone zone;
        WriteItems(Item(), 0);
        ExpectDrop("a null item", "null item", [] { Load(kInline); });
    }
    Zone zone;
    const Record menu = Bare().Set(0x0A4, 0x40000000).Set(0x118, kInline);
    g_file.insert(g_file.end(), menu.bytes.begin(), menu.bytes.end());
    File().Text("b");
    ExpectDrop("item bytes past 32 bits", "menu item count", [] { Load(kInline); });
}

struct Malformed
{
    const char *what;
    std::uint32_t at;
    std::uint32_t value;
    const char *error;
};

constexpr const char *kItems = "menu item count";
constexpr const char *kEntries = "statement entry count";

const Malformed kMalformed[] = {
    {"a negative item count", 0x0A4, 0xFFFFFFFF, kItems},
    {"items without a token", 0x0A4, 1, kItems},
    {"a null name", 0x000, 0, "has no name"},
    {"an unmapped background", 0x098, VirtualOffset(64), "alias offset"},
    {"an unmapped escape script", 0x0CC, VirtualOffset(900), "string offset"},
    {"a negative entry count", 0x0D4, 0xFFFFFFFF, kEntries},
    {"entries without a token", 0x10C, 0, kEntries},
    {"entry bytes past 32 bits", 0x0D4, 0x40000000, kEntries},
    {"tokens past the block", 0x0D4, 600, "exceeds stream block"},
};

const std::pair<Stream, const char *> kStreamBreaks[] = {
    {{0}, "null entry"},
    {{kInline, static_cast<std::uint32_t>(NUM_OPERATORS)}, "expression operator"},
    {{kInline, 0xFFFFFFFF}, "expression operator"},
};


void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        File().Write(Record().Set(test.at, test.value));
        ExpectDrop(test.what, test.error, [] { Load(kInline); });
    }
    for (const auto &[stream, error] : kStreamBreaks)
    {
        Zone zone;
        File().Write(Record(), stream);
        ExpectDrop("a malformed stream", error, [] { Load(kInline); });
    }
    {
        Zone zone;
        const Record chained = Bare().Set(0x0D0, kInline);
        g_file.insert(g_file.end(), chained.bytes.begin(), chained.bytes.end());
        File().Text("b").Word(1).Word(0).Word(kInline); // its next handler never arrives
        ExpectDrop("a key-handler chain past the stream", "ended unexpectedly", [] { Load(kInline); });
    }
    {
        Zone zone;
        File().Write(Record());
        g_file.resize(kRecordBytes - 4);
        ExpectDrop("a truncated record", "ended unexpectedly", [] { Load(kInline); });
    }
    Zone zone;
    File().Write(Record());
    g_arenaCapacity = 175;
    ExpectDrop("native storage exhausted", "exhausted", [] { Load(kInline); });
    ExpectDrop("an unmapped alias", "alias offset", [] { Load(VirtualOffset(16)); });
}
} // namespace

void __cdecl Load_MenuAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the window's name, then copies the header.
    menuDef_t &entry = g_pool[g_published++];
    entry = *header->menu;
    Expect(entry.window.name != nullptr, "a published menu has a name");
    header->menu = &entry;
}

// The background names its material by alias, so nothing else loads; the
// families' TUs link all the same.
void __cdecl Load_MaterialAsset(XAssetHeader *)
{
    Expect(false, "no material loads");
}

void __cdecl Load_MaterialTechniqueSetAsset(XAssetHeader *)
{
    Expect(false, "no technique set loads");
}

void __cdecl Load_GfxImageAsset(XAssetHeader *)
{
    Expect(false, "no image loads");
}

// Focus sounds resolve only by alias.
void __cdecl Load_snd_alias_list_Asset(XAssetHeader *)
{
    Expect(false, "no sound loads");
}

void __cdecl Load_LoadedSoundAsset(XAssetHeader *)
{
    Expect(false, "no loaded sound loads");
}

void __cdecl Load_SndCurveAsset(XAssetHeader *)
{
    Expect(false, "no sound curve loads");
}

void __cdecl DB_LoadedExternalData(std::int32_t)
{
    Expect(false, "no image loads");
}

int main()
{
    return Run({TestInlineMenu, TestSharedInlineAndAlias, TestItems, TestListBox, TestEditField, TestMulti,
                TestEnumDvar, TestTypeDataBreaksFailClosed, TestItemBreaksFailClosed, TestMalformedFailsClosed});
}
