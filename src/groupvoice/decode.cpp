#ifndef KISAK_MP
#error This File is MultiPlayer Only
#endif

#include "directsound.h"
#include <speex/speex.h>
#include <qcommon/qcommon.h>

void *g_decoder;
int32_t g_current_decode_bandwidth_setting;
SpeexBits decodeBits;
int32_t g_decode_frame_size;


char __cdecl Decode_Init(int32_t bandwidthEnum)
{
    const SpeexMode *mode; // [esp+8h] [ebp-4h]

    if (bandwidthEnum)
    {
        if (bandwidthEnum == 1)
        {
            mode = &speex_wb_mode;
        }
        else
        {
            if (bandwidthEnum != 2)
            {
                Com_Printf(9, "Unknown bandwidth mode %i\n", bandwidthEnum);
                return 0;
            }
            mode = &speex_uwb_mode;
        }
    }
    else
    {
        mode = &speex_nb_mode;
    }
    g_decoder = speex_decoder_init(mode);
    int32_t tmp = 1;
    speex_decoder_ctl(g_decoder, SPEEX_SET_ENH, &tmp);
    Decode_SetOptions();
    speex_decoder_ctl(g_decoder, SPEEX_GET_FRAME_SIZE, &g_decode_frame_size);
    g_current_decode_bandwidth_setting = bandwidthEnum;
    speex_bits_init(&decodeBits);
    return 1;
}

void __cdecl Decode_SetOptions()
{
    speex_decoder_ctl(g_decoder, SPEEX_SET_SAMPLING_RATE, &g_encoder_samplerate);
}

void __cdecl Decode_Shutdown()
{
    if (g_decoder)
    {
        speex_bits_destroy(&decodeBits);
        speex_decoder_destroy(g_decoder);
    }
    g_decoder = 0;
}

int32_t __cdecl Decode_Sample(char *buffer, int32_t maxLength, int16_t *out, int32_t frame_size)
{
    int32_t v5; // [esp+0h] [ebp-400Ch]
    float v6[4097]; // [esp+4h] [ebp-4008h]
    int32_t i; // [esp+4008h] [ebp-4h]

    iassert(maxLength <= 4096);
    iassert(maxLength <= frame_size);

    speex_bits_read_from(&decodeBits, buffer, maxLength);
    if (speex_decode(g_decoder, &decodeBits, v6))
        return 0;
    // speex_decode writes one decoder frame: convert exactly that many
    // samples, clamped to int16, and return their byte count. The caller's
    // frame_size is the encoder's (the same for a matching bandwidth).
    v5 = g_decode_frame_size;
    if (v5 > 4096)
        v5 = 4096;
    for (i = 0; i < v5; ++i)
    {
        const float s = v6[i];
        out[i] = s >= 32767.0f ? 32767 : s <= -32768.0f ? -32768 : static_cast<int16_t>(s);
    }
    (void)frame_size;
    return 2 * v5;
}

