#pragma once
void TestScalarScriptFieldStores()
{
    struct IntegerField { int32_t before; int32_t value; int32_t after; } field{};
    HighAddress(&field);
    for (int value : {0, 1, -19, INT32_MAX}) {
        field.before = 0x12345678;
        field.after = 0x23456789;
        fixtureFieldValue = value;
        Fixture_SetGenericIntField(reinterpret_cast<uint8_t *>(&field), offsetof(IntegerField, value));
        Check(field.value == value);
        Check(field.before == 0x12345678 && field.after == 0x23456789);
    }
    static_assert(offsetof(game_hudelem_s, archived) + sizeof(int32_t) == sizeof(game_hudelem_s));
    auto *hud = static_cast<game_hudelem_s *>(Allocate(sizeof(game_hudelem_s)));
    HighAddress(hud);
    *hud = {};
    hud->clientNum = 7;
    hud->team = 13;
    for (int value : {0, 1, -19}) {
        fixtureFieldValue = value;
        HudElem_SetBoolean(hud, 0);
        Check(hud->archived == value);
        Check(hud->clientNum == 7 && hud->team == 13);
    }
}
