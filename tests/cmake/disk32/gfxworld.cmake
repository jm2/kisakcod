# GfxWorld's 64-bit load: the generated steps and its custom record body, with
# Image's, LightDef's and Material's (and TechniqueSet's) real steps for the
# images, light defs and materials it names.
kisakcod_disk32_load_test(gfxworld db_disk32_gfxworld_tests.cpp db_disk32_gfxworld.cpp db_disk32_image.cpp
    db_disk32_lightdef.cpp db_disk32_material.cpp db_disk32_techniqueset.cpp)
