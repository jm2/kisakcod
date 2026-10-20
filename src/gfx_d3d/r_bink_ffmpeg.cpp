// Bink playback through FFmpeg for clients without binkw32 (KISAK_FFMPEG_CINEMATICS).
//
// binkw32 is 32-bit Windows only, so 64-bit and POSIX clients define the Bink
// calls r_cinematic.cpp makes here and decode with libavcodec's binkvideo:
// - The header (frame count, size, rate, alpha) is read from the file itself,
//   so BINK and BINKREALTIME report what binkw32 would.
// - BinkDoFrame decodes the next frame into the planes the engine locked
//   (bink_buffers.Frames[FrameNum], L8 textures); BinkNextFrame advances and
//   flips the buffer, as binkw32 does with BINKNOFRAMEBUFFERS.
// - BinkWait paces frames on the wall clock. Nothing plays the audio track:
//   Miles is stubbed on these clients, so cinematics are silent until the
//   OpenAL backend lands.
// - Reads are synchronous, so the read buffer always reports full and the
//   engine never seizes file IO for the movie.

#define __RADINEXE__
#include <binklib/bink.h>
#include <qcommon/qcommon.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace
{
constexpr U32 kHeaderSize = 44;
constexpr U32 kAlphaFlag = 0x00100000; // video flags: the file carries an alpha plane

thread_local char s_error[256];

void SetError(const char *text)
{
    snprintf(s_error, sizeof(s_error), "%s", text);
}

U32 ReadU32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<U32>(p[3]) << 24);
}

using Clock = std::chrono::steady_clock;

// BINK comes first, so an HBINK is also a Movie.
struct Movie
{
    BINK bink;
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;
    U32 decoded = 0; // frames decoded since the start or the last rewind
    U32 totalDecoded = 0;
    bool hasRects = false;
    bool paused = false;
    Clock::time_point start;      // when frame 1 was due, shifted by pauses
    Clock::time_point pausedAt;
    Clock::time_point opened;
};

void CloseMovie(Movie *movie)
{
    av_frame_free(&movie->frame);
    av_packet_free(&movie->packet);
    avcodec_free_context(&movie->codec);
    avformat_close_input(&movie->format);
    delete movie;
}

// Decodes the next video frame into movie->frame; false at the end or on error.
bool DecodeNext(Movie *movie)
{
    for (;;)
    {
        const int got = avcodec_receive_frame(movie->codec, movie->frame);
        if (got == 0)
            return true;
        if (got != AVERROR(EAGAIN))
            return false;
        const int read = av_read_frame(movie->format, movie->packet);
        if (read < 0)
        {
            avcodec_send_packet(movie->codec, nullptr); // drain
            continue;
        }
        if (movie->packet->stream_index == movie->stream)
            avcodec_send_packet(movie->codec, movie->packet);
        av_packet_unref(movie->packet);
    }
}

bool Rewind(Movie *movie)
{
    if (av_seek_frame(movie->format, movie->stream, 0, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(movie->codec);
    movie->decoded = 0;
    return true;
}

void CopyPlane(const BINKPLANE &plane, const uint8_t *src, int srcPitch, U32 width, U32 height, uint8_t fill)
{
    if (!plane.Buffer)
        return;
    uint8_t *dst = static_cast<uint8_t *>(plane.Buffer);
    for (U32 row = 0; row < height; ++row, dst += plane.BufferPitch)
    {
        if (src)
            memcpy(dst, src + static_cast<ptrdiff_t>(row) * srcPitch, width);
        else
            memset(dst, fill, width);
    }
}

Clock::time_point FrameDue(const Movie *movie, U32 frameNum)
{
    const int64_t usec = static_cast<int64_t>(frameNum - 1) * movie->bink.FrameRateDiv * 1000000 / movie->bink.FrameRate;
    return movie->start + std::chrono::microseconds(usec);
}

Movie *OpenMovie(const char *name, U32 flags)
{
    // Only file opens are supported; every engine caller opens a file.
    if (flags & BINKFROMMEMORY)
    {
        SetError("only file opens are supported");
        return nullptr;
    }
    uint8_t header[kHeaderSize];
    FILE *file = fopen(name, "rb");
    if (!file)
    {
        SetError("can't open the file");
        return nullptr;
    }
    const size_t got = fread(header, 1, kHeaderSize, file);
    fclose(file);
    if (got != kHeaderSize)
    {
        SetError("file is shorter than a Bink header");
        return nullptr;
    }
    if (memcmp(header, "BIK", 3) != 0)
    {
        SetError("not a Bink 1 file");
        return nullptr;
    }

    Movie *movie = new (std::nothrow) Movie();
    if (!movie)
    {
        SetError("out of memory");
        return nullptr;
    }
    movie->bink.Size = ReadU32(header + 4) + 8;
    movie->bink.Frames = ReadU32(header + 8);
    movie->bink.Width = ReadU32(header + 20);
    movie->bink.Height = ReadU32(header + 24);
    movie->bink.FrameRate = ReadU32(header + 28);
    movie->bink.FrameRateDiv = ReadU32(header + 32);
    movie->bink.BinkType = ReadU32(header + 36);
    movie->bink.OpenFlags = flags;
    movie->bink.FrameNum = 1;
    if (!movie->bink.Frames || !movie->bink.Width || !movie->bink.Height || !movie->bink.FrameRate
        || !movie->bink.FrameRateDiv)
    {
        SetError("bad Bink header");
        CloseMovie(movie);
        return nullptr;
    }

    if (avformat_open_input(&movie->format, name, av_find_input_format("bink"), nullptr) < 0)
    {
        SetError("FFmpeg can't open the movie");
        CloseMovie(movie);
        return nullptr;
    }

    const AVCodec *decoder = nullptr;
    movie->stream = av_find_best_stream(movie->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (movie->stream < 0 || !decoder)
    {
        SetError("no decodable video stream");
        CloseMovie(movie);
        return nullptr;
    }
    for (unsigned i = 0; i < movie->format->nb_streams; ++i)
        movie->format->streams[i]->discard = static_cast<int>(i) == movie->stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    movie->codec = avcodec_alloc_context3(decoder);
    movie->packet = av_packet_alloc();
    movie->frame = av_frame_alloc();
    if (!movie->codec || !movie->packet || !movie->frame
        || avcodec_parameters_to_context(movie->codec, movie->format->streams[movie->stream]->codecpar) < 0
        || avcodec_open2(movie->codec, decoder, nullptr) < 0)
    {
        SetError("FFmpeg can't open the Bink video decoder");
        CloseMovie(movie);
        return nullptr;
    }
    movie->start = movie->opened = Clock::now();
    Com_DPrintf(CON_CHANNEL_GFX, "Bink via FFmpeg: opened %s, %ux%u, %u frames at %u/%u fps%s\n",
                name, movie->bink.Width, movie->bink.Height,
                movie->bink.Frames, movie->bink.FrameRate, movie->bink.FrameRateDiv,
                (movie->bink.BinkType & kAlphaFlag) ? ", with alpha" : "");
    return movie;
}
} // namespace

char *RADEXPLINK BinkGetError(void)
{
    return s_error;
}

HBINK RADEXPLINK BinkOpen(const char *name, U32 flags)
{
    s_error[0] = 0;
    Movie *movie = OpenMovie(name, flags);
    return movie ? &movie->bink : nullptr;
}

void RADEXPLINK BinkClose(HBINK bink)
{
    if (!bink)
        return;
    Movie *movie = reinterpret_cast<Movie *>(bink);
    const double seconds = std::chrono::duration<double>(Clock::now() - movie->opened).count();
    Com_DPrintf(CON_CHANNEL_GFX, "Bink via FFmpeg: closed at frame %u of %u after decoding %u frames in %.1f s\n",
                bink->FrameNum, bink->Frames, movie->totalDecoded, seconds);
    CloseMovie(movie);
}

void RADEXPLINK BinkGetFrameBuffersInfo(HBINK bink, BINKFRAMEBUFFERS *buffers)
{
    const Movie *movie = reinterpret_cast<const Movie *>(bink);
    const bool alpha = (movie->bink.OpenFlags & BINKALPHA) && (movie->bink.BinkType & kAlphaFlag);
    memset(buffers, 0, sizeof(*buffers));
    buffers->TotalFrames = BINKMAXFRAMEBUFFERS;
    buffers->YABufferWidth = movie->bink.Width;
    buffers->YABufferHeight = movie->bink.Height;
    buffers->cRcBBufferWidth = (movie->bink.Width + 1) / 2;
    buffers->cRcBBufferHeight = (movie->bink.Height + 1) / 2;
    for (BINKFRAMEPLANESET &planes : buffers->Frames)
    {
        planes.YPlane.Allocate = planes.cRPlane.Allocate = planes.cBPlane.Allocate = 1;
        planes.APlane.Allocate = alpha;
    }
}

void RADEXPLINK BinkRegisterFrameBuffers(HBINK bink, BINKFRAMEBUFFERS *buffers)
{
    bink->FrameBuffers = buffers;
}

S32 RADEXPLINK BinkDoFrame(HBINK bink)
{
    Movie *movie = reinterpret_cast<Movie *>(bink);
    movie->hasRects = false;
    if (bink->FrameNum <= movie->decoded && !Rewind(movie))
        return 1;
    while (movie->decoded < bink->FrameNum)
    {
        if (!DecodeNext(movie))
        {
            bink->ReadError = 1;
            return 1;
        }
        ++movie->decoded;
        ++movie->totalDecoded;
    }
    bink->LastFrameNum = bink->FrameNum;

    BINKFRAMEBUFFERS *buffers = bink->FrameBuffers;
    if (!buffers)
        return 0;
    const AVFrame *frame = movie->frame;
    const U32 width = bink->Width < static_cast<U32>(frame->width) ? bink->Width : frame->width;
    const U32 height = bink->Height < static_cast<U32>(frame->height) ? bink->Height : frame->height;
    const BINKFRAMEPLANESET &planes = buffers->Frames[buffers->FrameNum];
    CopyPlane(planes.YPlane, frame->data[0], frame->linesize[0], width, height, 0);
    CopyPlane(planes.cRPlane, frame->data[2], frame->linesize[2], (width + 1) / 2, (height + 1) / 2, 0);
    CopyPlane(planes.cBPlane, frame->data[1], frame->linesize[1], (width + 1) / 2, (height + 1) / 2, 0);
    const bool frameAlpha = frame->format == AV_PIX_FMT_YUVA420P;
    CopyPlane(planes.APlane, frameAlpha ? frame->data[3] : nullptr, frameAlpha ? frame->linesize[3] : 0, width, height, 0xFF);
    bink->FrameRects[0] = {0, 0, static_cast<S32>(width), static_cast<S32>(height)};
    movie->hasRects = true;
    return 0;
}

void RADEXPLINK BinkNextFrame(HBINK bink)
{
    bink->FrameNum = bink->FrameNum < bink->Frames ? bink->FrameNum + 1 : 1;
    if (bink->FrameNum == 1)
        reinterpret_cast<Movie *>(bink)->start = Clock::now();
    if (bink->FrameBuffers)
        bink->FrameBuffers->FrameNum = (bink->FrameBuffers->FrameNum + 1) % bink->FrameBuffers->TotalFrames;
}

S32 RADEXPLINK BinkWait(HBINK bink)
{
    const Movie *movie = reinterpret_cast<const Movie *>(bink);
    return movie->paused || Clock::now() < FrameDue(movie, bink->FrameNum);
}

S32 RADEXPLINK BinkPause(HBINK bink, S32 pause)
{
    Movie *movie = reinterpret_cast<Movie *>(bink);
    if (pause && !movie->paused)
        movie->pausedAt = Clock::now();
    else if (!pause && movie->paused)
        movie->start += Clock::now() - movie->pausedAt;
    movie->paused = pause != 0;
    return movie->paused;
}

S32 RADEXPLINK BinkGetRects(HBINK bink, U32)
{
    const Movie *movie = reinterpret_cast<const Movie *>(bink);
    bink->NumRects = movie->hasRects ? 1 : 0;
    return bink->NumRects;
}

void RADEXPLINK BinkGetRealtime(HBINK bink, BINKREALTIME *run, U32)
{
    memset(run, 0, sizeof(*run));
    run->FrameNum = bink->FrameNum;
    run->FrameRate = bink->FrameRate;
    run->FrameRateDiv = bink->FrameRateDiv;
}

// Reads are synchronous and the audio track isn't played, so these have
// nothing to configure.
S32 RADEXPLINK BinkControlBackgroundIO(HBINK, U32) { return 0; }
void RADEXPLINK BinkSetIOSize(U32) { }
void RADEXPLINK BinkSetMemory(BINKMEMALLOC, BINKMEMFREE) { }
BINKSNDOPEN RADEXPLINK BinkOpenMiles(UINTa) { return nullptr; }
S32 RADEXPLINK BinkSetSoundSystem(BINKSNDSYSOPEN, UINTa) { return 0; }
void RADEXPLINK BinkSetSoundTrack(U32, U32 *) { }
void RADEXPLINK BinkSetMixBinVolumes(HBINK, U32, U32 *, S32 *, U32) { }
