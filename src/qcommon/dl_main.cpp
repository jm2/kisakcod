// SPDX-License-Identifier: GPL-3.0-only
//
// HTTP/www download transport (CODEBASE_AUDIT.md H4).
//
// The retail client fetched server-redirected downloads with libwww over
// HTTP; this build implements the same client-side contract directly on
// the portable Sys_Socket* stream extension plus the pure protocol helpers
// in dl_http.cpp. There is no wire-protocol risk: the wwwdl handshake with
// the server is unchanged, and any transport failure is reported to the
// server as "wwwdl fail", which switches the transfer to the in-band UDP
// download exactly as it did with the retail client.
//
// Scope matches the retail observable behavior: absolute http:// redirect
// URLs (with optional Basic credentials taken from the URL userinfo),
// bounded redirects, Content-Length or connection-close body framing,
// progress fed to the legacy meter, and a soft stall timeout. https/ftp
// URLs and chunked bodies are unsupported and fail the download, which
// falls back in-band rather than breaking the transfer.

#include "dl_main.h"

#include <qcommon/com_fileaccess.h>
#include <qcommon/dl_http.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_socket.h>
#include <qcommon/sys_time.h>
#include <universal/com_files.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

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
// head exceeds DlResponseHeadMaxLength, so the accumulator only needs that
// much head room (plus slack for one in-flight read).
constexpr std::uint32_t DlHeadBufferSize = DlResponseHeadMaxLength + 256;

// Body staging buffer between the socket and the download file.
constexpr std::uint32_t DlBodyBufferSize = UINT32_C(32768);

namespace
{
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

    // Current request target (updated across redirects).
    char host[256]{};
    char path[1024]{};
    char user[64]{};
    char password[64]{};
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

DlTransport dl_transport;
bool dl_isMotd = false;
bool dl_initialized = false;

// Fails the active download: releases the transport and marks the state so
// the next DL_InProgress/DL_DownloadLoop observation reports the failure
// contract to the client (which sends "wwwdl fail" and the server falls
// back to the in-band download).
void DlAbort()
{
    Sys_SocketClose(&dl_transport.socket);
    if (dl_transport.file)
    {
        FS_FileClose(dl_transport.file);
        dl_transport.file = nullptr;
    }
    dl_transport.state = DlTransportState::Idle;
    dl_transport.running = false;
}

// Issues the connect for the current request target. Everything up to the
// TCP connect is synchronous (parse + resolve, as the retail library's
// setup was); the handshake itself is polled by DL_DownloadLoop.
bool DlStartConnect()
{
    Sys_SocketClose(&dl_transport.socket);
    SysSocketAddress endpoint{};
    const SysSocketResolveStatus resolved = Sys_SocketResolveHost(
        dl_transport.host, dl_transport.port, &endpoint);
    if (resolved != SysSocketResolveStatus::Resolved)
    {
        Com_DPrintf(DlPrintChannel, "DL: cannot resolve download host %s\n",
            dl_transport.host);
        return false;
    }

    if (Sys_SocketOpenStream(true, &dl_transport.socket)
        != SysSocketStreamOpenStatus::Opened)
    {
        Com_DPrintf(DlPrintChannel, "DL: cannot open stream socket\n");
        return false;
    }

    const SysSocketStreamConnectStatus connected = Sys_SocketConnectStream(
        dl_transport.socket, &endpoint);
    if (connected == SysSocketStreamConnectStatus::Connected)
    {
        dl_transport.state = DlTransportState::SendRequest;
        return true;
    }
    if (connected == SysSocketStreamConnectStatus::InProgress)
    {
        dl_transport.state = DlTransportState::Connecting;
        return true;
    }
    Com_DPrintf(DlPrintChannel, "DL: connect to %s failed\n",
        dl_transport.host);
    return false;
}

// Builds the GET request for the current target. Returns false on an
// internal formatting failure (a URL the parser accepted always fits).
bool DlBuildRequest()
{
    DlRedirectUrl target{};
    std::memcpy(target.host, dl_transport.host, sizeof(target.host));
    std::memcpy(target.path, dl_transport.path, sizeof(target.path));
    std::memcpy(target.user, dl_transport.user, sizeof(target.user));
    std::memcpy(target.password, dl_transport.password,
        sizeof(target.password));
    target.port = dl_transport.port;
    target.hasBasicAuth = dl_transport.hasBasicAuth;

    const DlRequestStatus formatted = Dl_FormatGetRequest(target,
        dl_transport.request, sizeof(dl_transport.request),
        &dl_transport.requestLength);
    if (formatted != DlRequestStatus::Ok)
    {
        Com_DPrintf(DlPrintChannel, "DL: cannot format request (%u)\n",
            static_cast<unsigned>(formatted));
        return false;
    }
    dl_transport.requestSent = 0;
    return true;
}

// Retargets the transport to `redirect` and restarts the connection for the
// next request attempt. Returns false when the redirect chain is exhausted
// or the new connection cannot be established.
bool DlFollowRedirect(const DlRedirectUrl &redirect)
{
    if (dl_transport.redirectsFollowed >= DlMaxRedirects)
    {
        Com_DPrintf(DlPrintChannel, "DL: too many redirects\n");
        return false;
    }
    ++dl_transport.redirectsFollowed;

    std::memcpy(dl_transport.host, redirect.host, sizeof(dl_transport.host));
    std::memcpy(dl_transport.path, redirect.path, sizeof(dl_transport.path));
    std::memcpy(dl_transport.user, redirect.user, sizeof(dl_transport.user));
    std::memcpy(dl_transport.password, redirect.password,
        sizeof(dl_transport.password));
    dl_transport.port = redirect.port;
    dl_transport.hasBasicAuth = redirect.hasBasicAuth;
    Com_DPrintf(DlPrintChannel, "DL: redirect to %s:%u%s\n",
        dl_transport.host, static_cast<unsigned>(dl_transport.port),
        dl_transport.path);

    // Body bytes buffered before the redirect belong to the old response;
    // discard them and start the next request with a clean accumulator.
    dl_transport.headLength = 0;
    dl_transport.hasContentLength = false;
    dl_transport.contentLength = 0;
    if (!DlBuildRequest())
        return false;
    return DlStartConnect();
}

// Applies the parsed head: 2xx arms the body phase, a redirect retargets
// the transport, everything else fails to the in-band fallback.
// Handles the 3xx branch of DlApplyHead: resolves the Location value
// against the current transport target and follows it. Returns false
// when the target is unsupported or the chain is exhausted.
bool Dl_ApplyRedirect(const char *const location)
{
    DlRedirectUrl redirect{};
    if (std::strstr(location, "://"))
    {
        // Absolute redirect: parse as a full URL (scheme must be http).
        if (Dl_ParseRedirectUrl(location, &redirect) != DlUrlStatus::Ok)
        {
            Com_DPrintf(DlPrintChannel,
                "DL: unsupported redirect target, failing to in-band\n");
            return false;
        }
    }
    else if (location[0] == '/')
    {
        // Path-absolute redirect: same origin and credentials, new
        // path. The fragment is not part of a request target.
        std::uint32_t pathLength = 0;
        while (location[pathLength] != '\0'
            && location[pathLength] != '#')
            ++pathLength;
        std::memcpy(redirect.host, dl_transport.host,
            sizeof(redirect.host));
        std::memcpy(redirect.user, dl_transport.user,
            sizeof(redirect.user));
        std::memcpy(redirect.password, dl_transport.password,
            sizeof(redirect.password));
        redirect.port = dl_transport.port;
        redirect.hasBasicAuth = dl_transport.hasBasicAuth;
        if (pathLength >= sizeof(redirect.path))
        {
            Com_DPrintf(DlPrintChannel,
                "DL: redirect target too long, failing to in-band\n");
            return false;
        }
        std::memcpy(redirect.path, location, pathLength);
        redirect.path[pathLength] = '\0';
    }
    else
    {
        // Relative references beyond path-absolute are unsupported;
        // the retail library resolved them, but real redirectors serve
        // absolute or path-absolute targets, and anything else fails
        // to the in-band fallback.
        Com_DPrintf(DlPrintChannel,
            "DL: unsupported redirect target, failing to in-band\n");
        return false;
    }

    return DlFollowRedirect(redirect);
}

bool DlApplyHead(const DlResponseHead &head)
{
    if (head.statusCode / 100 == 3)
    {
        if (!head.hasLocation)
        {
            Com_DPrintf(DlPrintChannel,
                "DL: redirect without Location, failing to in-band\n");
            return false;
        }
        return Dl_ApplyRedirect(head.location);
    }

    if (head.statusCode / 100 == 2)
    {
        if (head.chunked)
        {
            // Dechunking is not implemented; fail to the in-band fallback
            // rather than persisting a corrupt body.
            Com_DPrintf(DlPrintChannel,
                "DL: chunked body unsupported, failing to in-band\n");
            return false;
        }
        dl_transport.hasContentLength = head.hasContentLength;
        dl_transport.contentLength = head.contentLength;
        dl_transport.state = DlTransportState::ReadBody;
        return true;
    }

    Com_DPrintf(DlPrintChannel, "DL: HTTP status %d, failing to in-band\n",
        head.statusCode);
    return false;
}
} // namespace

bool __cdecl DL_InProgress()
{
    return dl_transport.running;
}

bool __cdecl DL_DLIsMotd()
{
    return dl_isMotd;
}

int __cdecl DL_BytesRead()
{
    // The progress meter is an int on the client side; a download beyond
    // 2 GiB keeps counting internally but the meter saturates.
    if (dl_transport.bytesRead > 2147483647ULL)
        return 2147483647;
    return static_cast<int>(dl_transport.bytesRead);
}

void __cdecl DL_InitDownload()
{
    if (!dl_initialized)
    {
        dl_initialized = true;
        Com_Printf(DlPrintChannel, "Client download subsystem initialized\n");
    }
}

void __cdecl DL_CancelDownload()
{
    // Unconditional teardown: the client abandons the transfer, the socket
    // and the temp file handle are released, and the temp file itself is
    // left on disk (the retail client did not delete it either; only a
    // completed download is renamed into place by the caller).
    Sys_SocketClose(&dl_transport.socket);
    if (dl_transport.file)
    {
        FS_FileClose(dl_transport.file);
        dl_transport.file = nullptr;
    }
    dl_transport = DlTransport{};
    dl_isMotd = false;
}

int __cdecl DL_BeginDownload(char *localName, char *remoteName)
{
    if (dl_transport.running)
        return 0;
    if (!localName || !remoteName || remoteName[0] == '\0')
        return 0;

    DL_InitDownload();

    DlRedirectUrl url{};
    if (Dl_ParseRedirectUrl(remoteName, &url) != DlUrlStatus::Ok)
    {
        Com_DPrintf(DlPrintChannel,
            "DL: unsupported download URL, failing to in-band\n");
        return 0;
    }

    // The client passed an OS path under fs_homepath for the temp file;
    // create its directories and open it for writing before any network
    // work, so an unwritable target fails the download up front.
    if (FS_CreatePath(localName) != 0)
    {
        Com_DPrintf(DlPrintChannel, "DL: cannot create path %s\n", localName);
        return 0;
    }
    FILE *file = FS_FileOpenWriteBinary(localName);
    if (!file)
    {
        Com_DPrintf(DlPrintChannel, "DL: cannot open %s for writing\n",
            localName);
        return 0;
    }

    dl_transport = DlTransport{};
    dl_transport.file = file;
    std::memcpy(dl_transport.host, url.host, sizeof(dl_transport.host));
    std::memcpy(dl_transport.path, url.path, sizeof(dl_transport.path));
    std::memcpy(dl_transport.user, url.user, sizeof(dl_transport.user));
    std::memcpy(dl_transport.password, url.password,
        sizeof(dl_transport.password));
    dl_transport.port = url.port;
    dl_transport.hasBasicAuth = url.hasBasicAuth;

    if (!DlBuildRequest() || !DlStartConnect())
    {
        DlAbort();
        return 0;
    }

    dl_transport.lastProgressMs = Sys_Milliseconds();
    dl_transport.running = true;
    return 1;
}

// Pumps the body phase: first writes any body bytes the head parser
// buffered alongside the head terminator, then streams socket data into
// the download file until a declared length is satisfied or the peer
// closes. Returns DL_CONTINUE, DL_FAILED, or DL_DONE.
int DlPumpBody(const std::uint32_t now)
{
    // A small file routinely arrives in the same TCP segment as the
    // response head, and Dl_ParseResponseHead leaves those bytes at the
    // front of the head buffer when it reports HeadComplete. They are
    // body bytes already received, so they must be written before (and
    // instead of) another socket read -- skipping them corrupts the file.
    if (dl_transport.headLength != 0)
    {
        std::uint32_t buffered = dl_transport.headLength;
        dl_transport.headLength = 0;
        if (dl_transport.hasContentLength
            && dl_transport.bytesRead + buffered
                > dl_transport.contentLength)
        {
            // Overshoot: keep the declared prefix only.
            buffered = static_cast<std::uint32_t>(
                dl_transport.contentLength - dl_transport.bytesRead);
        }
        if (buffered != 0)
        {
            const std::uint32_t written = FS_FileWrite(dl_transport.head,
                buffered, dl_transport.file);
            if (written != buffered)
            {
                Com_DPrintf(DlPrintChannel,
                    "DL: write failed (disk full?)\n");
                DlAbort();
                return DL_FAILED;
            }
            dl_transport.bytesRead += buffered;
            dl_transport.lastProgressMs = now;
        }
    }

    for (;;)
    {
        // The drain above may have satisfied a declared Content-Length
        // already; connection-close framing (no declared length) only
        // completes on the peer's EOF below.
        if (dl_transport.hasContentLength
            && dl_transport.bytesRead >= dl_transport.contentLength)
            break;

        char body[DlBodyBufferSize];
        std::uint32_t received = 0;
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(
            dl_transport.socket, body, sizeof(body), &received);
        if (status == SysSocketStreamRecvStatus::WouldBlock)
            return DL_CONTINUE;
        if (status == SysSocketStreamRecvStatus::Disconnected)
        {
            // Connection-close framing: EOF completes the body
            // when no Content-Length was declared; with a declared
            // length a short body is a truncation.
            if (!dl_transport.hasContentLength)
                break;
            Com_DPrintf(DlPrintChannel, "DL: truncated download\n");
            DlAbort();
            return DL_FAILED;
        }
        if (status != SysSocketStreamRecvStatus::Received)
        {
            Com_DPrintf(DlPrintChannel, "DL: body receive failed\n");
            DlAbort();
            return DL_FAILED;
        }

        if (dl_transport.hasContentLength
            && dl_transport.bytesRead + received
                > dl_transport.contentLength)
        {
            // Overshoot: keep the declared prefix only.
            received = static_cast<std::uint32_t>(
                dl_transport.contentLength - dl_transport.bytesRead);
        }
        if (received != 0)
        {
            const std::uint32_t written = FS_FileWrite(body,
                received, dl_transport.file);
            if (written != received)
            {
                Com_DPrintf(DlPrintChannel,
                    "DL: write failed (disk full?)\n");
                DlAbort();
                return DL_FAILED;
            }
            dl_transport.bytesRead += received;
            dl_transport.lastProgressMs = now;
        }
    }

    // Transfer complete: release the transport, keep the file.
    FS_FileClose(dl_transport.file);
    dl_transport.file = nullptr;
    Sys_SocketClose(&dl_transport.socket);
    dl_transport.state = DlTransportState::Idle;
    dl_transport.running = false;
    return DL_DONE;
}

int __cdecl DL_DownloadLoop()
{
    if (!dl_transport.running)
        return DL_FAILED;

    // Soft stall timeout: no bytes and no state progress for a full
    // timeout window fails the download to the in-band fallback.
    const std::uint32_t now = Sys_Milliseconds();
    if (now - dl_transport.lastProgressMs > DlStallTimeoutMs)
    {
        Com_DPrintf(DlPrintChannel,
            "DL: download stalled, failing to in-band\n");
        DlAbort();
        return DL_FAILED;
    }

    switch (dl_transport.state)
    {
        case DlTransportState::Connecting:
        {
            const SysSocketStreamPollStatus ready = Sys_SocketPollConnected(
                dl_transport.socket);
            if (ready == SysSocketStreamPollStatus::InProgress)
                return DL_CONTINUE;
            if (ready != SysSocketStreamPollStatus::Ready)
            {
                Com_DPrintf(DlPrintChannel, "DL: connection failed\n");
                DlAbort();
                return DL_FAILED;
            }
            dl_transport.lastProgressMs = now;
            dl_transport.state = DlTransportState::SendRequest;
            [[fallthrough]];
        }
        case DlTransportState::SendRequest:
        {
            while (dl_transport.requestSent < dl_transport.requestLength)
            {
                std::uint32_t sent = 0;
                const SysSocketStreamSendStatus status = Sys_SocketSendStream(
                    dl_transport.socket,
                    dl_transport.request + dl_transport.requestSent,
                    dl_transport.requestLength - dl_transport.requestSent,
                    &sent);
                if (status == SysSocketStreamSendStatus::WouldBlock)
                    return DL_CONTINUE;
                if (status == SysSocketStreamSendStatus::Sent)
                {
                    dl_transport.requestSent += sent;
                    dl_transport.lastProgressMs = now;
                    continue;
                }
                Com_DPrintf(DlPrintChannel, "DL: request send failed\n");
                DlAbort();
                return DL_FAILED;
            }
            dl_transport.state = DlTransportState::ReadHead;
            [[fallthrough]];
        }
        case DlTransportState::ReadHead:
        {
            // Alternate parsing and pulling until the head completes.
            for (;;)
            {
                DlResponseHead head{};
                const DlResponseEvent event = Dl_ParseResponseHead(
                    dl_transport.head, &dl_transport.headLength, &head);
                if (event == DlResponseEvent::NeedMoreData)
                {
                    if (dl_transport.headLength
                        >= sizeof(dl_transport.head))
                    {
                        Com_DPrintf(DlPrintChannel,
                            "DL: response head overflow\n");
                        DlAbort();
                        return DL_FAILED;
                    }
                    std::uint32_t received = 0;
                    const SysSocketStreamRecvStatus status =
                        Sys_SocketRecvStream(dl_transport.socket,
                            dl_transport.head + dl_transport.headLength,
                            static_cast<std::uint32_t>(
                                sizeof(dl_transport.head)
                                - dl_transport.headLength),
                            &received);
                    if (status == SysSocketStreamRecvStatus::WouldBlock)
                        return DL_CONTINUE;
                    if (status == SysSocketStreamRecvStatus::Disconnected)
                    {
                        Com_DPrintf(DlPrintChannel,
                            "DL: connection closed before response head\n");
                        DlAbort();
                        return DL_FAILED;
                    }
                    if (status != SysSocketStreamRecvStatus::Received)
                    {
                        Com_DPrintf(DlPrintChannel,
                            "DL: head receive failed\n");
                        DlAbort();
                        return DL_FAILED;
                    }
                    dl_transport.headLength += received;
                    dl_transport.lastProgressMs = now;
                    continue;
                }
                if (event == DlResponseEvent::StatusError)
                {
                    Com_DPrintf(DlPrintChannel,
                        "DL: malformed response head\n");
                    DlAbort();
                    return DL_FAILED;
                }

                dl_transport.lastProgressMs = now;
                if (!DlApplyHead(head))
                {
                    DlAbort();
                    return DL_FAILED;
                }
                if (dl_transport.state == DlTransportState::Connecting
                    || dl_transport.state == DlTransportState::SendRequest)
                {
                    // A redirect retargeted the transport; resume the pump
                    // next frame from the new connection.
                    return DL_CONTINUE;
                }
                break; // 2xx: body phase follows in this pump
            }
            [[fallthrough]];
        }
        case DlTransportState::ReadBody:
            return DlPumpBody(now);
        default:
            DlAbort();
            return DL_FAILED;
    }
}
