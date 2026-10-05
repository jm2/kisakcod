# ClipMap's 64-bit load: the generated steps and its custom record body, with
# MapEnts' real step for the map entities, and XModel's, FX's and
# PhysPreset's (and their families') for static models and dynamic entities.
kisakcod_disk32_load_test(clipmap db_disk32_clipmap_tests.cpp db_disk32_clipmap.cpp db_disk32_mapents.cpp
    db_disk32_xmodel.cpp db_disk32_material.cpp db_disk32_techniqueset.cpp db_disk32_image.cpp db_stringtable_load.cpp
    db_disk32_fx.cpp db_disk32_physpreset.cpp)
