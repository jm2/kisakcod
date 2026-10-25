// SPDX-License-Identifier: GPL-3.0-only
//
// voice_decode_sample_tests.cpp -- drives the shipped receive-side codec
// wrapper (groupvoice/decode.cpp: Decode_Init, Decode_Sample) over the
// in-tree Speex build. A narrowband frame decodes to exactly one frame of
// PCM: Decode_Sample must write g_decode_frame_size samples, return their
// byte count, and leave the rest of the caller's buffer untouched.

#include <groupvoice/directsound.h>
#include <speex/speex.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern int32_t g_decode_frame_size;

// Engine hooks decode.cpp references.
void Com_Printf(int, const char *, ...) {}
void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "voice-decode-sample: assert at %s:%d: ", filename, line);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::exit(EXIT_FAILURE);
}

namespace
{
constexpr int16_t Canary = 0x7A7A;

bool Check(const bool condition, const char *what)
{
    if (!condition)
        std::fprintf(stderr, "voice-decode-sample: %s failed\n", what);
    return condition;
}

// One narrowband frame of a 440 Hz tone, encoded the way encode.cpp does.
int EncodeToneFrame(char *bits, const int capacity, int *frameSize)
{
    void *encoder = speex_encoder_init(&speex_nb_mode);
    speex_encoder_ctl(encoder, SPEEX_GET_FRAME_SIZE, frameSize);
    int quality = g_encoder_quality;
    speex_encoder_ctl(encoder, SPEEX_SET_QUALITY, &quality);
    speex_encoder_ctl(encoder, SPEEX_SET_SAMPLING_RATE, &g_encoder_samplerate);
    float pcm[640] = {};
    for (int i = 0; i < *frameSize; ++i)
        pcm[i] = 8000.0f * std::sin(6.2831853f * 440.0f * static_cast<float>(i) / 8000.0f);
    SpeexBits encodeBits;
    speex_bits_init(&encodeBits);
    speex_encode(encoder, pcm, &encodeBits);
    const int bytes = speex_bits_write(&encodeBits, bits, capacity);
    speex_bits_destroy(&encodeBits);
    speex_encoder_destroy(encoder);
    return bytes;
}
} // namespace

int main()
{
    char bits[256] = {};
    int frameSize = 0;
    const int encoded = EncodeToneFrame(bits, sizeof(bits), &frameSize);
    if (!Check(encoded > 0 && frameSize == 160, "narrowband encode"))
        return EXIT_FAILURE;

    if (!Check(Decode_Init(0) != 0, "Decode_Init narrowband")
        || !Check(g_decode_frame_size == frameSize, "decoder frame size"))
        return EXIT_FAILURE;

    static int16_t out[4096];
    for (int16_t &sample : out)
        sample = Canary;
    // Voice_IncomingVoiceData passes the encoder frame size (g_frame_size).
    const int32_t bytes = Decode_Sample(bits, encoded, out, frameSize);
    bool ok = Check(bytes == 2 * frameSize, "Decode_Sample returns one frame of bytes");

    int touchedPastFrame = 0;
    for (int i = frameSize; i < 4096; ++i)
        touchedPastFrame += out[i] != Canary;
    ok = Check(touchedPastFrame == 0, "nothing written past one decoded frame") && ok;

    int audible = 0;
    for (int i = 0; i < frameSize; ++i)
        audible += out[i] != 0 && out[i] != Canary;
    ok = Check(audible > 0, "the decoded frame carries the tone") && ok;

    Decode_Shutdown();
    if (!ok)
        return EXIT_FAILURE;
    std::printf("voice-decode-sample: all checks passed\n");
    return EXIT_SUCCESS;
}
