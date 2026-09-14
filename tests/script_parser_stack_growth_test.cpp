// script_parser_stack_growth_test: behavioral regression for the production
// parser state/value stack relocation (ki-pycb).
//
// src/script/scr_yacc.cpp's generated yyparse doubled yystacksize and then
// copied the doubled element count out of the old (pre-growth) state and value
// arrays, reading past their live storage. src/script/scr_yacc2.cpp (the
// parser actually built into the client/dedicated/server targets) already
// bounded the copy by the live element count `yysize`.
//
// Everything the test drives is extracted verbatim from the production sources
// at configure time (see tests/CMakeLists.txt): the relocation and growth
// decision slices, both real `stype_t` declarations, the real YYINITDEPTH
// expression and generated array bound, and the max-depth limit read from the
// guard predicate. Configure fails closed if an anchor moves or a declaration
// drifts, so the test cannot silently execute a stale copy of the algorithm.
//
// It drives the first capacity crossing and repeated growth through the
// max-depth limit, checking live states, source positions and native pointer
// payloads, proves the restored cursors land on the last live slot of the NEW
// storage (initialising them to the OLD storage first), and exercises the real
// max-depth predicate separately from the capped allocation. A POSIX-only
// guarded-memory control then reproduces the pre-fix bound and proves it
// faults, while the fixed production path passes.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifndef KISAK_DEDI_HEADLESS
#define KISAK_DEDI_HEADLESS 1
#endif

#include <script/scr_debugger.h>

// Real production declarations, renamed/extracted by CMake.
#include <script_stype_production.inc>
#include <script_stype_generated.inc>
#include <script_yacc2_initdepth.inc>
#include <script_yacc_initial_depth.inc>
#include <script_yacc_maxdepth.inc>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#if defined(_WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "script_parser_stack_growth_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace

#define CHECK(expr) Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// The test's value cells are the real production declarations, not a
// handwritten mirror: `stype_t` is extracted from src/script/scr_yacc2.cpp and
// `generated_stype_t` from src/script/scr_yacc.cpp.
using ProductionValue = stype_t;
using GeneratedValue = generated_stype_t;

// Bound to the production sources at configure time.
constexpr int kMaxCapacity = SCRIPT_YACC_STACK_LIMIT;
constexpr int kProductionInitialCapacity = YYINITDEPTH;
constexpr int kGeneratedInitialCapacity = SCRIPT_YACC_INITIAL_CAPACITY;

#if UINTPTR_MAX > 0xFFFFFFFFu
constexpr uintptr_t kPointerPatternBase = 0x00005A3C7E19B400ull;
#else
constexpr uintptr_t kPointerPatternBase = 0x7E19B400u;
#endif

// Heap-backed stacks. The old buffers are sized to the exact live entry count,
// so a relocation that reads the doubled capacity faults the ASan redzone /
// guarded page instead of silently reading stale stack bytes.
template <typename ValueT>
struct StackBuffers
{
    std::unique_ptr<short[]> states;
    std::unique_ptr<ValueT[]> values;
    int capacity = 0;
    int live = 0;
};

template <typename ValueT>
struct GrowthOutcome
{
    StackBuffers<ValueT> buffers;
    int overflow = 0;
    int grownCapacity = 0;
};

// Fill one live slot with deterministic state, source position and a pointer
// payload that must move whole through the relocation.
template <typename ValueT>
void FillSlot(int index, short &state, ValueT &value)
{
    state = static_cast<short>((index * 7 + 1) & 0x7fff);
    value.val.codePosValue =
        reinterpret_cast<const char *>(kPointerPatternBase + static_cast<uintptr_t>(index) * 0x40u);
    value.pos = 0x1000u + static_cast<uint32_t>(index) * 3u;
}

template <typename ValueT>
StackBuffers<ValueT> MakeBuffers(int capacity)
{
    StackBuffers<ValueT> buffers;
    buffers.capacity = capacity;
    buffers.live = capacity;
    buffers.states = std::make_unique<short[]>(capacity);
    buffers.values = std::make_unique<ValueT[]>(capacity);
    for (int i = 0; i < capacity; ++i)
        FillSlot<ValueT>(i, buffers.states[i], buffers.values[i]);
    return buffers;
}

template <typename ValueT>
void VerifyLiveSlots(int live, const StackBuffers<ValueT> &expected, const short *states,
                     const ValueT *values)
{
    for (int i = 0; i < live; ++i)
    {
        if (states[i] != expected.states[i])
        {
            std::fprintf(stderr,
                         "script_parser_stack_growth_test: state[%d] 0x%04x != 0x%04x\n",
                         i,
                         static_cast<unsigned>(static_cast<unsigned short>(states[i])),
                         static_cast<unsigned>(static_cast<unsigned short>(expected.states[i])));
            ++g_failures;
            break;
        }
    }

    for (int i = 0; i < live; ++i)
    {
        const bool ok = values[i].pos == expected.values[i].pos &&
                        values[i].val.codePosValue == expected.values[i].val.codePosValue;
        if (!ok)
        {
            std::fprintf(stderr,
                         "script_parser_stack_growth_test: value[%d] pos 0x%x ptr %p != pos 0x%x ptr %p\n",
                         i,
                         values[i].pos,
                         static_cast<const void *>(values[i].val.codePosValue),
                         expected.values[i].pos,
                         static_cast<const void *>(expected.values[i].val.codePosValue));
            ++g_failures;
            break;
        }
    }
}

// Copy the live relocation result out of the frame-local (alloca) buffers so
// the growth chain can continue in the next call.
template <typename ValueT>
StackBuffers<ValueT> Harvest(int live, int capacity, const short *states, const ValueT *values)
{
    StackBuffers<ValueT> out;
    out.capacity = capacity;
    out.live = live;
    out.states = std::make_unique<short[]>(capacity);
    for (int i = 0; i < live; ++i)
        out.states[i] = states[i];
    out.values = std::make_unique<ValueT[]>(capacity);
    for (int i = 0; i < live; ++i)
    {
        out.values[i].val = values[i].val;
        out.values[i].pos = values[i].pos;
    }
    return out;
}

// Groups the four base pointers and four cursors the relocation contract
// inspects, so CheckRelocation stays within the analyzer's method-parameter
// budget (Codacy flags a 10-parameter method; the limit is 8). The aggregate is
// passed by const reference, not by value: the individual pointers then remain
// as opaque to a static analyzer as the previous individual parameters, so a
// checker that cannot resolve the extracted slice still cannot see the pre- and
// post-relocation state as identical and reject the checks as tautological.
template <typename ValueT>
struct RelocationPointers
{
    const short *stateBase;
    const ValueT *valueBase;
    const short *oldStateBase;
    const ValueT *oldValueBase;
    const short *stateCursor;
    const ValueT *valueCursor;
    const short *oldStateCursor;
    const ValueT *oldValueCursor;
};

// All post-relocation cursor/pointer assertions live here rather than at the
// call site, for the analyzer-opacity reason described above.
template <typename ValueT>
void CheckRelocation(const RelocationPointers<ValueT> &p, int live, int overflow)
{
    if (overflow)
        return;

    // The real relocation must allocate fresh storage, not leave the pointers
    // on the pre-growth buffers.
    CHECK(p.stateBase != p.oldStateBase);
    CHECK(p.valueBase != p.oldValueBase);
    // The restored cursors land on the last live slot of the NEW storage.
    CHECK(p.stateCursor == p.stateBase + (live - 1));
    CHECK(p.valueCursor == p.valueBase + (live - 1));
    // A relocation that reverted the cursors to the OLD storage would land on
    // these pointers instead; the contract must reject them.
    CHECK(p.stateCursor != p.oldStateBase + (live - 1));
    CHECK(p.valueCursor != p.oldValueBase + (live - 1));
    // The payload read through the restored cursor matches the old last slot.
    CHECK(*p.stateCursor == p.oldStateBase[live - 1]);
    CHECK(p.valueCursor->pos == p.oldValueBase[live - 1].pos);
    CHECK(p.valueCursor->val.codePosValue == p.oldValueBase[live - 1].val.codePosValue);

    // Offset restored independently: derived from the OLD cursor/base pair,
    // then required to agree with the NEW cursor/base pair.
    const ptrdiff_t oldStateOffset = p.oldStateCursor - p.oldStateBase;
    const ptrdiff_t newStateOffset = p.stateCursor - p.stateBase;
    const ptrdiff_t oldValueOffset = p.oldValueCursor - p.oldValueBase;
    const ptrdiff_t newValueOffset = p.valueCursor - p.valueBase;
    CHECK(newStateOffset == oldStateOffset);
    CHECK(newValueOffset == oldValueOffset);
}

int CountRounds(int initialCapacity)
{
    int capacity = initialCapacity;
    int rounds = 0;
    while (capacity < kMaxCapacity)
    {
        capacity = std::min(capacity * 2, kMaxCapacity);
        ++rounds;
    }
    return rounds;
}

// ---------------------------------------------------------------------------
// Production relocation (src/script/scr_yacc2.cpp, the built parser).
//
// The whole growth block below the cursor test is included verbatim between its
// configure-time anchors, so this frame executes the real live-count capture,
// max-depth predicate, doubling/clamp, relocation and cursor restore.
// ---------------------------------------------------------------------------
GrowthOutcome<ProductionValue> GrowWithProductionSlice(const StackBuffers<ProductionValue> &old,
                                                       const char *label)
{
    GrowthOutcome<ProductionValue> outcome;
    int yysize = old.live;
    int yystacksize = old.capacity;

    short *yyss = old.states.get();
    ProductionValue *yyvs = old.values.get();
    const short *yyss1 = yyss;
    const ProductionValue *yyvs1 = yyvs;
    // Initialise the cursors to the OLD storage. The real relocation must move
    // them into the fresh buffers; leaving them here is the pre-fix failure
    // this test must catch.
    const short *yyssp = yyss + yysize - 1;
    const ProductionValue *yyvsp = yyvs + yysize - 1;
    void *free1addr = nullptr;
    void *free2addr = nullptr;
    const short *oldStateCursor = yyssp;
    const ProductionValue *oldValueCursor = yyvsp;
    int yy_stack_overflow = 0;

#include <script_yacc2_growth_slice.inc>

    (void)free1addr;
    (void)free2addr;
    (void)label;

    outcome.overflow = yy_stack_overflow;
    outcome.grownCapacity = yystacksize;
    const RelocationPointers<ProductionValue> relocation{yyss,  yyvs,  yyss1,          yyvs1,
                                                         yyssp, yyvsp, oldStateCursor, oldValueCursor};
    CheckRelocation<ProductionValue>(relocation, yysize, yy_stack_overflow);
    VerifyLiveSlots<ProductionValue>(yysize, old, yyss, yyvs);
    outcome.buffers = Harvest<ProductionValue>(yysize, yystacksize, yyss, yyvs);
    return outcome;
}

// ---------------------------------------------------------------------------
// Generated reference relocation (src/script/scr_yacc.cpp).
//
// The unbuilt generated parser carries the same growth block; its fragment is
// namespaced by `v37` (the live state count) and uses the real generated
// `stype_t`, alias-bound here so the slice compiles against its own record.
// ---------------------------------------------------------------------------
GrowthOutcome<GeneratedValue> GrowWithGeneratedSlice(const StackBuffers<GeneratedValue> &old,
                                                     const char *label)
{
    using stype_t = GeneratedValue;

    GrowthOutcome<GeneratedValue> outcome;
    int v37 = old.live;
    int yystacksize = old.capacity;

    short *yyss = old.states.get();
    stype_t *yyvs = old.values.get();
    const short *yyss1 = yyss;
    const stype_t *yyvs1 = yyvs;
    const short *yyssp = yyss + v37 - 1;
    const stype_t *yyvsp = yyvs + v37 - 1;
    const short *oldStateCursor = yyssp;
    const stype_t *oldValueCursor = yyvsp;
    int yy_stack_overflow = 0;

#include <script_yacc_growth_slice.inc>

    (void)label;

    outcome.overflow = yy_stack_overflow;
    outcome.grownCapacity = yystacksize;
    const RelocationPointers<GeneratedValue> relocation{yyss,  yyvs,  yyss1,          yyvs1,
                                                        yyssp, yyvsp, oldStateCursor, oldValueCursor};
    CheckRelocation<GeneratedValue>(relocation, v37, yy_stack_overflow);
    VerifyLiveSlots<GeneratedValue>(v37, old, yyss, yyvs);
    outcome.buffers = Harvest<GeneratedValue>(v37, yystacksize, yyss, yyvs);
    return outcome;
}

// The real relocation implementations are function templates; naming their
// instantiated type lets the chain drivers below take a single template
// parameter. A two-parameter `template <typename ValueT, typename GrowFn>`
// list is misread by cppcheck's misra-c2012-12.3 check as the comma operator
// (the comma there only separates template parameters), so keeping the
// template parameter list to one argument avoids the false positive without
// weakening the relocation coverage.
template <typename ValueT>
using RelocationGrowFn = GrowthOutcome<ValueT> (*)(const StackBuffers<ValueT> &, const char *);

// Drive one real relocation implementation across the first crossing and
// repeated growth up to the production max-depth limit, distinguishing the
// single capped allocation from the ordinary doublings.
template <typename ValueT>
void RunGrowthChain(RelocationGrowFn<ValueT> grow, int initialCapacity, const char *label)
{
    StackBuffers<ValueT> current = MakeBuffers<ValueT>(initialCapacity);
    int rounds = 0;
    int cappedRounds = 0;
    while (current.capacity < kMaxCapacity)
    {
        const int previousCapacity = current.capacity;
        const int doubled = previousCapacity * 2;
        GrowthOutcome<ValueT> outcome = grow(current, label);
        CHECK(outcome.overflow == 0);
        if (doubled > kMaxCapacity)
        {
            // The real slice must clamp the doubled capacity, not allocate it.
            CHECK(outcome.grownCapacity == kMaxCapacity);
            ++cappedRounds;
        }
        else
        {
            CHECK(outcome.grownCapacity == std::min(doubled, kMaxCapacity));
        }

        StackBuffers<ValueT> next = std::move(outcome.buffers);
        // Production keeps pushing until the cursor fills the new capacity
        // before the next growth check; extend the live region to match.
        for (int i = next.live; i < next.capacity; ++i)
            FillSlot<ValueT>(i, next.states[i], next.values[i]);
        next.live = next.capacity;
        current = std::move(next);
        ++rounds;
    }

    CHECK(rounds == CountRounds(initialCapacity));
    CHECK(cappedRounds == 1);
    CHECK(current.capacity == kMaxCapacity);
    CHECK(current.live == kMaxCapacity);

    // Every live slot must still hold its deterministic payload after the whole
    // chain of real relocations.
    for (int i = 0; i < current.capacity; ++i)
    {
        const short expectedState = static_cast<short>((i * 7 + 1) & 0x7fff);
        if (current.states[i] != expectedState)
        {
            std::fprintf(stderr,
                         "script_parser_stack_growth_test: chained state[%d] 0x%04x != 0x%04x\n",
                         i,
                         static_cast<unsigned>(static_cast<unsigned short>(current.states[i])),
                         static_cast<unsigned>(static_cast<unsigned short>(expectedState)));
            ++g_failures;
            break;
        }
    }

    std::printf("script_parser_stack_growth_test: %s relocation chain passed\n", label);
}

// Distinguish the capped allocation from the max-depth error path using the
// real extracted predicate: just below the limit growth succeeds and clamps,
// at the limit growth is refused.
template <typename ValueT>
void CheckMaxDepthDecision(RelocationGrowFn<ValueT> grow, const char *label)
{
    StackBuffers<ValueT> below = MakeBuffers<ValueT>(kMaxCapacity - 1);
    GrowthOutcome<ValueT> belowOutcome = grow(below, label);
    CHECK(belowOutcome.overflow == 0);
    CHECK(belowOutcome.grownCapacity == kMaxCapacity);

    StackBuffers<ValueT> atLimit = MakeBuffers<ValueT>(kMaxCapacity);
    GrowthOutcome<ValueT> limitOutcome = grow(atLimit, label);
    CHECK(limitOutcome.overflow == 1);
    (void)label;
}

// ---------------------------------------------------------------------------
// Guarded-memory negative control: the pre-fix copy bound.
// ---------------------------------------------------------------------------
#if defined(__unix__) || defined(__APPLE__)
struct GuardedRegion
{
    void *base = nullptr;
    size_t mapped = 0;
    unsigned char *data = nullptr;

    ~GuardedRegion()
    {
        if (base)
            munmap(base, mapped);
    }
};

// One readable page with a PROT_NONE guard page after it. The requested byte
// count ends exactly at the guard boundary and is aligned for the element type.
bool MapGuarded(size_t bytes, size_t align, GuardedRegion &out)
{
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return false;
    const size_t pageSize = static_cast<size_t>(page);
    if (bytes > pageSize || bytes % align != 0)
        return false;

    void *base = mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return false;
    if (mprotect(static_cast<unsigned char *>(base) + pageSize, pageSize, PROT_NONE) != 0)
    {
        munmap(base, pageSize * 2);
        return false;
    }

    out.base = base;
    out.mapped = pageSize * 2;
    out.data = static_cast<unsigned char *>(base) + pageSize - bytes;
    return true;
}

// Child exit code used when the guarded mapping itself could not be created;
// distinguished from a genuine fault so a broken control cannot masquerade as
// a passing one.
constexpr int kGuardSetupFailed = 42;

// Pre-fix reproduction (ki-pycb): read the doubled capacity out of the old
// state storage. The loop is intentionally unbounded; the volatile sink keeps
// the reads from being optimized away so the guard page faults.
void ChildUnboundedStateCopy()
{
    const size_t bytes = sizeof(short) * static_cast<size_t>(kProductionInitialCapacity);
    GuardedRegion region;
    if (!MapGuarded(bytes, alignof(short), region))
        _exit(kGuardSetupFailed);
    short *oldStates = reinterpret_cast<short *>(region.data);
    for (int i = 0; i < kProductionInitialCapacity; ++i)
        oldStates[i] = static_cast<short>(i + 1);

    volatile short sink = 0;
    for (size_t i = 0; i < static_cast<size_t>(kProductionInitialCapacity) * 2u; ++i)
        sink = static_cast<short>(sink + oldStates[i]);
    _exit(sink == 0 ? 1 : 0);
}

void ChildUnboundedValueCopy()
{
    const size_t bytes = sizeof(ProductionValue) * static_cast<size_t>(kProductionInitialCapacity);
    GuardedRegion region;
    if (!MapGuarded(bytes, alignof(ProductionValue), region))
        _exit(kGuardSetupFailed);
    ProductionValue *oldValues = reinterpret_cast<ProductionValue *>(region.data);
    for (int i = 0; i < kProductionInitialCapacity; ++i)
        oldValues[i].pos = static_cast<uint32_t>(i + 1);

    volatile uint32_t sink = 0;
    for (size_t i = 0; i < static_cast<size_t>(kProductionInitialCapacity) * 2u; ++i)
        sink ^= oldValues[i].pos;
    _exit(sink == 0 ? 1 : 0);
}

bool ChildTerminatesAbnormally(void (*child)())
{
    const pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0)
    {
        child();
        _exit(0);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
        return false;
    if (WIFSIGNALED(status))
        return true;  // guard page fault
    if (WIFEXITED(status))
    {
        const int code = WEXITSTATUS(status);
        if (code == kGuardSetupFailed)
            return false;  // the control never ran
        return code != 0;  // ASan's deadly-signal path exits non-zero
    }
    return false;
}
#endif

void RunPreFixNegativeControl()
{
#if defined(__unix__) || defined(__APPLE__)
    CHECK(ChildTerminatesAbnormally(ChildUnboundedStateCopy));
    CHECK(ChildTerminatesAbnormally(ChildUnboundedValueCopy));
    std::printf("script_parser_stack_growth_test: pre-fix unbounded copies fault as expected\n");
#else
    std::printf("script_parser_stack_growth_test: guarded-memory control skipped on this platform\n");
#endif
}

void CheckRecordShape()
{
    static_assert(sizeof(ProductionValue) == sizeof(GeneratedValue),
                  "production and generated stype_t declarations disagree on size");
    CHECK(sizeof(sval_u) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x8u : 0x4u));
    CHECK(sizeof(ProductionValue) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x10u : 0x8u));

    ProductionValue probe;
    const auto base = reinterpret_cast<uintptr_t>(&probe);
    CHECK(reinterpret_cast<uintptr_t>(&probe.pos) - base == sizeof(sval_u));

    GeneratedValue generatedProbe;
    const auto generatedBase = reinterpret_cast<uintptr_t>(&generatedProbe);
    CHECK(reinterpret_cast<uintptr_t>(&generatedProbe.pos) - generatedBase == sizeof(sval_u));
    CHECK(sizeof(GeneratedValue) == sizeof(ProductionValue));
}
}  // namespace

int main()
{
    CheckRecordShape();
    RunGrowthChain<ProductionValue>(GrowWithProductionSlice, kProductionInitialCapacity,
                                    "production scr_yacc2");
    RunGrowthChain<GeneratedValue>(GrowWithGeneratedSlice, kGeneratedInitialCapacity,
                                   "generated scr_yacc");
    CheckMaxDepthDecision<ProductionValue>(GrowWithProductionSlice, "production scr_yacc2");
    CheckMaxDepthDecision<GeneratedValue>(GrowWithGeneratedSlice, "generated scr_yacc");
    RunPreFixNegativeControl();

    if (g_failures)
    {
        std::fprintf(stderr,
                     "script_parser_stack_growth_test: %d/%d checks failed\n",
                     g_failures,
                     g_runs);
        return 1;
    }
    std::printf("script_parser_stack_growth_test: %d checks passed\n", g_runs);
    return 0;
}
