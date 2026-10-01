# XAnimParts' 64-bit load (db_disk32_xanimparts.cpp: the generated steps and its
# custom record body), with the production Load_ScriptStringCustom. Section GC
# drops the rest of db_stringtable_load.cpp, so the test stubs no more seams.
kisakcod_disk32_load_test(xanimparts db_disk32_xanimparts_tests.cpp db_disk32_xanimparts.cpp db_stringtable_load.cpp)
target_compile_options(kisakcod-db-disk32-xanimparts-tests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(kisakcod-db-disk32-xanimparts-tests PRIVATE -Wl,--gc-sections)
