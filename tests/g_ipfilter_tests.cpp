// g_ipfilter_tests.cpp: parsing and matching the IP ban filters behind addip,
// removeip and g_banIPs (game/g_ipfilter.h).

#include <game/g_ipfilter.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

bool Banned(const char *filter, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint32_t mask = 0;
    uint32_t compare = 0;
    if (!g_ipfilter::Parse(filter, &mask, &compare))
        return false;
    const uint8_t ip[4] = {a, b, c, d};
    return g_ipfilter::Matches(mask, compare, ip);
}

void TestExactAndWildcard()
{
    CHECK(Banned("192.168.1.20", 192, 168, 1, 20));
    CHECK(!Banned("192.168.1.20", 192, 168, 1, 21));
    // A zero octet, or one left out, matches anything.
    CHECK(Banned("192.168.0.0", 192, 168, 7, 9));
    CHECK(Banned("192.168", 192, 168, 250, 1));
    CHECK(!Banned("192.168", 192, 169, 0, 1));
    CHECK(Banned("10", 10, 1, 2, 3));
    CHECK(Banned("255.255.255.254", 255, 255, 255, 254));
}

void TestRejected()
{
    uint32_t mask = 0x12345678;
    uint32_t compare = 0x9abcdef0;
    CHECK(!g_ipfilter::Parse("", &mask, &compare));
    CHECK(!g_ipfilter::Parse("abc", &mask, &compare));
    CHECK(!g_ipfilter::Parse("1.2.x.4", &mask, &compare));
    CHECK(!g_ipfilter::Parse("256.1.1.1", &mask, &compare));
    CHECK(!g_ipfilter::Parse("1.2.3.300", &mask, &compare));
    CHECK(!g_ipfilter::Parse("0001.2.3.4", &mask, &compare));
    CHECK(!g_ipfilter::Parse(nullptr, &mask, &compare));
    // A failed parse leaves the outputs alone.
    CHECK(mask == 0x12345678 && compare == 0x9abcdef0);
}

// A digit run far longer than any octet: the engine copied it into a
// 128-byte stack buffer. It is now refused after the fourth digit.
void TestLongDigitRun()
{
    const std::string digits(4000, '7');
    uint32_t mask = 0;
    uint32_t compare = 0;
    CHECK(!g_ipfilter::Parse(digits.c_str(), &mask, &compare));
    CHECK(!g_ipfilter::Parse(("1.2.3." + digits).c_str(), &mask, &compare));
}

// removeip finds a filter by comparing parsed mask and compare values, so the
// same address must always parse the same way.
void TestStableEncoding()
{
    uint32_t m1 = 0;
    uint32_t c1 = 0;
    uint32_t m2 = 0;
    uint32_t c2 = 0;
    CHECK(g_ipfilter::Parse("172.16.0.0", &m1, &c1));
    CHECK(g_ipfilter::Parse("172.16", &m2, &c2));
    CHECK(m1 == m2 && c1 == c2);
}
} // namespace

int main()
{
    TestExactAndWildcard();
    TestRejected();
    TestLongDigitRun();
    TestStableEncoding();
    if (g_failures)
    {
        std::fprintf(stderr, "%d ip filter check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("ip filter contracts: ok");
    return 0;
}
