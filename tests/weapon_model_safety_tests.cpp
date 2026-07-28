#include <bgame/bg_weapon_model_safety.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
constexpr int kConstModel = 7;
constexpr const int *kConstModels[3] = {nullptr, &kConstModel, nullptr};

static_assert(
    bg::weapon_model::CheckedLookup(kConstModels, 1) == &kConstModel);
static_assert(bg::weapon_model::CheckedLookup(kConstModels, 2) == nullptr);
static_assert(bg::weapon_model::CheckedLookup(kConstModels, -1) == nullptr);
static_assert(bg::weapon_model::ResolveIndex(kConstModels, 1) == 1);
static_assert(bg::weapon_model::ResolveIndex(kConstModels, 2) == 0);

int failures = 0;

void Check(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
} // namespace

int main()
{
    int storage[16] = {};
    int *models[16] = {};
    for (std::int32_t index = 0; index < 16; ++index)
        models[index] = &storage[index];

    Check(
        bg::weapon_model::CheckedLookup(models, 0) == &storage[0],
        "model zero remains valid");
    Check(
        bg::weapon_model::CheckedLookup(models, 15) == &storage[15],
        "the final model slot remains valid");
    Check(
        bg::weapon_model::ResolveIndex(models, 15) == 15,
        "the final model index survives checked narrowing");

    models[7] = nullptr;
    Check(
        bg::weapon_model::CheckedLookup(models, 7) == nullptr,
        "an empty in-range model slot is rejected");
    Check(
        bg::weapon_model::ResolveIndex(models, 7) == 0,
        "an empty in-range model slot falls back to zero");

    Check(
        bg::weapon_model::CheckedLookup(models, -1) == nullptr,
        "negative model indices are rejected before indexing");
    Check(
        bg::weapon_model::CheckedLookup(
            models, (std::numeric_limits<std::int32_t>::min)()) == nullptr,
        "the minimum signed model index is rejected");
    Check(
        bg::weapon_model::CheckedLookup(models, 16) == nullptr,
        "the model count is rejected");
    Check(
        bg::weapon_model::CheckedLookup(models, 255) == nullptr,
        "a byte-sized but out-of-range model index is rejected");
    Check(
        bg::weapon_model::CheckedLookup(models, 256) == nullptr,
        "an index above byte range is rejected");
    Check(
        bg::weapon_model::CheckedLookup(
            models, (std::numeric_limits<std::int32_t>::max)()) == nullptr,
        "the maximum signed model index is rejected");

    Check(
        bg::weapon_model::ResolveIndex(models, -1) == 0,
        "negative narrowing falls back to zero");
    Check(
        bg::weapon_model::ResolveIndex(models, 16) == 0,
        "out-of-range narrowing falls back to zero");
    Check(
        bg::weapon_model::ResolveIndex(models, 255) == 0,
        "invalid byte-sized narrowing falls back to zero");
    Check(
        bg::weapon_model::ResolveIndex(
            models, (std::numeric_limits<std::int32_t>::max)()) == 0,
        "maximum signed narrowing falls back to zero");

    int byteStorage = 0;
    int *byteModels[256] = {};
    byteModels[255] = &byteStorage;
    Check(
        bg::weapon_model::ResolveIndex(byteModels, 255) == 255,
        "a 256-slot table can represent its final index");

    return failures == 0 ? 0 : 1;
}
