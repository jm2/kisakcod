#include <universal/sort_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{

template <typename T, std::size_t Count, typename Less>
bool IsStrictWeakOrder(const std::array<T, Count> &values, Less less)
{
    for (const T &left : values)
    {
        if (less(left, left))
            return false;

        for (const T &right : values)
        {
            if (less(left, right) && less(right, left))
                return false;

            for (const T &third : values)
            {
                if (less(left, right) && less(right, third) && !less(left, third))
                    return false;

                const bool leftRightEquivalent =
                    !less(left, right) && !less(right, left);
                const bool rightThirdEquivalent =
                    !less(right, third) && !less(third, right);
                const bool leftThirdEquivalent =
                    !less(left, third) && !less(third, left);
                if (leftRightEquivalent && rightThirdEquivalent && !leftThirdEquivalent)
                    return false;
            }
        }
    }
    return true;
}

bool TestCStringOrdering()
{
    char alphaFirst[] = "alpha";
    char alphaSecond[] = "alpha";
    char alphaPrefix[] = "alph";
    char beta[] = "beta";
    char delta[] = "delta";
    char empty[] = "";
    char asciiHigh[] = {static_cast<char>(0x7F), '\0'};
    char nonAscii[] = {static_cast<char>(0x80), '\0'};

    std::array<const char *, 9> names = {
        nonAscii,
        delta,
        nullptr,
        alphaSecond,
        beta,
        empty,
        alphaPrefix,
        asciiHigh,
        alphaFirst};
    const auto originalNames = names;

    if (!IsStrictWeakOrder(originalNames, kisak::sort::CStringLess))
        return false;

    std::sort(names.begin(), names.end(), kisak::sort::CStringLess);
    if (std::strcmp(names[0], "") != 0
        || std::strcmp(names[1], "alph") != 0
        || std::strcmp(names[2], "alpha") != 0
        || std::strcmp(names[3], "alpha") != 0
        || std::strcmp(names[4], "beta") != 0
        || std::strcmp(names[5], "delta") != 0
        || names[6] != asciiHigh
        || names[7] != nonAscii
        || names[8] != nullptr)
    {
        return false;
    }

    // The typed sort must preserve every native-width pointer identity. The
    // source contract separately rejects fixed-four-byte production copies.
    std::array<std::uintptr_t, names.size()> beforeAddresses{};
    std::array<std::uintptr_t, names.size()> afterAddresses{};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        beforeAddresses[index] = reinterpret_cast<std::uintptr_t>(originalNames[index]);
        afterAddresses[index] = reinterpret_cast<std::uintptr_t>(names[index]);
    }
    std::sort(beforeAddresses.begin(), beforeAddresses.end());
    std::sort(afterAddresses.begin(), afterAddresses.end());
    if (beforeAddresses != afterAddresses)
        return false;

    const char *singleName = beta;
    std::sort(&singleName, &singleName, kisak::sort::CStringLess);
    std::sort(&singleName, &singleName + 1, kisak::sort::CStringLess);
    return singleName == beta;
}

bool TestFloatOrdering()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<float, 9> values = {
        nan,
        -nan,
        -std::numeric_limits<float>::infinity(),
        -1.0F,
        -0.0F,
        0.0F,
        1.0F,
        std::numeric_limits<float>::infinity(),
        nan};

    if (!IsStrictWeakOrder(values, kisak::sort::FloatLess))
        return false;

    auto sorted = values;
    std::sort(sorted.begin(), sorted.end(), kisak::sort::FloatLess);
    if (!std::isinf(sorted[0]) || sorted[0] >= 0.0F
        || sorted[1] != -1.0F
        || sorted[2] != 0.0F || sorted[3] != 0.0F
        || sorted[4] != 1.0F
        || !std::isinf(sorted[5]) || sorted[5] <= 0.0F)
    {
        return false;
    }
    if (!std::isnan(sorted[6])
        || !std::isnan(sorted[7])
        || !std::isnan(sorted[8]))
    {
        return false;
    }

    std::array<float, 24> candidates{};
    for (std::size_t index = 0; index < candidates.size(); ++index)
        candidates[index] = static_cast<float>(index);
    candidates.back() = -100.0F;
    std::sort(candidates.begin(), candidates.end(), kisak::sort::FloatLess);
    return candidates.front() == -100.0F;
}

} // namespace

int main()
{
    static_assert(sizeof(std::uintptr_t) == sizeof(void *));
    static_assert(sizeof(const char *) == sizeof(void *));

    if (!TestCStringOrdering())
    {
        std::fputs("native string-pointer sort contract failed\n", stderr);
        return 1;
    }
    if (!TestFloatOrdering())
    {
        std::fputs("floating-point strict-order contract failed\n", stderr);
        return 1;
    }
    return 0;
}
