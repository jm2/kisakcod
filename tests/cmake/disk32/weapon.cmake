# Weapon's 64-bit load: the generated steps and its custom record body, with
# the production Load_ScriptStringCustom for the tags and notetrack maps, and
# Material's, FX's and XModel's real steps (and their families') for the
# references.
kisakcod_disk32_load_test(weapon db_disk32_weapon_tests.cpp db_disk32_weapon.cpp db_stringtable_load.cpp
    db_disk32_material.cpp db_disk32_techniqueset.cpp db_disk32_image.cpp db_disk32_fx.cpp db_disk32_xmodel.cpp)
