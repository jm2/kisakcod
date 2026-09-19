# Reuse the existing fail-closed declaration/body extractor in the renderer gate.
kisakcod_renderer_enum_slice(bgame/bg_public.h "enum perksEnum" "\n};" gameplay_perk_enum)
kisakcod_renderer_enum_slice(game/game_public.h "enum DAMAGE_FLAGS" "\n};" gameplay_damage_enum)
kisakcod_renderer_enum_slice(game/g_local.h "enum meansOfDeath_t" "\n};" gameplay_mod_sp)
kisakcod_renderer_enum_slice(game_mp/g_public_mp.h "enum meansOfDeath_t" "\n};" gameplay_mod_mp)
kisakcod_renderer_enum_slice(bgame/bg_perks_mp.cpp "const char *bg_perkNames[" "\n};" gameplay_perk_names)
kisakcod_renderer_enum_slice(bgame/bg_perks_mp.cpp "uint32_t __cdecl BG_GetPerkIndexForName(" "\n}" gameplay_perk_lookup)
kisakcod_renderer_enum_slice(game_mp/g_client_script_cmd_mp.cpp "bool __cdecl IsBulletImpactMOD(" "\n}" gameplay_bullet_mod)
kisakcod_renderer_enum_slice(game_mp/g_vehicles_mp.cpp "bool __cdecl G_VehImmuneToDamage(" "\n}" gameplay_vehicle_immunity)
