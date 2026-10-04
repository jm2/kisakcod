# XAnimParts' 64-bit load (db_disk32_xanimparts.cpp: the generated steps and its
# custom record body), with the production Load_ScriptStringCustom, then the
# notetrack and delta-part readers on what it loaded. The readers compile as
# the Linux headless server compiles them. Section GC keeps only what the
# checks reach, so the test stubs just their boundary.
kisakcod_disk32_load_test(xanimparts db_disk32_xanimparts_tests.cpp db_disk32_xanimparts.cpp db_stringtable_load.cpp)
add_library(kisakcod-db-disk32-xanimparts-readers OBJECT
    ${SRC_DIR}/xanim/xanim.cpp ${SRC_DIR}/xanim/xanim_calc.cpp)
target_include_directories(kisakcod-db-disk32-xanimparts-readers SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-db-disk32-xanimparts-readers PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-db-disk32-xanimparts-readers PRIVATE
    KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS UNIX)
target_compile_options(kisakcod-db-disk32-xanimparts-readers PRIVATE
    -fms-extensions -ffunction-sections -fdata-sections)
target_sources(kisakcod-db-disk32-xanimparts-tests PRIVATE $<TARGET_OBJECTS:kisakcod-db-disk32-xanimparts-readers>)
target_compile_options(kisakcod-db-disk32-xanimparts-tests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(kisakcod-db-disk32-xanimparts-tests PRIVATE ${KISAK_TEST_GC_SECTIONS})
