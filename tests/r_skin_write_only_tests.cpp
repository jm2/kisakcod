// r_skin_write_only_tests.cpp: R_SkinXSurfaceWeight (gfx_d3d/r_model_skin.cpp)
// skins into the skin cache, a locked D3D9 vertex buffer. On a native driver
// that memory is write-combined, so every read from it stalls: the 1/2/3-bone
// loops read each vertex's position back to blend it, which held the Win64
// client at about 40 fps on mp_shipment. They must only write the output.
//
// The output buffer is a PROT_NONE page. Each access faults; the handler
// counts it as a read or a write (the page-fault error code), opens the page,
// and single-steps the instruction (TF) before closing the page again. Linux
// x86-64 only, for REG_ERR and TF.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches.

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <gfx_d3d/r_gfx.h>
#include <gfx_d3d/r_model_skin.h>
#include <xanim/dobj.h>
#include <xanim/xanim.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

constexpr int kVerts = 4; // one per bone count, 1 to 4
constexpr std::uint64_t kTrapFlag = 0x100;
constexpr long long kPageFaultWrite = 2;

void *g_page;
std::size_t g_pageSize;
volatile sig_atomic_t g_reads;
volatile sig_atomic_t g_writes;
struct sigaction g_oldSegv;
struct sigaction g_oldTrap;

void OnSegv(int sig, siginfo_t *info, void *raw)
{
    auto *uc = static_cast<ucontext_t *>(raw);
    auto *addr = static_cast<char *>(info->si_addr);
    auto *page = static_cast<char *>(g_page);
    if (addr < page || addr >= page + g_pageSize)
    {
        sigaction(sig, &g_oldSegv, nullptr); // not ours: let it crash
        return;
    }
    if (uc->uc_mcontext.gregs[REG_ERR] & kPageFaultWrite)
        ++g_writes;
    else
        ++g_reads;
    mprotect(g_page, g_pageSize, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= kTrapFlag;
}

void OnTrap(int, siginfo_t *, void *raw)
{
    auto *uc = static_cast<ucontext_t *>(raw);
    mprotect(g_page, g_pageSize, PROT_NONE);
    uc->uc_mcontext.gregs[REG_EFL] &= ~kTrapFlag;
}

void Install()
{
    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = OnSegv;
    sigaction(SIGSEGV, &sa, &g_oldSegv);
    sa.sa_sigaction = OnTrap;
    sigaction(SIGTRAP, &sa, &g_oldTrap);
}

void Uninstall()
{
    sigaction(SIGSEGV, &g_oldSegv, nullptr);
    sigaction(SIGTRAP, &g_oldTrap, nullptr);
    mprotect(g_page, g_pageSize, PROT_READ | PROT_WRITE);
}

// Identity axes: a bone only translates, so every expected value is exact.
DObjSkelMat Bone(float x, float y, float z)
{
    DObjSkelMat mat{};
    mat.axis[0][0] = mat.axis[1][1] = mat.axis[2][2] = 1.0f;
    mat.origin[0] = x;
    mat.origin[1] = y;
    mat.origin[2] = z;
    return mat;
}

constexpr std::uint16_t Offset(int bone) { return static_cast<std::uint16_t>(bone * sizeof(DObjSkelMat)); }
constexpr std::uint16_t kQuarter = 0x4000;
constexpr std::uint16_t kHalf = 0x8000;
} // namespace

WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}

int main()
{
    const DObjSkelMat bones[4] = {Bone(16, 32, -8), Bone(32, 64, -16), Bone(48, 96, -24), Bone(64, 128, -32)};
    // Blend records, in the order R_SkinXSurfaceWeight walks them: a bone
    // offset per vertex, then (offset, weight) pairs for each extra bone.
    std::uint16_t blend[] = {
        Offset(0),
        Offset(1), Offset(2), kHalf,
        Offset(2), Offset(1), kQuarter, Offset(3), kQuarter,
        Offset(3), Offset(0), kQuarter, Offset(1), kQuarter, Offset(2), kQuarter,
    };
    XSurfaceVertexInfo info{};
    info.vertCount[0] = info.vertCount[1] = info.vertCount[2] = info.vertCount[3] = 1;
    info.vertsBlend = blend;

    GfxPackedVertex in[kVerts]{};
    for (int i = 0; i < kVerts; ++i)
    {
        in[i].xyz[0] = 1.0f + i;
        in[i].xyz[1] = 2.0f;
        in[i].xyz[2] = 3.0f;
        in[i].binormalSign = i % 2 ? -1.0f : 1.0f;
        in[i].color.packed = 0x11223300u + i;
        in[i].texCoord.packed = 0x3C003C00u + i;
        in[i].normal.packed = 0x3F7F7FFEu;  // +x, exact through unpack and repack
        in[i].tangent.packed = 0x3F7FFE7Fu; // +y
    }

    g_pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    g_page = mmap(nullptr, g_pageSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(g_page != MAP_FAILED);
    if (g_page == MAP_FAILED)
        return 1;
    auto *out = static_cast<GfxPackedVertex *>(g_page);

    Install();
    R_SkinXSurfaceWeight(in, &info, bones, out);
    Uninstall();

    std::printf("output reads %d, writes %d\n", static_cast<int>(g_reads), static_cast<int>(g_writes));
    CHECK(g_reads == 0);
    CHECK(g_writes > 0);

    // Expected position: sum of weight * (xyz + bone origin), with the first
    // bone taking what the others leave.
    const struct { int bones[4]; float weights[4]; } mix[kVerts] = {
        {{0}, {1.0f}},
        {{1, 2}, {0.5f, 0.5f}},
        {{2, 1, 3}, {0.5f, 0.25f, 0.25f}},
        {{3, 0, 1, 2}, {0.25f, 0.25f, 0.25f, 0.25f}},
    };
    for (int i = 0; i < kVerts; ++i)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            float expected = 0.0f;
            for (int b = 0; b < i + 1; ++b)
                expected += mix[i].weights[b] * (in[i].xyz[axis] + bones[mix[i].bones[b]].origin[axis]);
            CHECK(out[i].xyz[axis] == expected);
        }
        CHECK(out[i].binormalSign == in[i].binormalSign);
        CHECK(out[i].color.packed == in[i].color.packed);
        CHECK(out[i].texCoord.packed == in[i].texCoord.packed);
        CHECK(out[i].normal.packed == in[i].normal.packed);
        CHECK(out[i].tangent.packed == in[i].tangent.packed);
    }

    munmap(g_page, g_pageSize);
    if (g_failures)
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
