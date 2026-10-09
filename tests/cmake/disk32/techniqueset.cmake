# TechniqueSet's 64-bit load: the generated steps and its custom record body.
kisakcod_disk32_load_test(techniqueset db_disk32_techniqueset_tests.cpp db_disk32_techniqueset.cpp)
# The same load headless: the renderer's creation hooks never run.
kisakcod_disk32_load_test(techniqueset-headless db_disk32_techniqueset_tests.cpp db_disk32_techniqueset.cpp)
target_compile_definitions(kisakcod-db-disk32-techniqueset-headless-tests PRIVATE KISAK_DEDI_HEADLESS)
