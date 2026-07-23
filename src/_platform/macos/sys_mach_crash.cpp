// SPDX-License-Identifier: GPL-3.0-only
//
// macOS Mach crash-freeze backend. Implements the macOS-specific half
// of Sys_ProcessFreezeForCrash: install a Mach exception handler on
// the current task, park the current thread via thread_suspend, then
// block on sigsuspend until the process is terminated externally. This
// file is compiled only on __APPLE__ and is linked in via the macOS
// platform.cmake entry for sys_mach_crash.cpp.

#include <qcommon/sys_process.h>

#include <cerrno>
#include <cstdint>

#include <mach/exception.h>
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <signal.h>
#include <unistd.h>

SysProcessFreezeStatus Sys_MachProcessFreezeForCrash() noexcept
{
    mach_port_t task = mach_task_self();
    exception_mask_t mask = EXC_MASK_ALL;
    mach_port_t handler = MACH_PORT_NULL;
    kern_return_t kr = task_set_exception_ports(
        task,
        mask,
        handler,
        EXCEPTION_DEFAULT,
        THREAD_STATE_NONE);
    if (kr != KERN_SUCCESS)
        return SysProcessFreezeStatus::Failed;

    thread_act_t current = mach_thread_self();
    kr = thread_suspend(current);
    mach_port_deallocate(mach_task_self(), current);
    if (kr != KERN_SUCCESS)
        return SysProcessFreezeStatus::Failed;

    // thread_suspend returns to the caller; the contract is that we
    // never resume. Block on sigsuspend with an empty mask until
    // SIGKILL or process termination closes the process down.
    sigset_t empty{};
    sigemptyset(&empty);
    while (sigsuspend(&empty) != 0 && errno == EINTR)
    {
    }
    _exit(0);
    return SysProcessFreezeStatus::Frozen; // unreachable
}
