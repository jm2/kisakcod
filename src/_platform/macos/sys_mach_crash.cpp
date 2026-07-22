// SPDX-License-Identifier: GPL-3.0-only
//
// macOS Mach crash-freezing. Suspends every Mach thread in the target task
// without sending an exception so the kernel preserves the crashed state for
// an out-of-band reporter to capture a stack. Uses task_for_pid() under the
// engine's debug port.
//
// This file is compiled only on __APPLE__ and is included by the POSIX
// backend implementation. It must not be linked into non-macOS builds.

#include <qcommon/sys_process.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <unistd.h>

namespace
{
constexpr mach_msg_type_number_t kThreadArrayCount = 64;

struct CapturedThreads
{
    std::vector<thread_t> threads;
    bool valid{false};
};

bool FreezeTask(mach_port_t task, CapturedThreads *outCaptured)
{
    outCaptured->threads.clear();
    outCaptured->valid = false;

    thread_array_t threadList{nullptr};
    mach_msg_type_number_t threadCount{0};
    kern_return_t result = task_threads(task, &threadList, &threadCount);
    if (result != KERN_SUCCESS)
        return false;

    outCaptured->threads.reserve(threadCount);
    bool suspendedAll = true;
    for (mach_msg_type_number_t i = 0; i < threadCount; ++i)
    {
        const thread_t thread = threadList[i];
        const kern_return_t suspendResult = thread_suspend(thread);
        if (suspendResult == KERN_SUCCESS)
            outCaptured->threads.push_back(thread);
        else
            suspendedAll = false;
    }

    if (threadList)
    {
        vm_deallocate(mach_task_self(),
            reinterpret_cast<vm_address_t>(threadList),
            threadCount * sizeof(*threadList));
    }
    outCaptured->valid = suspendedAll;
    return suspendedAll;
}

bool ResumeTask(const CapturedThreads &captured)
{
    bool resumedAll = true;
    for (thread_t thread : captured.threads)
    {
        const kern_return_t result = thread_resume(thread);
        if (result != KERN_SUCCESS)
            resumedAll = false;
    }
    return resumedAll;
}

pid_t PidFromHandle(SysProcessHandle handle)
{
    if (!handle)
        return -1;
    auto *const process = static_cast<SysProcess *>(handle);
    return process->pid;
}
}

SysMachCrashFreezeStatus Sys_MachCrashFreezeSuspend(SysProcessHandle handle)
{
    const pid_t pid = PidFromHandle(handle);
    if (pid <= 0)
        return SysMachCrashFreezeStatus::Unsupported;

    mach_port_t task{};
    const kern_return_t taskResult = task_for_pid(
        mach_task_self(), pid, &task);
    if (taskResult == KERN_NO_ACCESS)
        return SysMachCrashFreezeStatus::PermissionDenied;
    if (taskResult != KERN_SUCCESS)
        return SysMachCrashFreezeStatus::Failed;

    CapturedThreads captured;
    const bool frozen = FreezeTask(task, &captured);
    if (!captured.valid)
    {
        ResumeTask(captured);
        mach_port_deallocate(mach_task_self(), task);
        return SysMachCrashFreezeStatus::Failed;
    }

    // Stash the captured thread list on the SysProcess so Resume can find it.
    auto *const process = static_cast<SysProcess *>(handle);
    auto *machState = static_cast<CapturedThreads *>(
        std::malloc(sizeof(CapturedThreads)));
    if (!machState)
    {
        ResumeTask(captured);
        mach_port_deallocate(mach_task_self(), task);
        return SysMachCrashFreezeStatus::Failed;
    }
    new (machState) CapturedThreads{std::move(captured.threads), true};

    if (process->machState)
    {
        auto *previous = static_cast<CapturedThreads *>(process->machState);
        previous->~CapturedThreads();
        std::free(previous);
    }
    process->machState = machState;
    mach_port_deallocate(mach_task_self(), task);

    return frozen
        ? SysMachCrashFreezeStatus::Frozen
        : SysMachCrashFreezeStatus::AlreadyFrozen;
}

SysMachCrashFreezeStatus Sys_MachCrashFreezeResume(SysProcessHandle handle)
{
    if (!handle)
        return SysMachCrashFreezeStatus::Unsupported;
    auto *const process = static_cast<SysProcess *>(handle);
    auto *machState = static_cast<CapturedThreads *>(process->machState);
    if (!machState)
        return SysMachCrashFreezeStatus::Unsupported;

    const bool resumedAll = ResumeTask(*machState);
    machState->~CapturedThreads();
    std::free(machState);
    process->machState = nullptr;

    return resumedAll
        ? SysMachCrashFreezeStatus::Frozen
        : SysMachCrashFreezeStatus::Failed;
}
