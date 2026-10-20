// Miles playback through OpenAL Soft for clients without mss32 (KISAK_OPENAL_AUDIO).
//
// mss32 is 32-bit Windows only, so 64-bit and POSIX clients define the Miles
// calls the sound driver (snd_driver.cpp, snd_mss.cpp) makes here. The engine
// keeps its own policy above this layer: channel choice, volumes, the 2D
// speaker map and the listener-relative 3D positions.
// - Each sample handle is one OpenAL source and one buffer. Loaded sounds
//   are PCM, or IMA ADPCM decoded through libavcodec.
// - 2D samples pan from the left/right speaker levels the engine sets; 3D
//   samples take the listener-relative position, with no distance model,
//   because the engine already scales volume with distance (rolloff 0).
// - Reverb, the EQ filter and the room type are not applied.
// - Streams (music, voices, streamed ambience) open through the engine's file
//   callbacks and decode with FFmpeg into a few queued buffers, which one
//   thread refills. One mutex guards the stream list and each stream's
//   decoder; the engine's sample setters touch only the source, as for
//   loaded samples.

// Declare the Miles functions without dllimport, as media_stubs.cpp does.
#define MSS_SPU_PROCESS
#define __RADINEXE__
#include <msslib/mss.h>
#include <qcommon/qcommon.h>

#include <AL/al.h>
#include <AL/alc.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace
{
constexpr S32 kFormatPcm = 1;
constexpr S32 kFormatImaAdpcm = 0x11;
constexpr U32 kMaxRate = 192000;
constexpr int kMaxSamples = 128;

struct Sample
{
    ALuint source = 0;
    ALuint buffer = 0;
    U32 baseRate = 0;     // the loaded sound's rate; AL_PITCH is relative to it
    S32 rate = 0;         // the rate the engine asked for
    U32 lengthMs = 0;
    S32 channels = 0;
    bool loaded = false;  // a buffer is attached
    bool ended = false;   // AIL_end_sample ran, or the sample never started
    F32 volume = 1.0f;
    F32 levels[2] = {1.0f, 1.0f};
    bool is3d = false;
    F32 position[3] = {};
};

struct Driver
{
    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    Sample *samples[kMaxSamples] = {};
    int sampleCount = 0;
};

Driver s_driver;
const char *s_error = "";

Sample *ToSample(HSAMPLE handle)
{
    return reinterpret_cast<Sample *>(handle);
}

void ApplyGainAndPan(Sample *s)
{
    if (!s->source)
        return;
    if (s->is3d)
    {
        // Miles is left-handed (+z ahead); OpenAL looks down -z.
        alSource3f(s->source, AL_POSITION, s->position[0], s->position[1], -s->position[2]);
        alSourcef(s->source, AL_GAIN, s->volume);
        return;
    }
    // A mono 2D sample sits on the unit circle at the angle its left/right
    // levels give, at the louder level; stereo samples play unpanned.
    const F32 left = std::max(s->levels[0], 0.0f);
    const F32 right = std::max(s->levels[1], 0.0f);
    const F32 level = std::max(left, right);
    const F32 angle = level > 0.0f ? std::atan2(right - left, right + left) : 0.0f;
    alSource3f(s->source, AL_POSITION, std::sin(angle), 0.0f, -std::cos(angle));
    alSourcef(s->source, AL_GAIN, s->volume * level);
}

// Decodes IMA ADPCM blocks to interleaved 16-bit PCM; false on any decode error.
bool DecodeImaAdpcm(const AILSOUNDINFO &info, std::vector<int16_t> &out)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_IMA_WAV);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool ok = ctx && packet && frame;
    if (ok)
    {
        ctx->sample_rate = static_cast<int>(info.rate);
        ctx->block_align = static_cast<int>(info.block_size);
        ctx->bits_per_coded_sample = 4;
        av_channel_layout_default(&ctx->ch_layout, info.channels);
        ok = avcodec_open2(ctx, codec, nullptr) >= 0;
    }
    const uint8_t *data = static_cast<const uint8_t *>(info.data_ptr);
    for (U32 offset = 0; ok && offset + info.block_size <= info.data_len; offset += info.block_size)
    {
        packet->data = const_cast<uint8_t *>(data + offset);
        packet->size = static_cast<int>(info.block_size);
        ok = avcodec_send_packet(ctx, packet) >= 0;
        while (ok && avcodec_receive_frame(ctx, frame) == 0)
        {
            ok = frame->format == AV_SAMPLE_FMT_S16P && frame->ch_layout.nb_channels == info.channels;
            for (int i = 0; ok && i < frame->nb_samples; ++i)
                for (int c = 0; c < info.channels; ++c)
                    out.push_back(reinterpret_cast<const int16_t *>(frame->extended_data[c])[i]);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    return ok && !out.empty();
}

// Uploads the loaded sound to the sample's buffer. Fails closed on any header
// value that doesn't describe whole frames inside data_len.
bool Upload(Sample *s, const AILSOUNDINFO &info)
{
    if (!info.data_ptr || !info.data_len || info.channels < 1 || info.channels > 2 || !info.rate
        || info.rate > kMaxRate)
        return false;
    const bool stereo = info.channels == 2;
    std::vector<int16_t> decoded;
    const void *pcm = info.data_ptr;
    ALsizei bytes = 0;
    ALenum format = 0;
    if (info.format == kFormatPcm && (info.bits == 8 || info.bits == 16))
    {
        const U32 frameBytes = static_cast<U32>(info.channels) * (info.bits / 8);
        bytes = static_cast<ALsizei>(std::min<U32>(info.data_len / frameBytes * frameBytes, INT32_MAX));
        format = info.bits == 8 ? (stereo ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8)
                                : (stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16);
    }
    else if (info.format == kFormatImaAdpcm && info.block_size >= 4u * info.channels
             && info.block_size <= info.data_len && info.block_size <= 0x10000)
    {
        if (!DecodeImaAdpcm(info, decoded))
            return false;
        pcm = decoded.data();
        bytes = static_cast<ALsizei>(std::min<size_t>(decoded.size() * sizeof(int16_t), INT32_MAX));
        format = stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    }
    else
    {
        return false;
    }
    if (!bytes)
        return false;
    alSourcei(s->source, AL_BUFFER, 0);
    alBufferData(s->buffer, format, pcm, bytes, static_cast<ALsizei>(info.rate));
    if (alGetError() != AL_NO_ERROR)
        return false;
    alSourcei(s->source, AL_BUFFER, static_cast<ALint>(s->buffer));
    const U32 frames = static_cast<U32>(bytes / (format == AL_FORMAT_MONO8 ? 1 : format == AL_FORMAT_STEREO16 ? 4 : 2));
    s->lengthMs = static_cast<U32>(static_cast<uint64_t>(frames) * 1000 / info.rate);
    s->channels = info.channels;
    return true;
}

void FreeSample(Sample *s)
{
    if (s->source)
    {
        alSourceStop(s->source);
        alSourcei(s->source, AL_BUFFER, 0);
        alDeleteSources(1, &s->source);
    }
    if (s->buffer)
        alDeleteBuffers(1, &s->buffer);
    delete s;
}
} // namespace

namespace
{
// The engine's file callbacks (MSS_File*), which streams read through.
struct FileCallbacks
{
    AIL_file_open_callback open = nullptr;
    AIL_file_close_callback close = nullptr;
    AIL_file_seek_callback seek = nullptr;
    AIL_file_read_callback read = nullptr;
};
FileCallbacks s_files;

void StartStreamThread();
void StopStreams();
} // namespace

// Driver.
S32 AILCALL AIL_startup(void) { return 1; }
char *AILCALL AIL_last_error(void) { return const_cast<char *>(s_error); }
char *AILCALL AIL_set_redist_directory(char const *) { return nullptr; }
SINTa AILCALL AIL_set_preference(U32, SINTa) { return 0; }
void AILCALL AIL_set_file_callbacks(AIL_file_open_callback open, AIL_file_close_callback close, AIL_file_seek_callback seek, AIL_file_read_callback read)
{
    s_files = {open, close, seek, read};
}

void AILCALL AIL_shutdown(void)
{
    StopStreams();
    for (int i = 0; i < s_driver.sampleCount; ++i)
        FreeSample(s_driver.samples[i]);
    s_driver.sampleCount = 0;
    if (s_driver.context)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(s_driver.context);
    }
    if (s_driver.device)
        alcCloseDevice(s_driver.device);
    s_driver = Driver();
}

HDIGDRIVER AILCALL AIL_open_digital_driver(U32 frequency, S32, S32, U32)
{
    if (s_driver.device)
        AIL_shutdown();
    s_driver.device = alcOpenDevice(nullptr);
    if (!s_driver.device)
    {
        s_error = "OpenAL can't open the default output device";
        return nullptr;
    }
    const ALCint attributes[] = {ALC_FREQUENCY, static_cast<ALCint>(frequency), 0};
    s_driver.context = alcCreateContext(s_driver.device, attributes);
    if (!s_driver.context || !alcMakeContextCurrent(s_driver.context))
    {
        s_error = "OpenAL can't create a context";
        AIL_shutdown();
        return nullptr;
    }
    alDistanceModel(AL_NONE);
    ALCint rate = 0;
    alcGetIntegerv(s_driver.device, ALC_FREQUENCY, 1, &rate);
    Com_Printf(CON_CHANNEL_SOUND, "OpenAL: device '%s', %d Hz, %s\n",
               alcGetString(s_driver.device, ALC_DEVICE_SPECIFIER), rate, alGetString(AL_VERSION));
    s_error = "";
    StartStreamThread();
    return reinterpret_cast<HDIGDRIVER>(&s_driver);
}

#if defined(_WIN32)
S32 AILCALL AIL_set_DirectSound_HWND(HDIGDRIVER, HWND) { return 1; }
#endif
S32 AILCALL AIL_digital_CPU_percent(HDIGDRIVER) { return 0; }
void AILCALL AIL_set_speaker_configuration(HDIGDRIVER, MSSVECTOR3D *, S32, F32) { }
MSSVECTOR3D *AILCALL AIL_speaker_configuration(HDIGDRIVER, S32 *, S32 *outputChannels, F32 *, MSS_MC_SPEC *spec)
{
    // Stereo output: the engine then uses its two-speaker map.
    if (outputChannels)
        *outputChannels = 2;
    if (spec)
        *spec = MSS_MC_STEREO;
    return nullptr;
}
void AILCALL AIL_set_3D_distance_factor(HDIGDRIVER, F32) { }
void AILCALL AIL_set_3D_rolloff_factor(HDIGDRIVER, F32) { }
void AILCALL AIL_set_room_type(HDIGDRIVER, S32) { }
void AILCALL AIL_set_digital_master_reverb_levels(HDIGDRIVER, F32, F32) { }

// The engine looks up the EQ filter by name and reports an error without it;
// a placeholder provider is found, and its stage properties do nothing.
S32 AILCALL AIL_enumerate_filters(HPROENUM *, HPROVIDER *, C8 **) { return 0; }
S32 AILCALL AIL_find_filter(C8 const *, HPROVIDER *provider)
{
    if (provider)
        *provider = 1;
    return 1;
}
HDRIVERSTATE AILCALL AIL_open_filter(HPROVIDER, HDIGDRIVER) { return 1; }

// Loose .wav files are not supported: fast files carry every loaded sound.
S32 AILCALL AIL_WAV_info(void const *, AILSOUNDINFO *) { return 0; }
// With snd_khz 44 the engine never resamples a loaded sound (playback_rate is
// unbounded). Below that, sounds above the rate would need this; they stay
// silent instead.
S32 AILCALL AIL_size_processed_digital_audio(U32, U32, S32, AILMIXINFO const *) { return 0; }
S32 AILCALL AIL_process_digital_audio(void *, S32, U32, U32, S32, AILMIXINFO *) { return 0; }

// Samples.
HSAMPLE AILCALL AIL_allocate_sample_handle(HDIGDRIVER)
{
    if (!s_driver.context || s_driver.sampleCount == kMaxSamples)
        return nullptr;
    Sample *s = new (std::nothrow) Sample();
    if (!s)
        return nullptr;
    alGenSources(1, &s->source);
    alGenBuffers(1, &s->buffer);
    if (alGetError() != AL_NO_ERROR)
    {
        FreeSample(s);
        return nullptr;
    }
    alSourcei(s->source, AL_SOURCE_RELATIVE, AL_TRUE);
    s->ended = true;
    s_driver.samples[s_driver.sampleCount++] = s;
    return reinterpret_cast<HSAMPLE>(s);
}

S32 AILCALL AIL_init_sample(HSAMPLE, S32) { return 1; }

S32 AILCALL AIL_set_sample_info(HSAMPLE handle, AILSOUNDINFO const *info)
{
    Sample *s = ToSample(handle);
    alSourceStop(s->source);
    s->loaded = info && Upload(s, *info);
    s->ended = !s->loaded;
    s->baseRate = s->loaded ? info->rate : 0;
    s->rate = static_cast<S32>(s->baseRate);
    s->volume = 1.0f;
    s->levels[0] = s->levels[1] = 1.0f;
    s->is3d = false;
    alSourcef(s->source, AL_PITCH, 1.0f);
    alSourcei(s->source, AL_LOOPING, AL_FALSE);
    ApplyGainAndPan(s);
    return s->loaded;
}

HPROVIDER AILCALL AIL_set_sample_processor(HSAMPLE, SAMPLESTAGE, HPROVIDER) { return 0; }
S32 AILCALL AIL_sample_stage_property(HSAMPLE, SAMPLESTAGE, C8 const *, S32, void *, void const *, void *) { return 0; }

U32 AILCALL AIL_sample_status(HSAMPLE handle)
{
    const Sample *s = ToSample(handle);
    if (!s->loaded || s->ended)
        return SMP_DONE;
    ALint state = AL_INITIAL;
    alGetSourcei(s->source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING ? SMP_PLAYING : state == AL_STOPPED ? SMP_DONE : SMP_STOPPED;
}

void AILCALL AIL_end_sample(HSAMPLE handle)
{
    Sample *s = ToSample(handle);
    alSourceStop(s->source);
    s->ended = true;
}

void AILCALL AIL_stop_sample(HSAMPLE handle)
{
    alSourcePause(ToSample(handle)->source);
}

void AILCALL AIL_resume_sample(HSAMPLE handle)
{
    Sample *s = ToSample(handle);
    if (s->loaded && !s->ended)
        alSourcePlay(s->source);
}

S32 AILCALL AIL_sample_playback_rate(HSAMPLE handle) { return ToSample(handle)->rate; }

void AILCALL AIL_set_sample_playback_rate(HSAMPLE handle, S32 rate)
{
    Sample *s = ToSample(handle);
    if (!s->baseRate || rate <= 0)
        return;
    s->rate = rate;
    alSourcef(s->source, AL_PITCH, std::clamp(static_cast<F32>(rate) / s->baseRate, 0.01f, 8.0f));
}

S32 AILCALL AIL_sample_channel_count(HSAMPLE handle, U32 *mask)
{
    if (mask)
        *mask = 0;
    return ToSample(handle)->channels;
}

void AILCALL AIL_sample_channel_levels(HSAMPLE handle, MSS_SPEAKER const *, MSS_SPEAKER const *, F32 *levels, S32 count)
{
    const Sample *s = ToSample(handle);
    for (S32 i = 0; levels && i < count; ++i)
        levels[i] = i < 2 ? s->levels[i] : 0.0f;
}

void AILCALL AIL_set_sample_channel_levels(HSAMPLE handle, MSS_SPEAKER const *, MSS_SPEAKER const *, F32 const *levels, S32 count)
{
    Sample *s = ToSample(handle);
    if (!levels || count < 1)
        return;
    s->levels[0] = levels[0];
    s->levels[1] = count > 1 ? levels[1] : levels[0];
    ApplyGainAndPan(s);
}

void AILCALL AIL_sample_volume_levels(HSAMPLE handle, F32 *left, F32 *right)
{
    const Sample *s = ToSample(handle);
    if (left)
        *left = s->volume;
    if (right)
        *right = s->volume;
}

void AILCALL AIL_set_sample_volume_levels(HSAMPLE handle, F32 left, F32 right)
{
    Sample *s = ToSample(handle);
    s->volume = std::clamp(std::max(left, right), 0.0f, 1.0f);
    ApplyGainAndPan(s);
}

void AILCALL AIL_sample_volume_pan(HSAMPLE handle, F32 *volume, F32 *pan)
{
    if (volume)
        *volume = ToSample(handle)->volume;
    if (pan)
        *pan = 0.5f;
}

void AILCALL AIL_set_sample_reverb_levels(HSAMPLE, F32, F32) { }

void AILCALL AIL_set_sample_loop_count(HSAMPLE handle, S32 count)
{
    // Miles: 0 loops forever, 1 plays once.
    alSourcei(ToSample(handle)->source, AL_LOOPING, count == 0 ? AL_TRUE : AL_FALSE);
}

void AILCALL AIL_sample_ms_position(HSAMPLE handle, S32 *total, S32 *current)
{
    const Sample *s = ToSample(handle);
    if (total)
        *total = static_cast<S32>(s->lengthMs);
    if (current)
    {
        ALfloat seconds = 0.0f;
        alGetSourcef(s->source, AL_SEC_OFFSET, &seconds);
        *current = static_cast<S32>(seconds * 1000.0f);
    }
}

void AILCALL AIL_set_sample_ms_position(HSAMPLE handle, S32 ms)
{
    const Sample *s = ToSample(handle);
    if (s->loaded && ms > 0)
        alSourcef(s->source, AL_SEC_OFFSET, static_cast<ALfloat>(std::min<U32>(ms, s->lengthMs)) / 1000.0f);
}

S32 AILCALL AIL_sample_3D_position(HSAMPLE handle, F32 *x, F32 *y, F32 *z)
{
    const Sample *s = ToSample(handle);
    if (x)
        *x = s->position[0];
    if (y)
        *y = s->position[1];
    if (z)
        *z = s->position[2];
    return s->is3d;
}

void AILCALL AIL_set_sample_3D_position(HSAMPLE handle, F32 x, F32 y, F32 z)
{
    Sample *s = ToSample(handle);
    s->is3d = true;
    s->position[0] = x;
    s->position[1] = y;
    s->position[2] = z;
    ApplyGainAndPan(s);
}

void AILCALL AIL_set_sample_3D_distances(HSAMPLE handle, F32, F32, S32)
{
    Sample *s = ToSample(handle);
    s->is3d = true;
    ApplyGainAndPan(s);
}

// Streams.
namespace
{
constexpr int kStreamBuffers = 4;
constexpr int kChunkFrames = 4096; // about 0.1 s per queued buffer at 44.1 kHz

// Miles seek origins, as MSS_FileSeekCallback reads them.
constexpr U32 kSeekBegin = 0;
constexpr U32 kSeekCurrent = 1;
constexpr U32 kSeekEnd = 2;

struct Stream
{
    Sample sample; // what AIL_stream_sample_handle returns
    UINTa file = 0;
    bool fileOpen = false;
    AVIOContext *io = nullptr;
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;
    ALuint buffers[kStreamBuffers] = {};
    std::vector<ALuint> freeBuffers;
    std::vector<int> queuedFrames; // per queued buffer, oldest first
    std::vector<int16_t> pcm;      // decoded, not yet queued
    int64_t playedFrames = 0;      // frames in buffers already played
    bool loop = false;
    bool eof = false;
    bool paused = true;
};

std::mutex s_streamMutex;
std::condition_variable s_streamWake;
std::vector<Stream *> s_streams;
std::thread s_streamThread;
bool s_streamStop = false;

int StreamRead(void *opaque, uint8_t *buf, int size)
{
    const Stream *st = static_cast<const Stream *>(opaque);
    const U32 got = s_files.read(st->file, buf, static_cast<U32>(size));
    return got ? static_cast<int>(got) : AVERROR_EOF;
}

int64_t StreamSeek(void *opaque, int64_t offset, int whence)
{
    const Stream *st = static_cast<const Stream *>(opaque);
    if (offset < INT32_MIN || offset > INT32_MAX)
        return AVERROR(EINVAL);
    if (whence == AVSEEK_SIZE)
    {
        const S32 here = s_files.seek(st->file, 0, kSeekCurrent);
        const S32 end = s_files.seek(st->file, 0, kSeekEnd);
        s_files.seek(st->file, here, kSeekBegin);
        return end;
    }
    const U32 origin = whence == SEEK_CUR ? kSeekCurrent : whence == SEEK_END ? kSeekEnd : kSeekBegin;
    return s_files.seek(st->file, static_cast<S32>(offset), origin);
}

void FreeStream(Stream *st)
{
    if (st->sample.source)
    {
        alSourceStop(st->sample.source);
        alSourcei(st->sample.source, AL_BUFFER, 0);
        alDeleteSources(1, &st->sample.source);
    }
    alDeleteBuffers(kStreamBuffers, st->buffers);
    av_frame_free(&st->frame);
    av_packet_free(&st->packet);
    avcodec_free_context(&st->codec);
    avformat_close_input(&st->format);
    if (st->io)
    {
        av_freep(&st->io->buffer);
        avio_context_free(&st->io);
    }
    if (st->fileOpen)
        s_files.close(st->file);
    delete st;
}

// Appends the frame as interleaved 16-bit PCM; false for a format it can't take.
bool AppendFrame(Stream *st, const AVFrame *frame)
{
    const int channels = frame->ch_layout.nb_channels;
    const AVSampleFormat fmt = static_cast<AVSampleFormat>(frame->format);
    const bool planar = av_sample_fmt_is_planar(fmt);
    const AVSampleFormat packed = av_get_packed_sample_fmt(fmt);
    if (channels != st->sample.channels)
        return false;
    for (int i = 0; i < frame->nb_samples; ++i)
    {
        for (int c = 0; c < channels; ++c)
        {
            const int index = planar ? i : i * channels + c;
            const uint8_t *plane = frame->extended_data[planar ? c : 0];
            float value = 0.0f;
            switch (packed)
            {
            case AV_SAMPLE_FMT_U8: value = (plane[index] - 128) / 128.0f; break;
            case AV_SAMPLE_FMT_S16: value = reinterpret_cast<const int16_t *>(plane)[index] / 32768.0f; break;
            case AV_SAMPLE_FMT_S32: value = reinterpret_cast<const int32_t *>(plane)[index] / 2147483648.0f; break;
            case AV_SAMPLE_FMT_FLT: value = reinterpret_cast<const float *>(plane)[index]; break;
            case AV_SAMPLE_FMT_DBL: value = static_cast<float>(reinterpret_cast<const double *>(plane)[index]); break;
            default: return false;
            }
            st->pcm.push_back(static_cast<int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f)));
        }
    }
    return true;
}

// Decodes until a chunk is ready or the stream ends (rewinding if it loops).
void DecodeChunk(Stream *st)
{
    const size_t want = static_cast<size_t>(kChunkFrames) * st->sample.channels;
    while (st->pcm.size() < want && !st->eof)
    {
        const int got = avcodec_receive_frame(st->codec, st->frame);
        if (got == 0)
        {
            st->eof = !AppendFrame(st, st->frame);
            continue;
        }
        if (got == AVERROR_EOF && st->loop && av_seek_frame(st->format, st->stream, 0, AVSEEK_FLAG_BACKWARD) >= 0)
        {
            avcodec_flush_buffers(st->codec);
            continue;
        }
        if (got != AVERROR(EAGAIN))
        {
            st->eof = true;
            break;
        }
        if (av_read_frame(st->format, st->packet) < 0)
        {
            avcodec_send_packet(st->codec, nullptr); // drain
            continue;
        }
        if (st->packet->stream_index == st->stream)
            avcodec_send_packet(st->codec, st->packet);
        av_packet_unref(st->packet);
    }
}

// Recycles played buffers and queues decoded audio into free ones. Holds
// s_streamMutex.
void Refill(Stream *st)
{
    const ALuint source = st->sample.source;
    ALint processed = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
    for (; processed > 0 && !st->queuedFrames.empty(); --processed)
    {
        ALuint buffer = 0;
        alSourceUnqueueBuffers(source, 1, &buffer);
        st->playedFrames += st->queuedFrames.front();
        st->queuedFrames.erase(st->queuedFrames.begin());
        st->freeBuffers.push_back(buffer);
    }
    while (!st->freeBuffers.empty())
    {
        DecodeChunk(st);
        if (st->pcm.empty())
            break;
        const size_t samples = std::min(st->pcm.size(), static_cast<size_t>(kChunkFrames) * st->sample.channels);
        const ALuint buffer = st->freeBuffers.back();
        alBufferData(buffer, st->sample.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, st->pcm.data(),
                     static_cast<ALsizei>(samples * sizeof(int16_t)), static_cast<ALsizei>(st->sample.baseRate));
        alSourceQueueBuffers(source, 1, &buffer);
        st->freeBuffers.pop_back();
        st->queuedFrames.push_back(static_cast<int>(samples / st->sample.channels));
        st->pcm.erase(st->pcm.begin(), st->pcm.begin() + static_cast<ptrdiff_t>(samples));
    }
    ALint state = AL_STOPPED;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    if (!st->paused && state != AL_PLAYING && !st->queuedFrames.empty())
        alSourcePlay(source); // first start, or recovery after an underrun
}

bool Finished(const Stream *st)
{
    return st->eof && st->pcm.empty() && st->queuedFrames.empty();
}

void StreamThread()
{
    std::unique_lock<std::mutex> lock(s_streamMutex);
    while (!s_streamStop)
    {
        for (Stream *st : s_streams)
            Refill(st);
        s_streamWake.wait_for(lock, std::chrono::milliseconds(10));
    }
}

void StartStreamThread()
{
    s_streamStop = false;
    s_streamThread = std::thread(StreamThread);
}

void StopStreams()
{
    {
        std::lock_guard<std::mutex> lock(s_streamMutex);
        s_streamStop = true;
    }
    s_streamWake.notify_all();
    if (s_streamThread.joinable())
        s_streamThread.join();
    for (Stream *st : s_streams)
        FreeStream(st);
    s_streams.clear();
}

// Opens the file through the engine's callbacks and its audio decoder;
// false (with the error set) on failure.
bool OpenStreamDecoder(Stream *st, const char *filename)
{
    st->fileOpen = s_files.open && s_files.open(filename, &st->file);
    if (!st->fileOpen)
    {
        s_error = "can't open the stream's file";
        return false;
    }
    const int bufferSize = 32 * 1024;
    uint8_t *buffer = static_cast<uint8_t *>(av_malloc(bufferSize));
    st->io = buffer ? avio_alloc_context(buffer, bufferSize, 0, st, StreamRead, nullptr, StreamSeek) : nullptr;
    st->format = st->io ? avformat_alloc_context() : nullptr;
    if (!st->format)
    {
        if (!st->io)
            av_free(buffer);
        s_error = "out of memory";
        return false;
    }
    st->format->pb = st->io;
    const AVCodec *decoder = nullptr;
    if (avformat_open_input(&st->format, nullptr, nullptr, nullptr) < 0
        || avformat_find_stream_info(st->format, nullptr) < 0
        || (st->stream = av_find_best_stream(st->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0)) < 0 || !decoder)
    {
        s_error = "FFmpeg can't read the stream";
        return false;
    }
    st->codec = avcodec_alloc_context3(decoder);
    st->packet = av_packet_alloc();
    st->frame = av_frame_alloc();
    if (!st->codec || !st->packet || !st->frame
        || avcodec_parameters_to_context(st->codec, st->format->streams[st->stream]->codecpar) < 0
        || avcodec_open2(st->codec, decoder, nullptr) < 0)
    {
        s_error = "FFmpeg can't open the stream's decoder";
        return false;
    }
    const int channels = st->codec->ch_layout.nb_channels;
    if (channels < 1 || channels > 2 || st->codec->sample_rate <= 0 || static_cast<U32>(st->codec->sample_rate) > kMaxRate)
    {
        s_error = "unsupported stream format";
        return false;
    }
    return true;
}

Stream *ToStream(HSTREAM handle)
{
    return reinterpret_cast<Stream *>(handle);
}
} // namespace

HSTREAM AILCALL AIL_open_stream(HDIGDRIVER, char const *filename, S32)
{
    if (!s_driver.context || !filename)
        return nullptr;
    Stream *st = new (std::nothrow) Stream();
    if (!st)
        return nullptr;
    alGenSources(1, &st->sample.source);
    alGenBuffers(kStreamBuffers, st->buffers);
    if (alGetError() != AL_NO_ERROR || !OpenStreamDecoder(st, filename))
    {
        if (!*s_error)
            s_error = "OpenAL can't create the stream's source";
        FreeStream(st);
        return nullptr;
    }
    Sample &sample = st->sample;
    sample.channels = st->codec->ch_layout.nb_channels;
    sample.baseRate = static_cast<U32>(st->codec->sample_rate);
    sample.rate = st->codec->sample_rate;
    sample.loaded = true;
    const int64_t duration = st->format->duration;
    sample.lengthMs = duration > 0 ? static_cast<U32>(std::min<int64_t>(duration / 1000, INT32_MAX)) : 0;
    alSourcei(sample.source, AL_SOURCE_RELATIVE, AL_TRUE);
    ApplyGainAndPan(&sample);
    st->freeBuffers.assign(st->buffers, st->buffers + kStreamBuffers);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    Refill(st); // prime the queue; the stream starts paused
    s_streams.push_back(st);
    return reinterpret_cast<HSTREAM>(st);
}

void AILCALL AIL_close_stream(HSTREAM handle)
{
    Stream *st = ToStream(handle);
    if (!st)
        return;
    std::lock_guard<std::mutex> lock(s_streamMutex);
    s_streams.erase(std::remove(s_streams.begin(), s_streams.end(), st), s_streams.end());
    FreeStream(st);
}

void AILCALL AIL_pause_stream(HSTREAM handle, S32 onoff)
{
    Stream *st = ToStream(handle);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    st->paused = onoff != 0;
    if (st->paused)
        alSourcePause(st->sample.source);
    else
        Refill(st);
}

S32 AILCALL AIL_stream_status(HSTREAM handle)
{
    const Stream *st = ToStream(handle);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    return Finished(st) ? SMP_DONE : st->paused ? SMP_STOPPED : SMP_PLAYING;
}

HSAMPLE AILCALL AIL_stream_sample_handle(HSTREAM handle)
{
    return reinterpret_cast<HSAMPLE>(&ToStream(handle)->sample);
}

void AILCALL AIL_stream_info(HSTREAM handle, S32 *datarate, S32 *sndtype, S32 *length, S32 *memory)
{
    const Sample &sample = ToStream(handle)->sample;
    if (datarate)
        *datarate = static_cast<S32>(sample.baseRate);
    if (sndtype)
        *sndtype = sample.channels == 2 ? DIG_F_STEREO_16 : DIG_F_MONO_16;
    if (length)
        *length = 0;
    if (memory)
        *memory = 0;
}

void AILCALL AIL_set_stream_loop_count(HSTREAM handle, S32 count)
{
    Stream *st = ToStream(handle);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    st->loop = count == 0; // Miles: 0 loops forever
}

void AILCALL AIL_stream_ms_position(HSTREAM handle, S32 *total, S32 *current)
{
    const Stream *st = ToStream(handle);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    if (total)
        *total = static_cast<S32>(st->sample.lengthMs);
    if (current)
    {
        ALint offset = 0;
        alGetSourcei(st->sample.source, AL_SAMPLE_OFFSET, &offset);
        *current = static_cast<S32>((st->playedFrames + offset) * 1000 / st->sample.baseRate);
    }
}

void AILCALL AIL_set_stream_ms_position(HSTREAM handle, S32 ms)
{
    Stream *st = ToStream(handle);
    std::lock_guard<std::mutex> lock(s_streamMutex);
    const int64_t clamped = std::clamp<int64_t>(ms, 0, st->sample.lengthMs);
    if (av_seek_frame(st->format, -1, clamped * (AV_TIME_BASE / 1000), AVSEEK_FLAG_BACKWARD) < 0)
        return;
    avcodec_flush_buffers(st->codec);
    alSourceStop(st->sample.source);
    alSourcei(st->sample.source, AL_BUFFER, 0); // unqueues everything
    st->freeBuffers.assign(st->buffers, st->buffers + kStreamBuffers);
    st->queuedFrames.clear();
    st->pcm.clear();
    st->eof = false;
    st->playedFrames = clamped * st->sample.baseRate / 1000;
    Refill(st);
}
