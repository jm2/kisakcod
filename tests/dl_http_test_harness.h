// SPDX-License-Identifier: GPL-3.0-only
//
// dl_http_test_harness.h -- shared check harness for the dl_http
// protocol test translation units (dl_http_tests.cpp,
// dl_http_head_parse_tests.cpp). Inline so one binary holds a single
// check state: the failing-check recorder is shared, so a broken
// contract anywhere fails the run, never just prints. The head-parse
// stage functions live in dl_http_head_parse_tests.cpp.

#ifndef DL_HTTP_TEST_HARNESS_H
#define DL_HTTP_TEST_HARNESS_H

#include <cstddef>
#include <cstring>

inline const char *checkStage = "startup";
// A failing check records its stage both for diagnostics and for the
// process exit code: a broken contract must fail the run, not just print.
inline bool checkFailed = false;

inline bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        checkFailed = true;
        return false;
    }
    return true;
}

inline bool CheckString(const char *const actual,
    const char *const expected,
    const char *const stage)
{
    return Check(actual != nullptr && std::strcmp(actual, expected) == 0,
        stage);
}

// Bounded test-fixture copy: asserts that the count fits the destination
// and then copies byte by byte, so a stage authoring error fails the run
// instead of overrunning the buffer.
inline void CopyBounded(char *const out, const std::size_t capacity,
    const char *const source, const std::size_t count)
{
    Check(count <= capacity, "test-fixture-copy");
    for (std::size_t index = 0; index < count; ++index)
        out[index] = source[index];
}

// Head-parse stages are defined in dl_http_head_parse_tests.cpp and run
// in the same binary after the URL and request stages.
void StageHeadParseComplete();
void StageHeadParseIncremental();
void StageHeadParseHeaders();
void StageHeadParseFailures();
void StageHeadParseContentLengthOverflowAtMax();
void StageHeadParseContentLengthOverflowBeyond();
void StageHeadParseContentLengthOverlongLenient();
void StageHeadParseContentLengthMax();
void StageHeadParseContentLengthFirstWins();
void StageResponseStatusAcceptance();

#endif // DL_HTTP_TEST_HARNESS_H
