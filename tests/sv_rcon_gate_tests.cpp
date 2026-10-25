// sv_rcon_gate_tests.cpp: the password check and rate limits SVC_RemoteCommand
// applies to rcon packets (server_mp/sv_rcon_gate.h).

#include <server_mp/sv_rcon_gate.h>

#include <cstdint>
#include <cstdio>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

sv_rcon::Source Ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    sv_rcon::Source source;
    source.type = 4; // NA_IP
    source.ip[0] = a;
    source.ip[1] = b;
    source.ip[2] = c;
    source.ip[3] = d;
    return source;
}

void TestPassword()
{
    using sv_rcon::PasswordMatches;
    CHECK(PasswordMatches("hunter2", "hunter2"));
    CHECK(!PasswordMatches("hunter3", "hunter2"));
    CHECK(!PasswordMatches("hunter", "hunter2"));
    CHECK(!PasswordMatches("hunter22", "hunter2"));
    CHECK(!PasswordMatches("", "hunter2"));
    CHECK(!PasswordMatches("Hunter2", "hunter2"));
    CHECK(PasswordMatches("", ""));
    CHECK(!PasswordMatches(nullptr, "hunter2"));
    CHECK(!PasswordMatches("hunter2", nullptr));
}

// One source gets one attempt per interval; a second source is not blocked by
// the first, which is what lets the admin in while someone else floods.
void TestPerSource()
{
    sv_rcon::Gate gate;
    const sv_rcon::Source spammer = Ip(203, 0, 113, 7);
    const sv_rcon::Source admin = Ip(198, 51, 100, 1);
    CHECK(gate.Allow(spammer, 1000));
    CHECK(!gate.Allow(spammer, 1001));
    CHECK(!gate.Allow(spammer, 1499));
    CHECK(gate.Allow(admin, 1499));
    CHECK(gate.Allow(spammer, 1500));
    CHECK(!gate.Allow(admin, 1600));
    CHECK(gate.Allow(admin, 2000));
}

// All sources together get at most kGlobalAttempts per window; the window
// reopens after kGlobalWindowMsec.
void TestGlobalCap()
{
    sv_rcon::Gate gate;
    for (uint32_t i = 0; i < sv_rcon::Gate::kGlobalAttempts; ++i)
        CHECK(gate.Allow(Ip(10, 0, 0, static_cast<uint8_t>(i)), 5000));
    CHECK(!gate.Allow(Ip(10, 0, 1, 0), 5000));
    CHECK(!gate.Allow(Ip(10, 0, 1, 0), 5999));
    CHECK(gate.Allow(Ip(10, 0, 1, 0), 6000));
}

// A rejected attempt is not charged: it neither restarts the source's interval
// nor uses the global budget.
void TestRejectedNotCharged()
{
    sv_rcon::Gate gate;
    const sv_rcon::Source source = Ip(192, 0, 2, 9);
    CHECK(gate.Allow(source, 0));
    for (int t = 1; t < 500; t += 50)
        CHECK(!gate.Allow(source, t));
    CHECK(gate.Allow(source, 500));
    for (uint32_t i = 2; i < sv_rcon::Gate::kGlobalAttempts; ++i)
        CHECK(gate.Allow(Ip(10, 1, 0, static_cast<uint8_t>(i)), 600));
    CHECK(!gate.Allow(Ip(10, 1, 1, 1), 700));
}

// More sources than slots: a new source takes the stalest slot, so the most
// recent source keeps its slot and stays throttled.
void TestSlotReuse()
{
    sv_rcon::Gate gate;
    int32_t now = 0;
    for (size_t i = 0; i < sv_rcon::Gate::kTrackedSources; ++i)
    {
        now += 100; // 10 per second stays within the global cap
        CHECK(gate.Allow(Ip(172, 16, 0, static_cast<uint8_t>(i)), now));
    }
    const sv_rcon::Source newest = Ip(172, 16, 0, static_cast<uint8_t>(sv_rcon::Gate::kTrackedSources - 1));
    now += 100;
    CHECK(gate.Allow(Ip(172, 17, 0, 0), now));
    now += 100;
    CHECK(!gate.Allow(newest, now));
}

// The millisecond clock wraps; intervals are measured modulo 2^32.
void TestClockWrap()
{
    sv_rcon::Gate gate;
    const sv_rcon::Source source = Ip(192, 0, 2, 1);
    CHECK(gate.Allow(source, INT32_MAX - 100));
    CHECK(!gate.Allow(source, INT32_MIN + 100));
    CHECK(gate.Allow(source, INT32_MIN + 400));
}

// The address type is part of the key: loopback and an all-zero IP are
// different sources.
void TestTypeIsPartOfKey()
{
    sv_rcon::Gate gate;
    sv_rcon::Source loopback;
    loopback.type = 2; // NA_LOOPBACK
    const sv_rcon::Source ip = Ip(0, 0, 0, 0);
    CHECK(gate.Allow(loopback, 0));
    CHECK(gate.Allow(ip, 1));
    CHECK(!gate.Allow(loopback, 2));
}
} // namespace

int main()
{
    TestPassword();
    TestPerSource();
    TestGlobalCap();
    TestRejectedNotCharged();
    TestSlotReuse();
    TestClockWrap();
    TestTypeIsPartOfKey();
    if (g_failures)
    {
        std::fprintf(stderr, "%d rcon gate check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("rcon gate contracts: ok");
    return 0;
}
