// buf_cursor_reset_tests.cpp: buf_cursor::ResetAll (production
// xanim/buf_cursor.cpp), which Com_ErrorCleanup calls. Com_Error longjmps out
// of a load between Activate/AnchorPos and Deactivate; the abandoned scope
// must not become the parent of the next load, whose Deactivate would write
// the restored position through the dead load's anchor.

#include <xanim/buf_cursor.hpp>

#include <cstdio>

namespace
{
unsigned char g_dead[8];
unsigned char g_next[8];
// The abandoned load's *pos lives here, where a stale Deactivate could reach it.
unsigned char *g_deadPos = nullptr;
}

int main()
{
    // A load that Com_Error abandons: activated and anchored, never deactivated.
    g_deadPos = g_dead;
    buf_cursor::Activate(g_dead, sizeof(g_dead));
    buf_cursor::AnchorPos(&g_deadPos);

    buf_cursor::ResetAll();
    // The dead load's stack slot is reused; the anchor must not touch it again.
    unsigned char *const sentinel = g_dead + 3;
    g_deadPos = sentinel;

    unsigned char *pos = g_next;
    buf_cursor::Activate(g_next, sizeof(g_next));
    buf_cursor::AnchorPos(&pos);
    buf_cursor::Advance(4);
    buf_cursor::Deactivate();

    const bool ok = g_deadPos == sentinel && buf_cursor::Current() == nullptr && pos == g_next + 4;
    if (!ok)
    {
        std::fputs("FAIL an abandoned load's cursor survives into the next load\n", stderr);
        return 1;
    }
    std::puts("buf_cursor reset: ok");
    return 0;
}
