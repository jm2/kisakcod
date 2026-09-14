// script_parser_stack_growth_test: behavioral regression for the production
// parser state/value stack relocation (ki-pycb).
//
// src/script/scr_yacc.cpp's generated yyparse doubled yystacksize and then
// copied the doubled element count out of the old (pre-growth) state and value
// arrays, reading past their live storage. src/script/scr_yacc2.cpp (the
// parser actually built into the client/dedicated/server targets) already
// bounded the copy by the live element count `yysize`.
//
// The relocation statements under test are extracted verbatim from the
// production sources at configure time (see tests/CMakeLists.txt), so this TU
// compiles and runs the real copy and cursor-restore logic against the real
// `sval_u` value cell and the real `short` state cell rather than a copy of
// the algorithm. It drives the first capacity crossing and repeated growth
// through the 10000 cap, checks that live states, source positions and native
// pointer payloads survive, and confirms both cursors land on the last live
// slot.
//
// A POSIX-only guarded-memory control then reproduces the pre-fix bound (copy
// the doubled capacity) and proves it faults, while the fixed production path
// passes. Under ASan the fixed path is additionally checked by the exact-sized
// old buffers used for every growth round.

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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

// stype_t mirrors the production declaration in src/script/scr_yacc.cpp and
// src/script/scr_yacc2.cpp. Pin its layout against the real widened value
// cell so a header/ABI change cannot silently invalidate the slice.
struct stype_t
{
    sval_u val;
    uint32_t pos;
};

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
constexpr int kInitialCapacity = 200;
constexpr int kMaxCapacity = 10000;

#if UINTPTR_MAX > 0xFFFFFFFFu
constexpr uintptr_t kPointerPatternBase = 0x00005A3C7E19B400ull;
#else
constexpr uintptr_t kPointerPatternBase = 0x7E19B400u;
#endif

// Heap-backed old stacks. The old buffers are sized to the exact live entry
// count, so a relocation that reads the doubled capacity faults the ASan
// redzone / guarded page instead of silently reading stale stack bytes.
struct StackBuffers
{
    std::unique_ptr<short[]> states;
    std::unique_ptr<stype_t[]> values;
    int capacity = 0;
    int live = 0;
};

// Fill one live slot with deterministic state, source position and a pointer
// payload that must move whole through the relocation.
void FillSlot(int index, short &state, stype_t &value)
{
    state = static_cast<short>((index * 7 + 1) & 0x7fff);
    value.val.codePosValue =
        reinterpret_cast<const char *>(kPointerPatternBase + static_cast<uintptr_t>(index) * 0x40u);
    value.pos = 0x1000u + static_cast<uint32_t>(index) * 3u;
}

StackBuffers MakeBuffers(int capacity)
{
    StackBuffers buffers;
    buffers.capacity = capacity;
    buffers.live = capacity;
    buffers.states = std::make_unique<short[]>(capacity);
    buffers.values = std::make_unique<stype_t[]>(capacity);
    for (int i = 0; i < capacity; ++i)
        FillSlot(i, buffers.states[i], buffers.values[i]);
    return buffers;
}

void VerifyLiveSlots(int live, const StackBuffers &expected, const short *states, const stype_t *values)
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

int ExpectedNextCapacity(int capacity)
{
    const int doubled = capacity * 2;
    return doubled > kMaxCapacity ? kMaxCapacity : doubled;
}

// ---------------------------------------------------------------------------
// Production relocation (src/script/scr_yacc2.cpp, the built parser).
//
// The relocation fragment below is included verbatim between its configure-time
// anchors. `yysize`/`yyss1`/`yyvs1` are prepared here exactly as production
// prepares them before the growth block: yysize is the live state cursor count
// and yyss1/yyvs1 capture the old arrays before the new alloca buffers replace
// them. The alloca buffers stay alive through the verification in this frame.
// ---------------------------------------------------------------------------
StackBuffers GrowWithProductionSlice(const StackBuffers &old, const char *label)
{
    const int expectedCapacity = ExpectedNextCapacity(old.capacity);
    const int yysize = old.live;
    int yystacksize = old.capacity;
    yystacksize *= 2;
    if (yystacksize > kMaxCapacity)
        yystacksize = kMaxCapacity;

    short *yyss = old.states.get();
    stype_t *yyvs = old.values.get();
    short *yyss1 = yyss;
    stype_t *yyvs1 = yyvs;
    short *yyssp = yyss + yysize - 1;
    stype_t *yyvsp = yyvs + yysize - 1;
    void *free1addr = nullptr;
    void *free2addr = nullptr;

#include <script_yacc2_growth_slice.inc>

    CHECK(yystacksize == expectedCapacity);
    CHECK(yyssp == yyss + yysize - 1);
    CHECK(yyvsp == yyvs + yysize - 1);
    VerifyLiveSlots(yysize, old, yyss, yyvs);
    (void)free1addr;
    (void)free2addr;
    (void)label;

    StackBuffers relocated;
    relocated.capacity = yystacksize;
    relocated.live = yysize;
    relocated.states = std::make_unique<short[]>(yystacksize);
    std::memcpy(relocated.states.get(), yyss, sizeof(short) * static_cast<size_t>(yysize));
    relocated.values = std::make_unique<stype_t[]>(yystacksize);
    std::memcpy(relocated.values.get(), yyvs, sizeof(stype_t) * static_cast<size_t>(yysize));
    return relocated;
}

// ---------------------------------------------------------------------------
// Generated reference relocation (src/script/scr_yacc.cpp).
//
// The unbuilt generated parser carries the same growth block; its fragment is
// namespaced by `v37` (the live state count) and has no free-address locals.
// ---------------------------------------------------------------------------
StackBuffers GrowWithGeneratedSlice(const StackBuffers &old, const char *label)
{
    const int expectedCapacity = ExpectedNextCapacity(old.capacity);
    const int v37 = old.live;
    int yystacksize = old.capacity;
    yystacksize *= 2;
    if (yystacksize > kMaxCapacity)
        yystacksize = kMaxCapacity;

    short *yyss = old.states.get();
    stype_t *yyvs = old.values.get();
    short *yyss1 = yyss;
    stype_t *yyvs1 = yyvs;
    short *yyssp = yyss + v37 - 1;
    stype_t *yyvsp = yyvs + v37 - 1;

#include <script_yacc_growth_slice.inc>

    CHECK(yystacksize == expectedCapacity);
    CHECK(yyssp == yyss + v37 - 1);
    CHECK(yyvsp == yyvs + v37 - 1);
    VerifyLiveSlots(v37, old, yyss, yyvs);
    (void)label;

    StackBuffers relocated;
    relocated.capacity = yystacksize;
    relocated.live = v37;
    relocated.states = std::make_unique<short[]>(yystacksize);
    std::memcpy(relocated.states.get(), yyss, sizeof(short) * static_cast<size_t>(v37));
    relocated.values = std::make_unique<stype_t[]>(yystacksize);
    std::memcpy(relocated.values.get(), yyvs, sizeof(stype_t) * static_cast<size_t>(v37));
    return relocated;
}

using GrowFn = StackBuffers (*)(const StackBuffers &, const char *);

// Drive one relocation implementation across the first crossing and repeated
// growth up to the 10000 cap.
void RunGrowthChain(GrowFn grow, const char *label)
{
    StackBuffers current = MakeBuffers(kInitialCapacity);
    int rounds = 0;
    while (current.capacity < kMaxCapacity)
    {
        current = grow(current, label);
        // Production keeps pushing until the cursor fills the new capacity
        // before the next growth check; extend the live region to match.
        for (int i = current.live; i < current.capacity; ++i)
            FillSlot(i, current.states[i], current.values[i]);
        current.live = current.capacity;
        ++rounds;
    }

    // 200 -> 400 -> 800 -> 1600 -> 3200 -> 6400 -> 10000.
    CHECK(rounds == 6);
    CHECK(current.capacity == kMaxCapacity);
    CHECK(current.live == kMaxCapacity);

    std::printf("script_parser_stack_growth_test: %s relocation chain passed\n", label);
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

// Pre-fix reproduction (ki-pycb): copy the doubled capacity out of the old
// state storage. Kept only as the negative control; the production slices
// above must never take this bound.
void KeepCopyObservable(const void *destination)
{
    // The child never inspects the destination before exiting, so without a
    // memory clobber the optimizer would drop the over-reading memcpy as a
    // dead store and the control would silently pass.
#if defined(__GNUC__)
    __asm__ __volatile__("" : : "r"(destination) : "memory");
#else
    (void)destination;
#endif
}

void ChildUnboundedStateCopy()
{
    const size_t bytes = sizeof(short) * static_cast<size_t>(kInitialCapacity);
    GuardedRegion region;
    if (!MapGuarded(bytes, alignof(short), region))
        _exit(kGuardSetupFailed);
    short *oldStates = reinterpret_cast<short *>(region.data);
    for (int i = 0; i < kInitialCapacity; ++i)
        oldStates[i] = static_cast<short>(i + 1);
    auto newStates = std::make_unique<short[]>(kInitialCapacity * 2);
    std::memcpy(newStates.get(), oldStates, sizeof(short) * static_cast<size_t>(kInitialCapacity) * 2u);
    KeepCopyObservable(newStates.get());
    _exit(0);
}

void ChildUnboundedValueCopy()
{
    const size_t bytes = sizeof(stype_t) * static_cast<size_t>(kInitialCapacity);
    GuardedRegion region;
    if (!MapGuarded(bytes, alignof(stype_t), region))
        _exit(kGuardSetupFailed);
    stype_t *oldValues = reinterpret_cast<stype_t *>(region.data);
    std::memset(oldValues, 0, bytes);
    auto newValues = std::make_unique<stype_t[]>(kInitialCapacity * 2);
    std::memcpy(newValues.get(), oldValues, sizeof(stype_t) * static_cast<size_t>(kInitialCapacity) * 2u);
    KeepCopyObservable(newValues.get());
    _exit(0);
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
    CHECK(sizeof(sval_u) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x8u : 0x4u));
    CHECK(sizeof(stype_t) == (UINTPTR_MAX > 0xFFFFFFFFu ? 0x10u : 0x8u));

    stype_t probe;
    const auto base = reinterpret_cast<uintptr_t>(&probe);
    CHECK(reinterpret_cast<uintptr_t>(&probe.pos) - base == sizeof(sval_u));
}
}  // namespace

int main()
{
    CheckRecordShape();
    RunGrowthChain(GrowWithProductionSlice, "production scr_yacc2");
    RunGrowthChain(GrowWithGeneratedSlice, "generated scr_yacc");
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
