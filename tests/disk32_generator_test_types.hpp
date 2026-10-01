#pragma once

// disk32_generator_test_types.hpp: native records for the generator test
// (db_disk32_generator_tests.cpp, disk32_generator.schema). They have the shapes
// the converted families need that no merged family uses yet: a record nested
// inline behind a 4-byte head, as XAnimPartTrans holds its frames, and a nested
// native struct with a pointer the loader nulls, as LoadedSound holds MssSound.

#include <cstdint>

struct GenFrames
{
    float mins[3];
    const void *frames;
    std::uint16_t indices[1];
};

// The 4-byte head, then the frames or frame0, as in XAnimPartTrans.
struct GenHead
{
    std::uint16_t size;
    std::uint8_t small;
    union
    {
        GenFrames frames;
        float frame0[3];
    } u;
};

struct GenInfo
{
    std::int32_t rate;
    const void *data;
};

struct GenRecord
{
    GenInfo info;
    std::int16_t values[3];
    bool loop;
    std::uint8_t pad;
    float scale;
};
