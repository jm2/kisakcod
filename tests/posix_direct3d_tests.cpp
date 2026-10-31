// Posix_Direct3DCreate9 (_platform/posix/posix_direct3d.cpp): dxvk-native's
// Direct3DCreate9 throws dxvk::DxvkError when it can't start Vulkan or the
// display, and the client aborted on it (#941). Here no Vulkan driver exists
// (the test points the loader at a missing one), so the wrapper must return
// null with dxvk's reason instead of throwing, and a second call must return
// the same failure without going back into dxvk, whose half-initialized state
// crashes a retry.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <d3d9.h>

#include <_platform/posix/posix_direct3d.h>

namespace
{
int g_failures = 0;

void Expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main()
{
    char first[256];
    char second[256];
    IDirect3D9 *d3d9 = Posix_Direct3DCreate9(first, sizeof(first));
    if (d3d9)
    {
        // A usable driver after all: nothing to test.
        d3d9->Release();
        std::printf("SKIP: a Vulkan driver started\n");
        return 0;
    }
    Expect(first[0] != '\0' && std::strcmp(first, "unknown error") != 0, "the failure carries dxvk's reason");
    Expect(Posix_Direct3DCreate9(second, sizeof(second)) == nullptr, "a second call fails too");
    Expect(std::strcmp(first, second) == 0, "a second call reports the first failure");
    std::printf("reason: %s\n", first);
    if (g_failures)
        return 1;
    std::printf("posix-direct3d: ok\n");
    return 0;
}
