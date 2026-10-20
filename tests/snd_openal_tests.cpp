// snd_openal_tests.cpp: the Miles calls the OpenAL Soft shim
// (sound/snd_openal.cpp) answers, on OpenAL Soft's null backend, which mixes
// in real time with no output device. Sample headers that don't describe
// whole frames fail closed; a PCM sample plays to SMP_DONE; a looping one
// keeps playing until ended.

// Declare the Miles functions without dllimport, as the shim does.
#define MSS_SPU_PROCESS
#define __RADINEXE__
#include <msslib/mss.h>

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

// The shim prints the device it opened.
void Com_Printf(int, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
}

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

constexpr U32 kRate = 20000;
const std::vector<int16_t> g_pcm(kRate / 20, 1000); // 50 ms of mono 16-bit PCM

AILSOUNDINFO Pcm16(const void *data, U32 bytes, S32 channels = 1)
{
    AILSOUNDINFO info{};
    info.format = 1; // WAVE_FORMAT_PCM
    info.data_ptr = data;
    info.data_len = bytes;
    info.rate = kRate;
    info.bits = 16;
    info.channels = channels;
    info.block_size = static_cast<U32>(2 * channels);
    return info;
}

U32 StatusAfter(HSAMPLE sample, int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return AIL_sample_status(sample);
}

void TestHeadersFailClosed(HSAMPLE sample)
{
    const U32 bytes = static_cast<U32>(g_pcm.size() * sizeof(int16_t));
    AILSOUNDINFO info = Pcm16(g_pcm.data(), bytes);
    info.channels = 0;
    CHECK(!AIL_set_sample_info(sample, &info));
    info.channels = 3;
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(g_pcm.data(), 1); // half of one 16-bit frame
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(g_pcm.data(), 3, 2); // less than one stereo frame
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(nullptr, bytes);
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(g_pcm.data(), bytes);
    info.bits = 24;
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(g_pcm.data(), bytes);
    info.rate = 0;
    CHECK(!AIL_set_sample_info(sample, &info));
    info = Pcm16(g_pcm.data(), bytes);
    info.format = 0x55; // MP3 is a stream format, never a loaded sound
    CHECK(!AIL_set_sample_info(sample, &info));
    // A sample whose header failed never starts.
    AIL_resume_sample(sample);
    CHECK(AIL_sample_status(sample) == SMP_DONE);
}

void TestPlaysToDone(HSAMPLE sample)
{
    // An odd byte count plays its whole frames.
    const AILSOUNDINFO info = Pcm16(g_pcm.data(), static_cast<U32>(g_pcm.size() * sizeof(int16_t) + 1));
    CHECK(AIL_set_sample_info(sample, &info));
    CHECK(AIL_sample_playback_rate(sample) == static_cast<S32>(kRate));
    S32 total = 0;
    AIL_sample_ms_position(sample, &total, nullptr);
    CHECK(total == 50);
    CHECK(AIL_sample_status(sample) == SMP_STOPPED); // loaded, not started
    AIL_set_sample_loop_count(sample, 1);
    AIL_resume_sample(sample);
    CHECK(AIL_sample_status(sample) == SMP_PLAYING);
    CHECK(StatusAfter(sample, 400) == SMP_DONE);
}

void TestLoopKeepsPlaying(HSAMPLE sample)
{
    const AILSOUNDINFO info = Pcm16(g_pcm.data(), static_cast<U32>(g_pcm.size() * sizeof(int16_t)));
    CHECK(AIL_set_sample_info(sample, &info));
    AIL_set_sample_loop_count(sample, 0); // Miles: 0 loops forever
    AIL_resume_sample(sample);
    CHECK(StatusAfter(sample, 400) == SMP_PLAYING);
    AIL_stop_sample(sample);
    CHECK(AIL_sample_status(sample) == SMP_STOPPED);
    AIL_resume_sample(sample);
    CHECK(AIL_sample_status(sample) == SMP_PLAYING);
    AIL_end_sample(sample);
    CHECK(AIL_sample_status(sample) == SMP_DONE);
}

void TestSeekToEnd(HSAMPLE sample)
{
    // On a playing source an offset at the end is out of range for OpenAL.
    // The shim clamps below it, and no error may linger to fail the next
    // upload.
    const AILSOUNDINFO info = Pcm16(g_pcm.data(), static_cast<U32>(g_pcm.size() * sizeof(int16_t)));
    CHECK(AIL_set_sample_info(sample, &info));
    AIL_set_sample_loop_count(sample, 0);
    AIL_resume_sample(sample);
    AIL_set_sample_ms_position(sample, 50);
    AIL_set_sample_ms_position(sample, 5000);
    CHECK(AIL_sample_status(sample) == SMP_PLAYING);
    CHECK(AIL_set_sample_info(sample, &info));
}

void TestLevels(HSAMPLE sample)
{
    AIL_set_sample_volume_levels(sample, 0.5f, 0.5f);
    F32 left = 0.0f;
    F32 right = 0.0f;
    AIL_sample_volume_levels(sample, &left, &right);
    CHECK(left == 0.5f && right == 0.5f);
    const F32 levels[2] = {0.25f, 0.75f};
    AIL_set_sample_channel_levels(sample, nullptr, nullptr, levels, 2);
    F32 read[2] = {};
    AIL_sample_channel_levels(sample, nullptr, nullptr, read, 2);
    CHECK(read[0] == 0.25f && read[1] == 0.75f);
    AIL_set_sample_playback_rate(sample, static_cast<S32>(kRate * 2));
    CHECK(AIL_sample_playback_rate(sample) == static_cast<S32>(kRate * 2));
}
} // namespace

int main()
{
    // OpenAL Soft's null backend: a mixer with no device, so CI needs no audio.
    setenv("ALSOFT_DRIVERS", "null", 1);
    CHECK(AIL_startup());
    HDIGDRIVER driver = AIL_open_digital_driver(44100, 16, 2, 0);
    CHECK(driver != nullptr);
    if (!driver)
    {
        std::fprintf(stderr, "no OpenAL device: %s\n", AIL_last_error());
        return 1;
    }
    HSAMPLE sample = AIL_allocate_sample_handle(driver);
    CHECK(sample != nullptr);
    CHECK(AIL_sample_status(sample) == SMP_DONE); // allocated, nothing loaded
    if (sample)
    {
        TestHeadersFailClosed(sample);
        TestPlaysToDone(sample);
        TestLoopKeepsPlaying(sample);
        TestSeekToEnd(sample);
        TestLevels(sample);
    }
    // Streams arrive later; until then they fail with a reason.
    CHECK(!AIL_open_stream(driver, "sound/music/x.mp3", 0) && AIL_last_error()[0] != '\0');
    AIL_shutdown();
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
