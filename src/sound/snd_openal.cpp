// Miles playback through OpenAL Soft for clients without mss32 (KISAK_OPENAL_AUDIO).
//
// mss32 is 32-bit Windows only, so 64-bit and POSIX clients define the Miles
// calls the sound driver (snd_driver.cpp, snd_mss.cpp) makes here. The engine
// keeps its own policy above this layer: channel choice, volumes, the 2D
// speaker map and the listener-relative 3D positions.
// - Each sample handle is one OpenAL source and one buffer. Loaded sounds
//   upload as 8- or 16-bit PCM; other formats (IMA ADPCM) don't play yet.
// - 2D samples pan from the left/right speaker levels the engine sets. 3D
//   samples play at the engine's volume, unpositioned for now; distance is
//   already in that volume (the engine sets rolloff 0), so OpenAL's
//   distance model is off.
// - Reverb, the EQ filter and the room type are not applied.
// - Streams (music and streamed sounds) don't open yet.

// Declare the Miles functions without dllimport, as media_stubs.cpp does.
#define MSS_SPU_PROCESS
#define __RADINEXE__
#include <msslib/mss.h>
#include <qcommon/qcommon.h>

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace
{
constexpr S32 kFormatPcm = 1;
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
    // A mono 2D sample sits on the unit circle at the angle its left/right
    // levels give, at the louder level; stereo samples play unpanned.
    const F32 left = std::max(s->levels[0], 0.0f);
    const F32 right = std::max(s->levels[1], 0.0f);
    const F32 level = std::max(left, right);
    const F32 angle = level > 0.0f ? std::atan2(right - left, right + left) : 0.0f;
    alSource3f(s->source, AL_POSITION, std::sin(angle), 0.0f, -std::cos(angle));
    alSourcef(s->source, AL_GAIN, s->volume * level);
}

// Whether the header describes PCM this shim plays: 8- or 16-bit, mono or
// stereo, at a rate OpenAL takes.
bool IsPlayablePcm(const AILSOUNDINFO &info)
{
    const bool bitsOk = info.bits == 8 || info.bits == 16;
    const bool channelsOk = info.channels == 1 || info.channels == 2;
    const bool rateOk = info.rate > 0 && info.rate <= kMaxRate;
    return info.data_ptr && info.format == kFormatPcm && bitsOk && channelsOk && rateOk;
}

// Uploads the loaded sound to the sample's buffer. Fails closed on any header
// value that doesn't describe whole frames inside data_len.
bool Upload(Sample *s, const AILSOUNDINFO &info)
{
    if (!IsPlayablePcm(info))
        return false;
    const bool stereo = info.channels == 2;
    const U32 frameBytes = static_cast<U32>(info.channels) * static_cast<U32>(info.bits / 8);
    const U32 frames = std::min<U32>(info.data_len / frameBytes, INT32_MAX / frameBytes);
    if (!frames)
        return false;
    const ALenum format = info.bits == 8 ? (stereo ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8)
                                         : (stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16);
    alGetError(); // only this upload's errors count
    alSourcei(s->source, AL_BUFFER, 0);
    alBufferData(s->buffer, format, info.data_ptr, static_cast<ALsizei>(frames * frameBytes),
                 static_cast<ALsizei>(info.rate));
    if (alGetError() != AL_NO_ERROR)
        return false;
    alSourcei(s->source, AL_BUFFER, static_cast<ALint>(s->buffer));
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

// Driver.
S32 AILCALL AIL_startup(void) { return 1; }
char *AILCALL AIL_last_error(void) { return const_cast<char *>(s_error); }
char *AILCALL AIL_set_redist_directory(char const *) { return nullptr; }
SINTa AILCALL AIL_set_preference(U32, SINTa) { return 0; }
void AILCALL AIL_set_file_callbacks(AIL_file_open_callback, AIL_file_close_callback, AIL_file_seek_callback, AIL_file_read_callback) { }

void AILCALL AIL_shutdown(void)
{
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
    alGetError(); // only this allocation's errors count
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
    // Back to AL_INITIAL: a reused source left AL_STOPPED would read as
    // SMP_DONE before the engine resumes the new sound.
    alSourceRewind(s->source);
    s->loaded = info && Upload(s, *info);
    s->ended = !s->loaded;
    s->baseRate = s->loaded ? info->rate : 0;
    s->rate = static_cast<S32>(s->baseRate);
    s->volume = 1.0f;
    s->levels[0] = s->levels[1] = 1.0f;
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
    // An offset at or past the end is out of range for AL_SEC_OFFSET.
    if (s->loaded && ms > 0 && s->lengthMs > 0)
        alSourcef(s->source, AL_SEC_OFFSET, static_cast<ALfloat>(std::min<U32>(ms, s->lengthMs - 1)) / 1000.0f);
}

// 3D samples are not positioned yet; see the file comment.
S32 AILCALL AIL_sample_3D_position(HSAMPLE, F32 *x, F32 *y, F32 *z)
{
    if (x)
        *x = 0.0f;
    if (y)
        *y = 0.0f;
    if (z)
        *z = 0.0f;
    return 0;
}
void AILCALL AIL_set_sample_3D_position(HSAMPLE, F32, F32, F32) { }
void AILCALL AIL_set_sample_3D_distances(HSAMPLE, F32, F32, S32) { }

// Streams.
HSTREAM AILCALL AIL_open_stream(HDIGDRIVER, char const *, S32)
{
    s_error = "streamed sounds are not supported yet";
    return nullptr;
}
void AILCALL AIL_close_stream(HSTREAM) { }
void AILCALL AIL_pause_stream(HSTREAM, S32) { }
S32 AILCALL AIL_stream_status(HSTREAM) { return SMP_DONE; }
HSAMPLE AILCALL AIL_stream_sample_handle(HSTREAM) { return nullptr; }
void AILCALL AIL_stream_info(HSTREAM, S32 *, S32 *, S32 *, S32 *) { }
void AILCALL AIL_set_stream_loop_count(HSTREAM, S32) { }
void AILCALL AIL_stream_ms_position(HSTREAM, S32 *, S32 *) { }
void AILCALL AIL_set_stream_ms_position(HSTREAM, S32) { }
