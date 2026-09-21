// SPDX-License-Identifier: GPL-3.0-only
//
// Shared internals of the HTTP download transport: the transport state,
// tuning constants, and the pump interface shared by dl_main.cpp (state
// machine, public DL_* entry points) and dl_main_pump.cpp (per-state
// pumps). Not part of any public contract; nothing outside those two
// units may include this header.

#pragma once

#include <cstdint>
#include <cstdio>

#include <qcommon/dl_http.h>
#include <qcommon/sys_socket.h>

// The client reports the failure to the server, which switches the
// transfer to the in-band download; keep the log lines at channel 14 to
// sit next to the client's own wwwdl diagnostics.
constexpr int DlPrintChannel = 14;

// Soft stall timeout: a download that makes no progress for this long
// fails to the in-band fallback instead of wedging the client. The retail
// library's host timeout behaved the same way (30 s default).
constexpr std::uint32_t DlStallTimeoutMs = UINT32_C(30000);

// Bounded redirect chain. The retail library followed redirects without a
// tight bound, which is a redirect-loop hazard, so the chain is capped and
// an exhausted chain fails to the in-band fallback.
constexpr int DlMaxRedirects = 8;

// Request staging buffer. The largest component is a 1 KiB path; the
// headers add a few hundred bytes, so this can never overflow for a URL
// Dl_ParseRedirectUrl accepted.
constexpr std::uint32_t DlRequestBufferSize = UINT32_C(4096);

// Response head accumulator. Dl_ParseResponseHead fails closed once the
// head exceeds DlResponseHeadMaxLength, so the accumulator only needs
// that much head room plus slack for one in-flight read.
constexpr std::uint32_t DlHeadBufferSize = DlResponseHeadMaxLength + 256;

// Body staging buffer between the socket and the download file.
constexpr std::uint32_t DlBodyBufferSize = UINT32_C(32768);

enum class DlTransportState : std::uint8_t
{
    Idle,
    Connecting,
    SendRequest,
    ReadHead,
    ReadBody,
};

struct DlTransport
{
    DlTransportState state{DlTransportState::Idle};
    SysSocketHandle socket{nullptr};
    FILE *file{nullptr};

    // Current request target (updated across redirects). The lengths
    // mirror the DlRedirectUrl fields the target was parsed from, so the
    // request formatter never rescans the buffers for terminators.
    char host[256]{};
    std::uint16_t hostLength{0};
    char path[1024]{};
    std::uint16_t pathLength{0};
    char user[64]{};
    std::uint16_t userLength{0};
    char password[64]{};
    std::uint16_t passwordLength{0};
    std::uint16_t port{80};
    bool hasBasicAuth{false};

    char request[DlRequestBufferSize]{};
    std::uint32_t requestLength{0};
    std::uint32_t requestSent{0};

    char head[DlHeadBufferSize]{};
    std::uint32_t headLength{0};

    std::uint64_t contentLength{0};
    bool hasContentLength{false};

    std::uint64_t bytesRead{0};
    std::uint32_t lastProgressMs{0};
    int redirectsFollowed{0};

    bool running{false};
};

extern DlTransport dl_transport;

// Fails the active download: releases the transport and leaves the state
// so the next DL_InProgress/DL_DownloadLoop observation reports the
// failure contract to the client (which sends "wwwdl fail" and the server
// falls back to the in-band download).
void DlAbort();

// Applies the parsed response head: 2xx arms the body phase, a redirect
// retargets the transport, everything else fails to the in-band fallback.
// Returns false when the download must fail (the caller aborts).
bool DlApplyHead(const DlResponseHead &head);

// Outcome of one pump invocation. Advance chains into the next state's
// pump within the same DL_DownloadLoop call, preserving the original
// fallthrough between connect, request send, head read, and body read.
enum class DlPumpResult : std::uint8_t
{
    Continue, // more progress next frame (WouldBlock / handshake pending)
    Advance,  // state advanced; run the next pump now
    Done,     // DL_DONE
    Failed,   // DL_FAILED (transport already aborted)
};

DlPumpResult DlPumpConnecting(std::uint32_t now);
DlPumpResult DlPumpSendRequest(std::uint32_t now);
DlPumpResult DlPumpResponseHead(std::uint32_t now);
DlPumpResult DlPumpBody(std::uint32_t now);
