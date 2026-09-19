# Production contents/masks, material table and movement trace consumers.
kisakcod_renderer_enum_slice(universal/q_shared.h "#define CONTENTS_SOLID" "include every IW3 trigger type\");" collision_contents)
kisakcod_renderer_enum_slice(universal/surfaceflags.cpp "const infoParm_t infoParms[60]" "\n};" collision_table)
kisakcod_renderer_enum_slice(universal/surfaceflags.cpp "int __cdecl Com_SurfaceTypeFromName(" "\n}" collision_from_name)
kisakcod_renderer_enum_slice(universal/surfaceflags.cpp "const char *__cdecl Com_SurfaceTypeToName(" "\n}" collision_to_name)
kisakcod_renderer_enum_slice(bgame/bg_pmove.cpp "void __cdecl PM_playerTrace(" "\n}" collision_player_trace)
kisakcod_renderer_enum_slice(bgame/bg_pmove.cpp "void __cdecl PM_AddTouchEnt(" "\n}" collision_add_touch)
kisakcod_renderer_enum_slice(physics/phys_local.h "#if defined(KISAK_SP)\ninline constexpr int PHYS_WORLD_CLIPMASK" "\n#endif" collision_physics_masks)
