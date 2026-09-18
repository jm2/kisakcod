// SPDX-License-Identifier: GPL-3.0-only
//
// Per-state pumps of the HTTP download transport (CODEBASE_AUDIT.md H4).
// Each pump advances one DlTransportState and reports whether the
// DL_DownloadLoop caller should chain into the next pump now (Advance) or
// wait for the next frame (Continue). Behavior is identical to the
// previous single-function implementation; the split keeps every function
// small and reviewable.

#include <qcommon/dl_main_internal.h>

#include <qcommon/com_fileaccess.h>
#include <qcommon/dl_http.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_socket.h>

#include <cstdint>

namespace
{
// Writes one received span into the download file, clamping to a declared
// Content-Length (overshoot keeps the declared prefix only). Returns false
// when the caller must fail the download (write error; transport aborted).
bool DlWriteBodyChunk(const char *const data,
    std::uint32_t length,
    const std::uint32_t now)
{
    if (dl_transport.hasContentLength
        && dl_transport.bytesRead + length > dl_transport.contentLength)
    {
        length = static_cast<std::uint32_t>(
            dl_transport.contentLength - dl_transport.bytesRead);
    }
    if (length == 0)
        return true;
    const std::uint32_t written = FS_FileWrite(data, length, dl_transport.file);
    if (written != length)
    {
        Com_DPrintf(DlPrintChannel, "DL: write failed (disk full?)\n");
        DlAbort();
        return false;
    }
    dl_transport.bytesRead += length;
    dl_transport.lastProgressMs = now;
    return true;
}

// A small file routinely arrives in the same TCP segment as the response
// head, and Dl_ParseResponseHead leaves those bytes at the front of the
// head buffer when it reports HeadComplete. They are body bytes already
// received, so they must be written before (and instead of) another
// socket read -- skipping them corrupts the file. Returns false when the
// download failed (transport aborted).
bool DlDrainBufferedBody(const std::uint32_t now)
{
    if (dl_transport.headLength == 0)
        return true;
    const std::uint32_t buffered = dl_transport.headLength;
    dl_transport.headLength = 0;
    return DlWriteBodyChunk(dl_transport.head, buffered, now);
}

// A declared Content-Length already satisfied ends the body; with
// connection-close framing only the peer's EOF completes it.
bool DlBodySatisfied()
{
    return dl_transport.hasContentLength
        && dl_transport.bytesRead >= dl_transport.contentLength;
}

// Pulls more head bytes into the accumulator. Returns false with
// *outWouldBlock set when the socket would block (keep waiting); any
// other false is a failure whose reason is already logged and aborted.
bool DlRecvHeadBytes(bool *const outWouldBlock)
{
    *outWouldBlock = false;
    if (dl_transport.headLength >= sizeof(dl_transport.head))
    {
        Com_DPrintf(DlPrintChannel, "DL: response head overflow\n");
        DlAbort();
        return false;
    }
    std::uint32_t received = 0;
    const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(
        dl_transport.socket, dl_transport.head + dl_transport.headLength,
        static_cast<std::uint32_t>(sizeof(dl_transport.head)
            - dl_transport.headLength),
        &received);
    if (status == SysSocketStreamRecvStatus::WouldBlock)
    {
        *outWouldBlock = true;
        return false;
    }
    if (status == SysSocketStreamRecvStatus::Disconnected)
    {
        Com_DPrintf(DlPrintChannel,
            "DL: connection closed before response head\n");
        DlAbort();
        return false;
    }
    if (status != SysSocketStreamRecvStatus::Received)
    {
        Com_DPrintf(DlPrintChannel, "DL: head receive failed\n");
        DlAbort();
        return false;
    }
    dl_transport.headLength += received;
    return true;
}

// Completes the transfer: release the transport, keep the file.
void DlFinishBody()
{
    FS_FileClose(dl_transport.file);
    dl_transport.file = nullptr;
    Sys_SocketClose(&dl_transport.socket);
    dl_transport.state = DlTransportState::Idle;
    dl_transport.running = false;
}
} // namespace

DlPumpResult DlPumpConnecting(const std::uint32_t now)
{
    const SysSocketStreamPollStatus ready =
        Sys_SocketPollConnected(dl_transport.socket);
    if (ready == SysSocketStreamPollStatus::InProgress)
        return DlPumpResult::Continue;
    if (ready != SysSocketStreamPollStatus::Ready)
    {
        Com_DPrintf(DlPrintChannel, "DL: connection failed\n");
        DlAbort();
        return DlPumpResult::Failed;
    }
    dl_transport.lastProgressMs = now;
    dl_transport.state = DlTransportState::SendRequest;
    return DlPumpResult::Advance;
}

DlPumpResult DlPumpSendRequest(const std::uint32_t now)
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
            return DlPumpResult::Continue;
        if (status == SysSocketStreamSendStatus::Sent)
        {
            dl_transport.requestSent += sent;
            dl_transport.lastProgressMs = now;
            continue;
        }
        Com_DPrintf(DlPrintChannel, "DL: request send failed\n");
        DlAbort();
        return DlPumpResult::Failed;
    }
    dl_transport.state = DlTransportState::ReadHead;
    return DlPumpResult::Advance;
}

DlPumpResult DlPumpResponseHead(const std::uint32_t now)
{
    // Alternate parsing and pulling until the head completes.
    for (;;)
    {
        DlResponseHead head{};
        const DlResponseEvent event = Dl_ParseResponseHead(
            dl_transport.head, &dl_transport.headLength, &head);
        if (event == DlResponseEvent::NeedMoreData)
        {
            bool wouldBlock = false;
            if (!DlRecvHeadBytes(&wouldBlock))
            {
                if (wouldBlock)
                    return DlPumpResult::Continue;
                return DlPumpResult::Failed;
            }
            dl_transport.lastProgressMs = now;
            continue;
        }
        if (event == DlResponseEvent::StatusError)
        {
            Com_DPrintf(DlPrintChannel, "DL: malformed response head\n");
            DlAbort();
            return DlPumpResult::Failed;
        }

        dl_transport.lastProgressMs = now;
        if (!DlApplyHead(head))
        {
            DlAbort();
            return DlPumpResult::Failed;
        }
        if (dl_transport.state == DlTransportState::Connecting
            || dl_transport.state == DlTransportState::SendRequest)
        {
            // A redirect retargeted the transport; resume the pump next
            // frame from the new connection.
            return DlPumpResult::Continue;
        }
        return DlPumpResult::Advance; // 2xx: body phase follows in this pump
    }
}

DlPumpResult DlPumpBody(const std::uint32_t now)
{
    if (!DlDrainBufferedBody(now))
        return DlPumpResult::Failed;

    for (;;)
    {
        // The drain above may have satisfied a declared Content-Length
        // already; connection-close framing (no declared length) only
        // completes on the peer's EOF below.
        if (DlBodySatisfied())
            break;

        char body[DlBodyBufferSize];
        std::uint32_t received = 0;
        const SysSocketStreamRecvStatus status = Sys_SocketRecvStream(
            dl_transport.socket, body, sizeof(body), &received);
        if (status == SysSocketStreamRecvStatus::WouldBlock)
            return DlPumpResult::Continue;
        if (status == SysSocketStreamRecvStatus::Disconnected)
        {
            // Connection-close framing: EOF completes the body when no
            // Content-Length was declared; with a declared length a
            // short body is a truncation.
            if (!dl_transport.hasContentLength)
                break;
            Com_DPrintf(DlPrintChannel, "DL: truncated download\n");
            DlAbort();
            return DlPumpResult::Failed;
        }
        if (status != SysSocketStreamRecvStatus::Received)
        {
            Com_DPrintf(DlPrintChannel, "DL: body receive failed\n");
            DlAbort();
            return DlPumpResult::Failed;
        }
        if (!DlWriteBodyChunk(body, received, now))
            return DlPumpResult::Failed;
    }

    DlFinishBody();
    return DlPumpResult::Done;
}
