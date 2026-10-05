# FX's 64-bit load: the generated steps and its custom record body, with
# Material's, TechniqueSet's and XModel's real steps for the visuals, and the
# FX converter, its oracle, converting the same records.
kisakcod_disk32_load_test(fx db_disk32_fx_tests.cpp db_disk32_fx.cpp ../EffectsCore/fx_fastfile_native_disk32.cpp
    db_disk32_material.cpp db_disk32_techniqueset.cpp db_disk32_image.cpp db_disk32_xmodel.cpp db_stringtable_load.cpp)
