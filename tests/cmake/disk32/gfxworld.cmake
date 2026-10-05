# GfxWorld's 64-bit load: the generated steps and its custom record body, with
# Image's, LightDef's, Material's (and TechniqueSet's) and XModel's real steps
# for the images, light defs, materials and models it names, and the
# production Load_ScriptStringCustom for XModel's bone names.
kisakcod_disk32_load_test(gfxworld db_disk32_gfxworld_tests.cpp db_disk32_gfxworld.cpp db_disk32_image.cpp
    db_disk32_lightdef.cpp db_disk32_material.cpp db_disk32_techniqueset.cpp db_disk32_xmodel.cpp
    db_stringtable_load.cpp)
