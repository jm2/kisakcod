// db_load_entry_checks: the shared check framework for the
// db_load_entry production enrollment (ki-458h / #125).
//
// db_load_entry_test.cpp (registry cases + service definitions) and
// db_load_entry_stream_test.cpp (the stream-relocation cases) both
// report through these counters and helpers, so one binary-wide
// check/failure tally reaches the single main(). Every helper is
// inline; the counters are C++17 inline variables owned by this
// header. Assertion text and failure-count semantics are identical
// in both TUs: a case prints FAIL lines per failed expectation and
// main() returns 1 when any expectation failed.

#ifndef DB_LOAD_ENTRY_CHECKS_HPP
#define DB_LOAD_ENTRY_CHECKS_HPP

#include "db_load_entry_harness.hpp"

namespace db_load_entry_checks
{
inline uint32_t g_checks = 0;
inline uint32_t g_failures = 0;

inline void ExpectTrue(bool condition, const char *what, int line)
{
    ++g_checks;
    if (!condition)
    {
        std::printf("FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

inline void ExpectU32(uint32_t actual, uint32_t expected, const char *what,
                      int line)
{
    ++g_checks;
    if (actual != expected)
    {
        std::printf("FAIL line %d: %s (actual 0x%08X expected 0x%08X)\n",
                    line, what, actual, expected);
        ++g_failures;
    }
}

inline void ClearErrors()
{
    db_load_entry_harness::State().errors.clear();
}
} // namespace db_load_entry_checks

#define EXPECT(cond) \
    db_load_entry_checks::ExpectTrue((cond), #cond, __LINE__)
#define EXPECT_U32(actual, expected)                          \
    db_load_entry_checks::ExpectU32((actual), (expected),     \
                                    #actual " == " #expected, \
                                    __LINE__)

#endif // DB_LOAD_ENTRY_CHECKS_HPP
