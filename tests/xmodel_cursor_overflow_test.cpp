// xmodel_cursor_overflow_test: fail-closed contracts for the buf_cursor
// save-stack bound (ki-okmr / #124).
//
// The scoped cursor save stack is bounded; activation past the bound
// must fail closed instead of silently dropping save state. These
// contracts pin the exact overflow semantics:
//
//   * scopes at and below the bound are all live and un-failed;
//   * the first scope past the bound is installed pre-failed and its
//     reads fail closed;
//   * while an overflow scope is open the depth is pinned at the bound
//     (overflow scopes push no save slot), so every deeper activation
//     overflows too, and the open-overflow count tracks the frames;
//   * an overflow scope's Deactivate pops nothing, but it must NOT drop
//     its caller into the unbounded legacy fallback either: the
//     overflowed caller's own cursor state was destroyed, so its
//     Deactivate leaves it active and pre-failed — failure latched,
//     bounded zero reads, anchored *pos write-back on a real pointer —
//     until its own Deactivate restores the nearest pushed ancestor;
//   * a sibling re-activation inside the overflowed frame overflows
//     again instead of taking the top-level path, which would push no
//     save and let its Deactivate steal the caller's pop (parent
//     receiving grandparent state);
//   * the scopes below unwind strictly LIFO and every parent restores
//     its own state.
//
// This suite is split from xmodel_nested_cursor_test.cpp: the overflow
// contracts need no fixtures and share no state with the restore /
// rewind contracts, and the split keeps each TU within the file-size
// budget with every helper individually readable. All assertions are
// retained in their original execution order.

#include <xanim/buf_cursor.h>

#include <cstdio>
#include <cstring>

namespace xmodel_cursor_overflow_test
{
namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "xmodel_cursor_overflow_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// One activated cursor scope: a private 4-byte domain and its anchored
// position slot — exactly the state a parent must get restored.
struct ScopeProbe
{
    unsigned char buffer[4];
    unsigned char *pos;
};

// The save stack holds 16 pushed frames: the first activation is the
// top level (pushes nothing), scopes 2..17 fill the stack exactly.
constexpr int kScopes = 17;

// Scope setup: activate kScopes nested scopes, each anchored to its own
// probe. Every scope at or below the bound must come up live and
// un-failed — only the next one (past the bound) is pre-failed.
void FillScopeStackToBound(ScopeProbe *probes)
{
    for (int i = 0; i < kScopes; ++i)
    {
        std::memset(probes[i].buffer, 0, sizeof(probes[i].buffer));
        probes[i].pos = probes[i].buffer;
        buf_cursor::Activate(probes[i].buffer, sizeof(probes[i].buffer));
        buf_cursor::AnchorPos(&probes[i].pos);
        CHECK(buf_cursor::Current() != nullptr);
        CHECK(!buf_cursor::Failed());
    }
}

// The bound is full: the next activation is installed pre-failed (its
// reads fail closed), and a deeper activation while an overflow scope
// is open overflows too — the depth is pinned at the bound because
// overflow scopes push no slot, so the open-overflow count tracks both
// frames. The inner overflow scope deactivates without popping.
void ExpectOverflowActivationsFailClosed()
{
    unsigned char overflowBuffer[4] = {};
    buf_cursor::Activate(overflowBuffer, sizeof(overflowBuffer));
    CHECK(buf_cursor::Failed());  // installed pre-failed
    char out[8];
    CHECK(!buf_cursor::ReadString(out, sizeof(out)));  // fails closed

    unsigned char overflowBuffer2[4] = {};
    buf_cursor::Activate(overflowBuffer2, sizeof(overflowBuffer2));
    CHECK(buf_cursor::Failed());
    CHECK(buf_cursor::ReadWeight() == 0);  // bounded zero read
    CHECK(buf_cursor::Failed());
    buf_cursor::Deactivate();
}

// The inner overflow scope's Deactivate popped nothing, so this
// Deactivate pops nothing too and must NOT go inactive: the overflowed
// caller (scope 17) stays active and pre-failed, so Failed() remains
// latched and its reads stay bounded zeros instead of decaying into the
// unbounded legacy fallback. The anchored *pos write-back stays on a
// real pointer (the pre-failed cursor's empty domain), never nullptr.
void ExpectOverflowedCallerLatchesFailure(ScopeProbe *probes)
{
    buf_cursor::Deactivate();
    const buf_cursor::BufCursor *overflowedCaller = buf_cursor::Current();
    CHECK(overflowedCaller != nullptr);  // still active — no unbounded fallback
    CHECK(buf_cursor::Failed());         // failure latched through the caller
    float zero = Buf_Read<float>(&probes[kScopes - 1].pos);
    CHECK(zero == 0.0f);
    CHECK(buf_cursor::Failed());
    CHECK(probes[kScopes - 1].pos != nullptr);
}

// Sibling re-activation accounting: the surviving (overflowed) frame
// starts another parse window, exactly like a production loader parsing
// a sibling sub-asset after a failed one. The save depth is still
// pinned at the bound, so this activation must overflow again — NOT
// take the top-level path, which would push no save and let its
// Deactivate steal scope 17's pop, shifting every remaining restore one
// level (parent receiving grandparent state).
void ExpectSiblingReactivationOverflows()
{
    unsigned char siblingBuffer[4] = {};
    buf_cursor::Activate(siblingBuffer, sizeof(siblingBuffer));
    CHECK(buf_cursor::Failed());  // overflow again, not a fresh top-level cursor
    buf_cursor::Deactivate();
    // Still the same fail-closed caller frame; the save stack did not move.
    CHECK(buf_cursor::Current() != nullptr);
    CHECK(buf_cursor::Failed());
}

// Unwind the kScopes real scopes: each Deactivate restores its own
// parent, in order, all un-failed and at their own buffer start.
void UnwindScopeStack(ScopeProbe *probes)
{
    for (int i = kScopes - 1; i >= 0; --i)
    {
        buf_cursor::Deactivate();
        if (i == 0)
        {
            // The bottom scope's Deactivate clears the thread-local.
            CHECK(buf_cursor::Current() == nullptr);
            break;
        }
        const buf_cursor::BufCursor *parent = buf_cursor::Current();
        CHECK(parent != nullptr);
        CHECK(parent->begin == probes[i - 1].buffer);
        CHECK(parent->current == probes[i - 1].buffer);
        CHECK(probes[i - 1].pos == probes[i - 1].buffer);
        CHECK(!buf_cursor::Failed());
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Contract: the save stack is bounded. One scope past the bound is
// installed pre-failed; while any overflow scope is open the depth is
// pinned at the bound, so every deeper activation overflows too and the
// open-overflow count tracks the frames. An overflow scope's Deactivate
// pops nothing, but it must NOT drop its caller into the unbounded
// legacy fallback either: the overflowed caller's own cursor state was
// destroyed, so its Deactivate leaves it active and pre-failed —
// failure latched, bounded zero reads — until its own Deactivate
// restores the nearest pushed ancestor. The scopes below unwind in
// order and every parent restores its own state, including after a
// sibling re-activation inside the overflowed frame.
// ---------------------------------------------------------------------------
bool TestScopeOverflowFailsClosed()
{
    // Activate 17 nested scopes: the first pushes nothing (top level),
    // scopes 2..17 fill the 16-slot stack exactly.
    ScopeProbe probes[kScopes] = {};
    FillScopeStackToBound(probes);

    // Scope 18: the save stack is full — installed pre-failed, pops
    // nothing on Deactivate. Scope 19, nested inside it, overflows too.
    ExpectOverflowActivationsFailClosed();

    // Scope 18's Deactivate leaves the overflowed caller active and
    // pre-failed, its reads bounded zeros.
    ExpectOverflowedCallerLatchesFailure(probes);

    // A sibling window in the overflowed frame overflows again instead
    // of taking the top-level path.
    ExpectSiblingReactivationOverflows();

    // Unwind the 17 real scopes: LIFO, every parent restored exactly.
    UnwindScopeStack(probes);
    CHECK(buf_cursor::Current() == nullptr);
    return true;
}

int RunAll()
{
    CHECK(TestScopeOverflowFailsClosed());

    std::fprintf(stderr, "xmodel_cursor_overflow_test: %d/%d passed\n", g_runs - g_failures, g_runs);
    return g_failures == 0 ? 0 : 1;
}
}  // namespace xmodel_cursor_overflow_test

int main()
{
    return xmodel_cursor_overflow_test::RunAll();
}
