#include <database/db_disk32_load.h>

#if KISAK_ARCH_64BIT

#include <database/db_disk32_renderer_hooks.h>

// The hooks that need complete D3D9 interfaces (db_disk32_renderer_hooks.h).

void db::disk32_load::ShareTexture(GfxImage *image, std::uintptr_t texture)
{
#ifndef KISAK_DEDI_HEADLESS
    image->texture.basemap = reinterpret_cast<IDirect3DBaseTexture9 *>(texture);
    if (image->texture.basemap)
        image->texture.basemap->AddRef();
#else
    // LoadTexture already cleared the texture; a server's aliases are null.
    (void)image;
    (void)texture;
#endif
}

#endif // KISAK_ARCH_64BIT
