// Huffman tie-order independence.
//
// Huff_BuildFromData sorts its heap with qsort, and msg_hData repeats two
// weights, so a comparator that leaves ties to qsort gives each C library its
// own code book. This test compiles the production huffman.cpp with its qsort
// calls routed to huffTestSort, then builds the tree under several sorts and
// checks each result against the reference code book:
//
//   - host qsort, stable sort, and a sort that orders ties by descending
//     address, all with the production nodeCmp. They must agree because
//     nodeCmp is a total order. A weight-only comparator fails the last two
//     on every host: the stable sort swaps 155/205, the other 228/231.
//   - a clone of the MSVC CRT qsort (the 1.7 Linux server ships it as
//     ms_qsort) with retail's weight-only comparator. This derives the
//     reference from the retail algorithm on every host.
//   - on MSVC, the CRT qsort itself with the weight-only comparator, and a
//     cross-check of the clone against the CRT on arrays full of ties.

#include <qcommon/huffman.h>
#include <universal/assertive.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

// Everything huffman.cpp includes is already in, so the macro only renames
// its two qsort calls.
void huffTestSort(void *base, std::size_t count, std::size_t width,
                  int (*compare)(const void *, const void *));

#define qsort huffTestSort
#include <qcommon/huffman.cpp>
#undef qsort

#include "huffman_reference_codebook.h"

#include <qcommon/msg_huffman_data.h>

void MyAssertHandler(const char *filename, int line, int type, const char *, ...)
{
    std::fprintf(stderr, "unexpected production assert at %s:%d (type %d)\n",
                 filename, line, type);
    std::abort();
}

namespace
{
using Compare = int (*)(const void *, const void *);

enum class Sort
{
    kHost,
    kStable,
    kTiesByDescendingAddress,
    kCrtClone,
};

Sort g_sort = Sort::kHost;
Compare g_compareOverride = nullptr;

// Retail's comparator: weight only.
int retailNodeCmp(const void *left, const void *right)
{
    const int leftWeight = (*static_cast<nodetype *const *>(left))->weight;
    const int rightWeight = (*static_cast<nodetype *const *>(right))->weight;
    return leftWeight < rightWeight ? -1 : leftWeight > rightWeight ? 1 : 0;
}

// The MSVC CRT qsort's element order, step for step: a median-of-three
// quicksort that selection-sorts ranges of 8 or fewer, pushes the larger
// partition and loops on the smaller. Indexes are signed like the CRT's
// pointer arithmetic.
void crtSelectionSort(nodetype **a, std::ptrdiff_t lo, std::ptrdiff_t hi, Compare cmp)
{
    for (; hi > lo; --hi)
    {
        std::ptrdiff_t max = lo;
        for (std::ptrdiff_t p = lo + 1; p <= hi; ++p)
        {
            if (cmp(&a[p], &a[max]) > 0)
                max = p;
        }
        std::swap(a[max], a[hi]);
    }
}

void crtQsortClone(nodetype **a, std::size_t count, Compare cmp)
{
    if (count < 2)
        return;
    std::ptrdiff_t loStack[64];
    std::ptrdiff_t hiStack[64];
    int depth = 0;
    const auto push = [&](std::ptrdiff_t lo, std::ptrdiff_t hi) {
        loStack[depth] = lo;
        hiStack[depth] = hi;
        ++depth;
    };
    std::ptrdiff_t lo = 0;
    std::ptrdiff_t hi = static_cast<std::ptrdiff_t>(count) - 1;
    for (;;)
    {
        if (hi - lo + 1 <= 8)
        {
            crtSelectionSort(a, lo, hi, cmp);
        }
        else
        {
            std::ptrdiff_t mid = lo + (hi - lo + 1) / 2;
            if (cmp(&a[lo], &a[mid]) > 0)
                std::swap(a[lo], a[mid]);
            if (cmp(&a[lo], &a[hi]) > 0)
                std::swap(a[lo], a[hi]);
            if (cmp(&a[mid], &a[hi]) > 0)
                std::swap(a[mid], a[hi]);

            std::ptrdiff_t loGuy = lo;
            std::ptrdiff_t hiGuy = hi;
            for (;;)
            {
                if (mid > loGuy)
                {
                    do { ++loGuy; } while (loGuy < mid && cmp(&a[loGuy], &a[mid]) <= 0);
                }
                if (mid <= loGuy)
                {
                    do { ++loGuy; } while (loGuy <= hi && cmp(&a[loGuy], &a[mid]) <= 0);
                }
                do { --hiGuy; } while (hiGuy > mid && cmp(&a[hiGuy], &a[mid]) > 0);
                if (hiGuy < loGuy)
                    break;
                std::swap(a[loGuy], a[hiGuy]);
                if (mid == hiGuy)
                    mid = loGuy;
            }
            ++hiGuy;
            if (mid < hiGuy)
            {
                do { --hiGuy; } while (hiGuy > mid && cmp(&a[hiGuy], &a[mid]) == 0);
            }
            if (mid >= hiGuy)
            {
                do { --hiGuy; } while (hiGuy > lo && cmp(&a[hiGuy], &a[mid]) == 0);
            }

            if (hiGuy - lo >= hi - loGuy)
            {
                if (lo < hiGuy)
                    push(lo, hiGuy);
                if (loGuy < hi)
                {
                    lo = loGuy;
                    continue;
                }
            }
            else
            {
                if (loGuy < hi)
                    push(loGuy, hi);
                if (lo < hiGuy)
                {
                    hi = hiGuy;
                    continue;
                }
            }
        }
        if (depth == 0)
            return;
        --depth;
        lo = loStack[depth];
        hi = hiStack[depth];
    }
}
} // namespace

void huffTestSort(void *base, std::size_t count, std::size_t width, Compare compare)
{
    if (width != sizeof(nodetype *))
        std::abort();
    if (g_compareOverride)
        compare = g_compareOverride;
    nodetype **elements = static_cast<nodetype **>(base);
    switch (g_sort)
    {
    case Sort::kHost:
        std::qsort(base, count, width, compare);
        break;
    case Sort::kStable:
        std::stable_sort(elements, elements + count, [compare](nodetype *left, nodetype *right) {
            return compare(&left, &right) < 0;
        });
        break;
    case Sort::kTiesByDescendingAddress:
        std::sort(elements, elements + count, [compare](nodetype *left, nodetype *right) {
            const int order = compare(&left, &right);
            return order != 0 ? order < 0 : std::greater<nodetype *>()(left, right);
        });
        break;
    case Sort::kCrtClone:
        crtQsortClone(elements, count, compare);
        break;
    }
}

namespace
{
int buildAndCompare(Sort sort, Compare compareOverride, const char *label)
{
    g_sort = sort;
    g_compareOverride = compareOverride;
    static huffman_t huff;
    Huff_Init(&huff);
    Huff_BuildFromData(&huff.compressDecompress, msg_hData);
    g_sort = Sort::kHost;
    g_compareOverride = nullptr;
    return huffReferenceCodebookMismatches(&huff.compressDecompress, label);
}

#if defined(_MSC_VER)
// Sorts arrays with many ties with both the CRT and the clone; the resulting
// element orders must be identical.
int crtCloneMismatches()
{
    int mismatches = 0;
    std::uint32_t seed = 12345u;
    for (int round = 0; round < 64; ++round)
    {
        const std::size_t count = 1 + static_cast<std::size_t>(round) * 5;
        std::vector<nodetype> nodes(count);
        std::vector<nodetype *> crt(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            seed = seed * 214013u + 2531011u;
            nodes[i].weight = static_cast<int>((seed >> 16) % 7);
            crt[i] = &nodes[i];
        }
        std::vector<nodetype *> clone = crt;
        std::qsort(crt.data(), count, sizeof(nodetype *), retailNodeCmp);
        crtQsortClone(clone.data(), count, retailNodeCmp);
        if (crt != clone)
        {
            std::fprintf(stderr, "FAIL: CRT qsort clone differs from the CRT for %zu elements\n",
                         count);
            ++mismatches;
        }
    }
    return mismatches;
}
#endif
} // namespace

int main()
{
    int failures = 0;
    failures += buildAndCompare(Sort::kHost, nullptr, "host qsort");
    failures += buildAndCompare(Sort::kStable, nullptr, "stable sort");
    failures += buildAndCompare(Sort::kTiesByDescendingAddress, nullptr,
                                "ties by descending address");
    failures += buildAndCompare(Sort::kCrtClone, nullptr, "CRT qsort clone");
    failures += buildAndCompare(Sort::kCrtClone, retailNodeCmp,
                                "CRT qsort clone, retail comparator");
#if defined(_MSC_VER)
    failures += buildAndCompare(Sort::kHost, retailNodeCmp,
                                "MSVC CRT qsort, retail comparator");
    failures += crtCloneMismatches();
#endif
    if (failures != 0)
    {
        std::fprintf(stderr, "huffman tie-order independence FAILED (%d)\n", failures);
        return 1;
    }
    std::fprintf(stdout, "huffman tie-order independence OK\n");
    return 0;
}
