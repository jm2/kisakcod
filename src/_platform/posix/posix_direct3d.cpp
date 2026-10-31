// posix_direct3d.cpp: Posix_Direct3DCreate9 (posix_direct3d.h).

#include "posix_direct3d.h"

#include <cstdio>
#include <exception>
#include <string>

#include <d3d9.h>

// dxvk's exception type (src/util/util_error.h in the pinned fork): one
// message string. dxvk-native's Direct3DCreate9 lets it escape, and its
// type_info is hidden in the library, so a matching declaration here catches
// it by name (libc++ and libstdc++ both compare hidden type_infos by name).
namespace dxvk
{
class DxvkError
{
public:
    const std::string &message() const
    {
        return m_message;
    }

private:
    std::string m_message;
};
} // namespace dxvk

namespace
{
// A failed attempt leaves dxvk's global state half set up, and a second
// Direct3DCreate9 then crashes inside it, so every later call returns the
// first failure.
bool s_failed = false;
char s_failure[256] = "unknown error";
} // namespace

IDirect3D9 *Posix_Direct3DCreate9(char *error, std::size_t errorSize)
{
    if (!s_failed)
    {
        try
        {
            return Direct3DCreate9(D3D_SDK_VERSION);
        }
        catch (const dxvk::DxvkError &e)
        {
            std::snprintf(s_failure, sizeof(s_failure), "%s", e.message().c_str());
        }
        catch (const std::exception &e)
        {
            std::snprintf(s_failure, sizeof(s_failure), "%s", e.what());
        }
        catch (...)
        {
        }
        s_failed = true;
    }
    std::snprintf(error, errorSize, "%s", s_failure);
    return nullptr;
}
