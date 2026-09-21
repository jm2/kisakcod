// SPDX-License-Identifier: GPL-3.0-only
//
// platform_socket_stream_harness.cpp -- the run-wide failing-check state
// for the platform socket stream test binary. The scaffolding itself
// (loopback listener, bounded connect poll, bounded non-blocking
// send/receive pumps) lives inline in platform_socket_stream_harness.h;
// only the failure recorder needs exactly one definition.

#include "platform_socket_stream_harness.h"

namespace
{
const char *checkStage = "startup";
bool checkFailed = false;
} // namespace

bool Check(const bool condition, const char *const stage)
{
    if (!condition)
    {
        checkStage = stage;
        checkFailed = true;
        return false;
    }
    return true;
}

bool StreamHarnessFailed()
{
    return checkFailed;
}

const char *StreamHarnessFailedStage()
{
    return checkStage;
}
