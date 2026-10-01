// sys_quit.h: the host's "quit requested" hook for the headless dedicated
// server. The host asks for an orderly shutdown from contexts where engine
// code cannot run: a POSIX signal handler (SIGINT, SIGTERM) or the Win32
// console control thread (Ctrl+C, Ctrl+Break, console close). The request is
// only recorded here; the main thread's frame loop turns it into the console
// command "quit" with Cbuf_AddRequestedQuit (qcommon/cmd.cpp), so the shutdown
// is exactly the one typing quit runs: clients are dropped with a message, the
// config and the logs are closed out, and the process exits normally.
//
// Header-only and free of engine includes, so a signal-handler TU can use it
// without linking anything. The state is lock-free atomics only, which makes
// both calls async-signal-safe and safe from any thread.
#pragma once

#include <atomic>

namespace sys_quit_detail
{
static_assert(std::atomic<int>::is_always_lock_free, "a signal handler may only touch lock-free atomics");
static_assert(std::atomic<const char *>::is_always_lock_free, "a signal handler may only touch lock-free atomics");

inline std::atomic<int> requests{0};
inline std::atomic<const char *> source{nullptr};
} // namespace sys_quit_detail

// Records a quit request from the host. `source` names it for the log (a
// string literal such as "SIGTERM" or "CTRL_CLOSE_EVENT"); the first request's
// name is kept. Returns how many requests have arrived, this one included:
// 1 asks for an orderly quit, more means the host insists, and the caller
// should let the process die (restore the default action and re-raise).
inline int Sys_RequestQuit(const char *source) noexcept
{
    const char *none = nullptr;
    sys_quit_detail::source.compare_exchange_strong(none, source);
    return sys_quit_detail::requests.fetch_add(1) + 1;
}

// True once any request has arrived. It stays true: a request is never lost,
// even one that arrived before the frame loop started.
inline bool Sys_QuitRequested() noexcept
{
    return sys_quit_detail::requests.load() > 0;
}

// The first request's name, or null when none has arrived.
inline const char *Sys_QuitRequestSource() noexcept
{
    return sys_quit_detail::source.load();
}
