// SPDX-License-Identifier: GPL-3.0-only
//
// HTTP/www download transport (CODEBASE_AUDIT.md H4): state machine and
// public DL_* entry points. The per-state pumps live in dl_main_pump.cpp;
// transport state and tuning constants in dl_main_internal.h.
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
#include <qcommon/dl_main_internal.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_socket.h>
#include <qcommon/sys_time.h>
#include <universal/com_files.h>

#include <cstdint>
#include <cstring>

DlTransport dl_transport;
bool dl_isMotd = false;
bool dl_initialized = false;

// Fails the active download: releases the transport and leaves the state
// so the next DL_InProgress/DL_DownloadLoop observation reports the
// failure contract to the client (which sends "wwwdl fail" and the server
// falls back to the in-band download).
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

namespace
{
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

// Mirrors the transport target into the decomposed URL the formatter
// consumes, lengths included, so formatting never rescans the buffers.
void DlLoadTarget(DlRedirectUrl *const target)
{
    *target = DlRedirectUrl{};
    std::memcpy(target->host, dl_transport.host, sizeof(target->host));
    std::memcpy(target->path, dl_transport.path, sizeof(target->path));
    std::memcpy(target->user, dl_transport.user, sizeof(target->user));
    std::memcpy(target->password, dl_transport.password,
        sizeof(target->password));
    target->hostLength = dl_transport.hostLength;
    target->pathLength = dl_transport.pathLength;
    target->userLength = dl_transport.userLength;
    target->passwordLength = dl_transport.passwordLength;
    target->port = dl_transport.port;
    target->hasBasicAuth = dl_transport.hasBasicAuth;
}

// Stores one decomposed target in the transport, lengths included, so the
// request formatter and the connect both consume the same components
// without rescanning.
void DlCopyTarget(const DlRedirectUrl &url)
{
    std::memcpy(dl_transport.host, Dl_UrlHost(url), sizeof(dl_transport.host));
    std::memcpy(dl_transport.path, Dl_UrlPath(url), sizeof(dl_transport.path));
    std::memcpy(dl_transport.user, Dl_UrlUser(url), sizeof(dl_transport.user));
    std::memcpy(dl_transport.password, Dl_UrlPassword(url),
        sizeof(dl_transport.password));
    dl_transport.hostLength = Dl_UrlHostLength(url);
    dl_transport.pathLength = Dl_UrlPathLength(url);
    dl_transport.userLength = Dl_UrlUserLength(url);
    dl_transport.passwordLength = Dl_UrlPasswordLength(url);
    dl_transport.port = Dl_UrlPort(url);
    dl_transport.hasBasicAuth = Dl_UrlHasCredentials(url);
}

// Builds the GET request for the current target. Returns false on an
// internal formatting failure (a URL the parser accepted always fits).
bool DlBuildRequest()
{
    DlRedirectUrl target;
    DlLoadTarget(&target);
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

    DlCopyTarget(redirect);
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

// Copies the path-absolute redirect target (bounded scan; the fragment is
// not part of a request target) into `redirect`. False on overflow.
bool DlCopyRedirectPath(const char *const location,
    DlRedirectUrl *const redirect)
{
    std::uint32_t pathLength = 0;
    while (location[pathLength] != '\0' && location[pathLength] != '#')
        ++pathLength;
    if (pathLength >= sizeof(redirect->path))
    {
        Com_DPrintf(DlPrintChannel,
            "DL: redirect target too long, failing to in-band\n");
        return false;
    }
    for (std::uint32_t index = 0; index < pathLength; ++index)
        redirect->path[index] = location[index];
    redirect->path[pathLength] = '\0';
    redirect->pathLength = static_cast<std::uint16_t>(pathLength);
    return true;
}

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
        std::memcpy(redirect.host, dl_transport.host,
            sizeof(redirect.host));
        std::memcpy(redirect.user, dl_transport.user,
            sizeof(redirect.user));
        std::memcpy(redirect.password, dl_transport.password,
            sizeof(redirect.password));
        redirect.hostLength = dl_transport.hostLength;
        redirect.userLength = dl_transport.userLength;
        redirect.passwordLength = dl_transport.passwordLength;
        redirect.port = dl_transport.port;
        redirect.hasBasicAuth = dl_transport.hasBasicAuth;
        if (!DlCopyRedirectPath(location, &redirect))
            return false;
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
} // namespace

// Applies the parsed head: 2xx arms the body phase, a redirect retargets
// the transport, everything else fails to the in-band fallback.
bool DlApplyHead(const DlResponseHead &head)
{
    if (Dl_ResponseIsRedirect(head))
    {
        if (!Dl_ResponseHasLocation(head))
        {
            Com_DPrintf(DlPrintChannel,
                "DL: redirect without Location, failing to in-band\n");
            return false;
        }
        return Dl_ApplyRedirect(Dl_ResponseLocation(head));
    }

    if (Dl_ResponseIsSuccess(head))
    {
        if (Dl_ResponseIsChunked(head))
        {
            // Dechunking is not implemented; fail to the in-band fallback
            // rather than persisting a corrupt body.
            Com_DPrintf(DlPrintChannel,
                "DL: chunked body unsupported, failing to in-band\n");
            return false;
        }
        dl_transport.hasContentLength = Dl_ResponseHasContentLength(head);
        dl_transport.contentLength = Dl_ResponseContentLength(head);
        dl_transport.state = DlTransportState::ReadBody;
        return true;
    }

    Com_DPrintf(DlPrintChannel, "DL: HTTP status %d, failing to in-band\n",
        Dl_ResponseStatusCode(head));
    return false;
}

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
    DlCopyTarget(url);

    if (!DlBuildRequest() || !DlStartConnect())
    {
        DlAbort();
        return 0;
    }

    dl_transport.lastProgressMs = Sys_Milliseconds();
    dl_transport.running = true;
    return 1;
}

namespace
{
// Runs pumps until one waits for the next frame or the transfer ends,
// preserving the original fallthrough between states within one call.
int DlRunPumps(const std::uint32_t now)
{
    DlPumpResult result = DlPumpResult::Advance;
    while (result == DlPumpResult::Advance)
    {
        switch (dl_transport.state)
        {
            case DlTransportState::Connecting:
                result = DlPumpConnecting(now);
                break;
            case DlTransportState::SendRequest:
                result = DlPumpSendRequest(now);
                break;
            case DlTransportState::ReadHead:
                result = DlPumpResponseHead(now);
                break;
            case DlTransportState::ReadBody:
                result = DlPumpBody(now);
                break;
            default:
                DlAbort();
                return DL_FAILED;
        }
    }
    switch (result)
    {
        case DlPumpResult::Done:
            return DL_DONE;
        case DlPumpResult::Failed:
            return DL_FAILED;
        default:
            return DL_CONTINUE;
    }
}
} // namespace

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
    return DlRunPumps(now);
}
