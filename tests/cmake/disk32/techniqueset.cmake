# TechniqueSet's 64-bit load: the generated steps and its custom record body.
kisakcod_disk32_load_test(techniqueset db_disk32_techniqueset_tests.cpp db_disk32_techniqueset.cpp)
# The same load as a client: it builds declarations and creates shaders.
kisakcod_disk32_load_test(techniqueset-client db_disk32_techniqueset_tests.cpp db_disk32_techniqueset.cpp)
