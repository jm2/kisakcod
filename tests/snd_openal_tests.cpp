// snd_openal_tests.cpp: the Miles calls the OpenAL Soft shim
// (sound/snd_openal.cpp) answers, on OpenAL Soft's null backend, which mixes
// in real time with no output device, and its IMA ADPCM decoder
// (sound/snd_openal_adpcm.cpp). Sample headers that don't describe
// whole frames fail closed; a PCM sample plays to SMP_DONE; a looping one
// keeps playing until ended.

// Declare the Miles functions without dllimport, as the shim does.
#define MSS_SPU_PROCESS
#define __RADINEXE__
#include <msslib/mss.h>
#include <sound/snd_openal_adpcm.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
bool g_printedDevice = false; // the shim names the device it opened
} // namespace

void Com_Printf(int, const char *fmt, ...)
{
    if (std::strncmp(fmt, "OpenAL: device ", 15) == 0)
        g_printedDevice = true;
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
    // Reloading the played sample makes it loaded-but-not-started again.
    CHECK(AIL_set_sample_info(sample, &info));
    CHECK(AIL_sample_status(sample) == SMP_STOPPED);
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

// One IMA ADPCM block per channel group: a 4-byte header per channel
// (predictor, step index 0, reserved), then nibble bytes.
std::vector<uint8_t> AdpcmBlock(const std::vector<int16_t> &predictors, uint8_t nibbles, uint32_t blockSize)
{
    std::vector<uint8_t> block(blockSize, nibbles);
    for (size_t c = 0; c < predictors.size(); ++c)
    {
        const uint16_t p = static_cast<uint16_t>(predictors[c]);
        block[c * 4 + 0] = static_cast<uint8_t>(p & 0xFF);
        block[c * 4 + 1] = static_cast<uint8_t>(p >> 8);
        block[c * 4 + 2] = 0;
        block[c * 4 + 3] = 0;
    }
    return block;
}

void TestAdpcmDecode()
{
    using snd_openal::DecodeImaAdpcm;
    std::vector<int16_t> pcm;
    // Zero nibbles never move the predictor: 1 header sample + 64 nibbles.
    std::vector<uint8_t> mono = AdpcmBlock({1000}, 0x00, 36);
    CHECK(DecodeImaAdpcm(mono.data(), 36, 36, 1, kRate, pcm) && pcm.size() == 65);
    bool flat = !pcm.empty();
    for (int16_t v : pcm)
        flat = flat && v == 1000;
    CHECK(flat);
    // A trailing partial block is ignored.
    mono.resize(46, 0);
    CHECK(DecodeImaAdpcm(mono.data(), 46, 36, 1, kRate, pcm) && pcm.size() == 65);
    // Stereo interleaves the channels.
    const std::vector<uint8_t> stereo = AdpcmBlock({1000, -1000}, 0x00, 72);
    CHECK(DecodeImaAdpcm(stereo.data(), 72, 72, 2, kRate, pcm) && pcm.size() == 130);
    CHECK(pcm.size() == 130 && pcm[0] == 1000 && pcm[1] == -1000 && pcm[128] == 1000 && pcm[129] == -1000);
    // Nibble 7 steps up and nibble 15 down.
    const std::vector<uint8_t> up = AdpcmBlock({0}, 0x77, 36);
    CHECK(DecodeImaAdpcm(up.data(), 36, 36, 1, kRate, pcm) && pcm.size() == 65 && pcm[1] > pcm[0] && pcm[64] > pcm[1]);
    const std::vector<uint8_t> down = AdpcmBlock({0}, 0xFF, 36);
    CHECK(DecodeImaAdpcm(down.data(), 36, 36, 1, kRate, pcm) && pcm.size() == 65 && pcm[1] < pcm[0] && pcm[64] < pcm[1]);
    // Headers that can't describe whole blocks fail closed and leave nothing.
    CHECK(!DecodeImaAdpcm(mono.data(), 30, 36, 1, kRate, pcm) && pcm.empty()); // block > data
    CHECK(!DecodeImaAdpcm(mono.data(), 36, 6, 2, kRate, pcm) && pcm.empty());  // block < headers
    CHECK(!DecodeImaAdpcm(mono.data(), 36, 36, 3, kRate, pcm) && pcm.empty());
    CHECK(!DecodeImaAdpcm(nullptr, 36, 36, 1, kRate, pcm) && pcm.empty());
    CHECK(!DecodeImaAdpcm(mono.data(), 36, 36, 1, 0, pcm) && pcm.empty());
}

void TestAdpcmSample(HSAMPLE sample)
{
    const std::vector<uint8_t> block = AdpcmBlock({1000}, 0x00, 36);
    AILSOUNDINFO info{};
    info.format = 0x11; // WAVE_FORMAT_IMA_ADPCM
    info.data_ptr = block.data();
    info.data_len = 36;
    info.rate = kRate;
    info.bits = 4;
    info.channels = 1;
    info.block_size = 36;
    CHECK(AIL_set_sample_info(sample, &info));
    CHECK(AIL_sample_channel_count(sample, nullptr) == 1);
    info.block_size = 40; // past data_len
    CHECK(!AIL_set_sample_info(sample, &info));
}

void Test3dPosition(HSAMPLE sample)
{
    const AILSOUNDINFO info = Pcm16(g_pcm.data(), static_cast<U32>(g_pcm.size() * sizeof(int16_t)));
    CHECK(AIL_set_sample_info(sample, &info));
    F32 x = 1.0f;
    F32 y = 1.0f;
    F32 z = 1.0f;
    CHECK(!AIL_sample_3D_position(sample, &x, &y, &z)); // a fresh sample is 2D
    AIL_set_sample_3D_distances(sample, 1000.0f, 10.0f, 1);
    AIL_set_sample_3D_position(sample, 3.0f, -2.0f, 5.0f);
    CHECK(AIL_sample_3D_position(sample, &x, &y, &z) && x == 3.0f && y == -2.0f && z == 5.0f);
    CHECK(AIL_set_sample_info(sample, &info));
    CHECK(!AIL_sample_3D_position(sample, &x, &y, &z)); // reloading clears it
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
    CHECK(g_printedDevice);
    TestAdpcmDecode();
    HSAMPLE sample = AIL_allocate_sample_handle(driver);
    CHECK(sample != nullptr);
    CHECK(AIL_sample_status(sample) == SMP_DONE); // allocated, nothing loaded
    if (sample)
    {
        TestHeadersFailClosed(sample);
        TestPlaysToDone(sample);
        TestLoopKeepsPlaying(sample);
        TestSeekToEnd(sample);
        TestAdpcmSample(sample);
        Test3dPosition(sample);
        TestLevels(sample);
    }
    // Streams arrive later; until then they fail with a reason.
    CHECK(!AIL_open_stream(driver, "sound/music/x.mp3", 0) && AIL_last_error()[0] != '\0');
    AIL_shutdown();
    if (g_failures)
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
