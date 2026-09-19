// MP message bit-codec wire contracts -- delta coding and framing half.
//
// Companion to msg_wire_contract_tests.cpp, sharing the harness and link
// stubs in msg_wire_test_harness.hpp and linking the same production codec
// (src/qcommon/msg_bits_mp.cpp + huffman.cpp). This TU pins:
//
//   - key-XOR delta primitives: WriteDeltaKey/ReadDeltaKey and the
//     Byte/Short variants, plus the unconditional WriteKey/ReadKey XOR,
//   - the usercmd delta classes: identical-command, low path (angle-only),
//     high path (buttons/weapon/melee), the serverTime >= 0x100 jump and the
//     BUTTON_LOC_CONFIRM-gated selectedLocation bytes -- every golden
//     machine-verified against the production encoder, incl. the retail
//     sign-extension asymmetry the melee yaw decode exhibits,
//   - string framing (WriteString/BigString, I_CleanChar and the '%' remap
//     on the wire, overflow guards) and split-buffer reads
//     (MSG_InitReadOnlySplit).
//
// See msg_wire_contract_tests.cpp for the full suite contract description
// (tie handling, msg_t quirks, reporter contract).

#include "msg_wire_test_harness.hpp"

#include <cstring>
#include <string>

// ---------------------------------------------------------------- tests ----

void test_delta_key_xor()
{
    const int key = 0x5A;
    msg_t m;
    std::uint8_t buf[32];
    makeMsg(m, buf, 32);

    // Unchanged -> single 0 bit.
    MSG_WriteDeltaKey(&m, key, 7, 7, 8);
    // Changed -> 1 bit + (key ^ new) masked to 8 bits: 0x5A ^ 0x2C = 0x76,
    // LSB-first from bit 2: byte 0 payload bits 3,4,6,7 set + the two carry
    // bits into byte 1 bits 0-1 (bit 0 set, the MSB at bit 1 currently 0 --
    // both covered by the carry span so a drift there still names the field).
    MSG_WriteDeltaKey(&m, key, 0, 0x2C, 8);
    const std::uint8_t want[] = {0xDA, 0x01};
    const WireSpan spans[] = {
        {"unchanged-flag", 0, 0, 1},
        {"changed-flag", 0, 1, 1},
        {"xored-payload", 0, 2, 6},
        {"xored-payload-carry", 1, 0, 2},
    };
    compareWire(m, "WriteDeltaKey", want, sizeof(want), spans, 4);

    MSG_BeginReading(&m);
    CHECK(MSG_ReadDeltaKey(&m, key, 7, 8) == 7);
    CHECK(MSG_ReadDeltaKey(&m, key, 0, 8) == 0x2C);

    // Byte/Short variants: the flag is a single bit, the payload a RAW byte
    // (WriteByte) / RAW short (WriteShort), both key-XORed.
    msg_t m2;
    std::uint8_t buf2[32];
    makeMsg(m2, buf2, 32);
    MSG_WriteDeltaKeyByte(&m2, static_cast<char>(0x33), static_cast<char>(0),
                          static_cast<char>(0x55));
    MSG_WriteDeltaKeyShort(&m2, static_cast<__int16>(0x1234), static_cast<__int16>(0),
                           static_cast<__int16>(0x5678));
    // 0x33^0x55 = 0x66 raw byte after flag bit0 -> {0x01, 0x66};
    // 0x1234^0x5678 = 0x444C raw short after flag bit1 -> byte0 0x03, {4C, 44}.
    const std::uint8_t want2[] = {0x03, 0x66, 0x4C, 0x44};
    const WireSpan spans2[] = {
        {"byte-flag", 0, 0, 1}, {"short-flag", 0, 1, 1},
        {"byte-payload", 1, 0, 8},
        {"short-payload", 2, 0, 16},
    };
    compareWire(m2, "WriteDeltaKeyByte/Short", want2, sizeof(want2), spans2, 4);

    MSG_BeginReading(&m2);
    CHECK(MSG_ReadDeltaKeyByte(&m2, static_cast<std::uint8_t>(0x33), 0) == 0x55);
    CHECK(MSG_ReadDeltaKeyShort(&m2, static_cast<__int16>(0x1234), 0) == 0x5678);

    // WriteKey/ReadKey (unconditional XOR write).
    msg_t m3;
    std::uint8_t buf3[8];
    makeMsg(m3, buf3, 8);
    MSG_WriteKey(&m3, 0xFF, 0x0F, 8); // writes 0xF0
    const std::uint8_t want3[] = {0xF0};
    compareWire(m3, "WriteKey", want3, sizeof(want3));
    MSG_BeginReading(&m3);
    CHECK(MSG_ReadKey(&m3, 0xFF, 8) == 0x0F);
}

// MSG_SetDefaultUserCmd stays in msg_mp.cpp (it dereferences the 32-bit-pinned
// playerState_s from bgame/bg_local.h), so only the wire codec side of the
// usercmd path is linked here. The delta codec itself only needs usercmd_s,
// which is pointer-free and fully defined in msg_mp.h.
void test_usercmd_identical()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = from.serverTime; // delta 0

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    const std::uint8_t want[] = {0x01, 0x00};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-delta", 1, 0, 8},
        {"changed-flag", 0, 1, 1},
    };
    compareWire(m, "usercmd identical", want, sizeof(want), spans, 3);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == to.serverTime);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == to.angles[0]);
    CHECK(out.angles[1] == to.angles[1]);
    CHECK(out.angles[2] == to.angles[2]);
    CHECK(out.weapon == to.weapon);
    CHECK(out.offHandIndex == to.offHandIndex);
    CHECK(out.forwardmove == to.forwardmove);
    CHECK(out.rightmove == to.rightmove);
}

// Low path: only the low bits change (angles + hor move), buttons>>1,
// weapon, offHand, angles[2] and melee are all unchanged. keyb is derived
// from the NEW serverTime; the hor-move nibble's last bits jump past the
// byte-field gap exactly like retail.
void test_usercmd_low_path()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = 1010;
    to.angles[0] = 150;
    to.angles[1] = 250;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keyb = 1010 ^ 0x12345678
    // = 0x1234558A.
    //   byte0: bit0 servertime-flag=1, bit1 changed=1, bit2 lowpath=0,
    //          bit3 buttons0=0, bit4 angles0-changed=1, bit5 angles1-changed=1,
    //          bit6 hor-changed=0 -> 0x33
    //   byte1: raw serverTime delta 10 -> 0x0A
    //   bytes2-3: WriteShort(keyb ^ 150 = 0x1234551C) -> 1C 55
    //   bytes4-5: WriteShort(keyb ^ 250 = 0x12345570) -> 70 55
    const std::uint8_t want[] = {0x33, 0x0A, 0x1C, 0x55, 0x70, 0x55};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-delta", 1, 0, 8},
        {"changed-flag", 0, 1, 1}, {"lowpath-flag", 0, 2, 1},
        {"buttons-bit0", 0, 3, 1}, {"angles0-flag", 0, 4, 1},
        {"angles1-flag", 0, 5, 1}, {"hor-flag", 0, 6, 1},
        {"angles0-xored", 2, 0, 16}, {"angles1-xored", 4, 0, 16},
    };
    compareWire(m, "usercmd low path", want, sizeof(want), spans, 10);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 1010);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == 150);
    CHECK(out.angles[1] == 250);
    CHECK(out.angles[2] == 300);
    CHECK(out.weapon == 1);
    CHECK(out.offHandIndex == 2);
    CHECK(out.forwardmove == 0);
    CHECK(out.rightmove == 0);
    CHECK(out.meleeChargeDist == 0);
}

// High path: buttons>>1 differs (melee press), weapon changes, angles[2]
// changes, melee charge set. Exercises the keya-derived fields, the 20-bit
// buttons>>1 field, the 7-bit weapon/offhand fields and the melee gating on
// BUTTON_MELEE.
void test_usercmd_high_path()
{
    const int key = static_cast<int>(0xDEADBEEFu);
    usercmd_s from = baseCmd();
    from.serverTime = 5000;
    usercmd_s to = baseCmd();
    to.serverTime = 5005;
    to.buttons = BUTTON_MELEE; // 4 -- buttons>>1 differs from 0
    to.weapon = 5;
    to.angles[2] = 35;
    to.meleeChargeYaw = 1.0f;
    to.meleeChargeDist = 10;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keya = 5005 ^ 0xDEADBEEF
    // = 0xDEADAD62 (the encoder truncates every key to __int16 -> 0xAD62).
    //   byte0  0x89: st-flag=1, changed=1, highpath=1, buttons0=1,
    //                a0/a1/hor/a2 flags=0
    //   byte1  0x05: serverTime delta
    //   b2-3  41 AD: WriteShort(keya ^ 35 = 0xAD62^0x23 = 0xAD41)
    //   b4    0xC1: 20-bit buttons>>1 payload head + flags
    //   b5    0x5A: payload bits 8,10,11,13
    //   b6    0xFB: payload tail + weapon flag + weapon payload
    //   b7    0xD9: melee flag + dist flag + yaw-short carry bits
    //   b8-9  D4 AD: WriteShort(keya ^ 182 = 0xAD62^0xB6 = 0xADD4)
    //   b10   0x68: 8-bit dist payload (keya ^ 10)
    const std::uint8_t want[] = {0x89, 0x05, 0x41, 0xAD, 0xC1, 0x5A,
                                 0xFB, 0xD9, 0xD4, 0xAD, 0x68};
    const WireSpan spans[] = {
        {"highpath-flags", 0, 0, 8}, {"servertime-delta", 1, 0, 8},
        {"buttons>>1-20bit", 4, 0, 8}, {"buttons>>1-mid", 5, 0, 8},
        {"buttons>>1-tail+weapon", 6, 0, 8}, {"offhand+flags", 7, 0, 8},
        {"angles2-xored", 2, 0, 16}, {"meleeYaw-xored", 8, 0, 16},
        {"meleeDist-xored", 10, 0, 8},
    };
    compareWire(m, "usercmd high path", want, sizeof(want), spans, 9);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 5005);
    CHECK(out.buttons == BUTTON_MELEE);
    CHECK(out.angles[0] == 100);
    CHECK(out.angles[1] == 200);
    CHECK(out.angles[2] == 35);
    CHECK(out.weapon == 5);
    CHECK(out.offHandIndex == 2);
    CHECK(out.meleeChargeDist == 10);
    // Retail asymmetry, pinned exactly: the encoder XORs the quantized yaw
    // with keya truncated to __int16 (0xAD62), but the decoder's
    // MSG_ReadDeltaKeyShort takes __int16 key and sign-extends it in its int
    // return, so the recovered quantized value is 0xFFFF00B6 = -65354, not
    // 182. Retail client and server both decode this identically, so the
    // simulation agrees; the wire-visible decode is -65354 * 360/65536.
    CHECK(out.meleeChargeYaw == -359.000244140625);
}

// ServerTime jump >= 0x100 takes the explicit-long branch.
void test_usercmd_time_jump()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = 2000; // delta 1000

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    const std::uint8_t want[] = {0x00, 0xD0, 0x07, 0x00, 0x00};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-full", 1, 0, 32},
        {"changed-flag", 0, 1, 1},
    };
    compareWire(m, "usercmd time jump", want, sizeof(want), spans, 3);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 2000);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == to.angles[0]);
}

// selectedLocation bytes are gated on BUTTON_LOC_CONFIRM (0x10000) and XORed
// with keya; an odd key exercises the inverted flag parities.
void test_usercmd_selected_location()
{
    const int key = 0x00000001;
    usercmd_s from = baseCmd();
    from.serverTime = 500;
    usercmd_s to = baseCmd();
    to.serverTime = 500;
    to.buttons = BUTTON_LOC_CONFIRM; // 0x10000
    to.selectedLocation[0] = 7;
    to.selectedLocation[1] = 9;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keya = 500 ^ 1 = 501
    // = 0x1F5; the 20-bit buttons>>1 payload is keya ^ 0x8000 = 0x81F5 and the
    // selLoc payloads are the raw bytes keya ^ 7 = 0xF2 and keya ^ 9 = 0xFC.
    // Each selLoc is preceded by its change flag bit: selloc0's flag shares
    // byte 4 with the payload tail; selloc1's flag opens byte 6 and the raw
    // payload byte follows at byte 7. Byte 0 is the control-flag byte
    // (0x09: bit0 servertime delta-byte flag, bit3 buttons field flag) and
    // byte 1 is the raw serverTime delta (500 -> 500 = 0).
    const std::uint8_t want[] = {0x09, 0x00, 0xEB, 0x03, 0x81, 0xF2, 0x01, 0xFC};
    const WireSpan spans[] = {
        {"selloc-flags", 0, 0, 8}, {"servertime-delta", 1, 0, 8},
        {"buttons>>1-20bit", 2, 0, 8}, {"buttons>>1-mid", 3, 0, 8},
        {"payload-tail+flags", 4, 0, 8}, {"selloc0-xored", 5, 0, 8},
        {"selloc1-flag", 6, 0, 8}, {"selloc1-xored", 7, 0, 8},
    };
    compareWire(m, "usercmd selectedLocation", want, sizeof(want), spans, 8);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 500);
    CHECK(out.buttons == BUTTON_LOC_CONFIRM);
    CHECK(out.selectedLocation[0] == 7);
    CHECK(out.selectedLocation[1] == 9);
    CHECK(out.weapon == 1);
    CHECK(out.angles[0] == 100);
}

void test_string_framing()
{
    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);

    MSG_WriteString(&m, "hi");
    MSG_WriteString(&m, "a\x92" "c"); // 146 cleans to 39 ('\'') on the wire
    const std::uint8_t want[] = {'h', 'i', 0, 'a', '\'', 'c', 0};
    // WireSpan::bits is a BIT length (spanFor matches in absolute bit
    // coordinates): "hi\0" covers 3 bytes = 24 bits and "a'c\0" covers
    // 4 bytes = 32 bits, so drift in ANY byte of either encoded string
    // names that string instead of reporting (unmapped).
    const WireSpan spans[] = {
        {"string0", 0, 0, 3 * 8}, {"string1", 3, 0, 4 * 8},
    };
    compareWire(m, "WriteString + I_CleanChar", want, sizeof(want), spans, 2);

    MSG_BeginReading(&m);
    CHECK(std::strcmp(MSG_ReadString(&m), "hi") == 0);
    CHECK(std::strcmp(MSG_ReadString(&m), "a'c") == 0);

    // Oversized string collapses to a single NUL byte.
    msg_t m2;
    std::uint8_t buf2[8];
    makeMsg(m2, buf2, 8);
    std::string big(1024, 'x');
    MSG_WriteString(&m2, big.c_str());
    const std::uint8_t want2[] = {0x00};
    compareWire(m2, "WriteString overflow guard", want2, sizeof(want2));

    // BigString write side + the read-side '%' -> '.' remap.
    msg_t m3;
    std::uint8_t buf3[64];
    makeMsg(m3, buf3, 64);
    char bigmsg[16] = "a%b";
    MSG_WriteBigString(&m3, bigmsg);
    const std::uint8_t want3[] = {'a', '%', 'b', 0};
    compareWire(m3, "WriteBigString", want3, sizeof(want3));
    MSG_BeginReading(&m3);
    CHECK(std::strcmp(MSG_ReadBigString(&m3), "a.b") == 0);

    // ReadStringLine stops at '\n'.
    msg_t m4;
    std::uint8_t buf4[8];
    makeMsg(m4, buf4, 8);
    char lineData[] = {'A', '\n', 'B', 0};
    MSG_WriteData(&m4, reinterpret_cast<std::uint8_t *>(lineData), 4);
    MSG_BeginReading(&m4);
    CHECK(std::strcmp(MSG_ReadStringLine(&m4), "A") == 0);
}

void test_read_data_and_splits()
{
    // MSG_ReadData across the primary/split boundary (netchan reassembly
    // layout), including the ki-gu2 negative-length guard.
    //
    // Contracts pinned here: (1) MSG_InitReadOnlySplit does NOT zero the
    // msg_t -- engine call sites pass zero-initialized structs and a garbage
    // readcount makes the split path read out of bounds, so tests must
    // zero-init; (2) a boundary-crossing MSG_ReadData is well-defined when
    // readcount == cursize at entry (the split index is readcount - cursize;
    // entering mid-buffer is a retail quirk that stays unpinned).
    std::uint8_t primary[2] = {0x11, 0x22};
    std::uint8_t split[2] = {0x33, 0x44};
    msg_t m{};
    MSG_InitReadOnlySplit(&m, primary, 2, split, 2);

    // Sequential contract: consume the primary half, then span into the
    // split half.
    CHECK(MSG_ReadByte(&m) == 0x11);
    CHECK(MSG_ReadByte(&m) == 0x22);
    std::uint8_t out[2] = {};
    MSG_ReadData(&m, out, 2);
    CHECK(!m.overflowed);
    CHECK(out[0] == 0x33 && out[1] == 0x44);

    // One-shot read fully inside the primary buffer.
    msg_t m1{};
    MSG_InitReadOnlySplit(&m1, primary, 2, split, 2);
    std::uint8_t out1[2] = {};
    MSG_ReadData(&m1, out1, 2);
    CHECK(!m1.overflowed);
    CHECK(out1[0] == 0x11 && out1[1] == 0x22);

    msg_t m2{};
    MSG_InitReadOnlySplit(&m2, primary, 2, split, 2);
    std::uint8_t out2[4] = {};
    MSG_ReadData(&m2, out2, -1);
    CHECK(m2.overflowed == 1);

    msg_t m3{};
    MSG_InitReadOnlySplit(&m3, primary, 2, split, 2);
    std::uint8_t out3[8] = {};
    MSG_ReadData(&m3, out3, 8);
    CHECK(m3.overflowed == 1);
    // The overflow arm fills the whole destination with 0xFF bytes (the
    // retail memset(data, 0xFF, len)); pinned byte-for-byte because the
    // repair replaces that memset with an explicit loop.
    for (std::uint8_t i = 0; i < 8; ++i)
        CHECK(out3[i] == 0xFF);

    // MSG_Discard / MSG_ClearLastReferencedEntity bookkeeping.
    msg_t m4;
    std::uint8_t buf4[4];
    makeMsg(m4, buf4, 4);
    MSG_ClearLastReferencedEntity(&m4);
    CHECK(m4.lastEntityRef == -1);
    MSG_WriteByte(&m4, 7);
    MSG_Discard(&m4);
    CHECK(m4.overflowed == 1);
    CHECK(m4.cursize == m4.readcount);
}
