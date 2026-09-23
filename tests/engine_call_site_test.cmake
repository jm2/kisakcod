# Fails unless SOURCE_ROOT/FILE contains LITERAL.
#
# Registered only for engine call sites that no test compiles, where the pin
# is the sole evidence that engine code calls a fork helper (AGENTS.md rule 2:
# a helper counts only once a real target calls it). Retire a pin as soon as a
# test compiles that engine file.
if (NOT DEFINED SOURCE_ROOT OR NOT DEFINED FILE OR NOT DEFINED LITERAL)
    message(FATAL_ERROR "usage: -DSOURCE_ROOT= -DFILE= -DLITERAL= -P engine_call_site_test.cmake")
endif()
file(READ "${SOURCE_ROOT}/${FILE}" _text)
string(FIND "${_text}" "${LITERAL}" _at)
if (_at EQUAL -1)
    message(FATAL_ERROR "${FILE} no longer calls `${LITERAL}`")
endif()
