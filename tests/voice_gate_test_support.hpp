#pragma once
// Shared infrastructure for the voice codec parity gates (VOX-1a / VOX-1b,
// docs/AUDIO_VOICE_CINEMATIC_PARITY_GATES.md Section 4.2) against the in-tree
// Speex 1.1.9 build that ships with the game (src/groupvoice/speex/*.c,
// public headers deps/speex/).
//
// The suite is split across two translation units to keep each analyzed file
// well under the static-analysis file-size limit:
//
//   tests/voice_gate_tests.cpp        golden wire pins, encode-side contracts,
//                                     --dump-goldens, main()
//   tests/voice_gate_decode_test.cpp  decode-side contracts (VOX-1b)
//   tests/voice_gate_test_support.hpp this file: production call sequences,
//                                     deterministic PCM builders, hex helpers
//
// The production wrappers src/groupvoice/encode.cpp / decode.cpp are pinned to
// the 32-bit Win32 ABI by directsound.h struct static_asserts and therefore
// cannot run on portable CI targets. The suite drives the exact production
// call sequences instead (verified statement-for-statement by
// tests/voice_framing_source_test.cmake):
//
//   Encode_Init(bandwidth): speex_encoder_init(mode); speex_bits_init();
//       Encode_SetOptions(8000, 1): SET_SAMPLING_RATE, SET_QUALITY,
//       GET_FRAME_SIZE, SET_VAD(1), SET_DTX(1); GET_FRAME_SIZE again.
//   Encode_Sample: quality sync to sv_voiceQuality (shipped default 3),
//       speex_bits_reset, speex_encode_int, speex_bits_write.
//   Decode_Init(bandwidth): speex_decoder_init(mode); SET_ENH(1);
//       SET_SAMPLING_RATE(8000); GET_FRAME_SIZE; speex_bits_init.
//   Decode_Sample: speex_bits_read_from, speex_decode, int16 truncation.

#include <speex/speex.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace voice_gate
{
inline int fail_count = 0;

inline void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++fail_count;
    }
}

constexpr char kHexDigits[] = "0123456789abcdef";

inline std::string to_hex(const std::vector<char> &bytes)
{
    std::string out;
    out.reserve(bytes.size() * 2);
    for (char c : bytes)
    {
        unsigned char u = static_cast<unsigned char>(c);
        out.push_back(kHexDigits[u >> 4]);
        out.push_back(kHexDigits[u & 0xF]);
    }
    return out;
}

inline std::vector<char> from_hex(std::string_view hex)
{
    std::vector<char> out;
    const size_t n = hex.size();
    out.reserve(n / 2);
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back(static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Deterministic test PCM (no rand()). The LCG is fixed-point and identical on
// every target; the tone/chirp builders only use sinf on constant arguments.
// The state is unsigned so the multiply wraparound is well-defined; the low
// 32 bits of the sequence are identical to the two's-complement wrap the
// golden vectors were generated with, so the pinned bytes are unchanged.
// The in-tree Speex comfort-noise generator (misc.c speex_rand_vec /
// speex_rand) now draws from the same fixed-point LCG family instead of libc
// rand(), so no libc randomness API remains anywhere in the decode path.
// ---------------------------------------------------------------------------
inline uint32_t lcg_next(uint32_t &state)
{
    state = state * 1103515245u + 12345u;
    return static_cast<int32_t>((state >> 16) & 0x7FFFu);
}

constexpr int kFrameNb = 160;   // narrowband frame at 8 kHz
constexpr int kFrameWb = 320;   // wideband frame at 16 kHz
constexpr int kFrameUwb = 640;  // ultra-wideband frame at 32 kHz

inline void fill_silence(int16_t *pcm, int n)
{
    for (int i = 0; i < n; ++i)
        pcm[i] = 0;
}

inline void fill_sine(int16_t *pcm, int n, float freq, float rate, float scale)
{
    for (int i = 0; i < n; ++i)
        pcm[i] = static_cast<int16_t>(sinf(2.0f * 3.14159265f * freq * i / rate) * scale);
}

inline void fill_noise(int16_t *pcm, int n, uint32_t seed)
{
    for (int i = 0; i < n; ++i)
        pcm[i] = static_cast<int16_t>(lcg_next(seed) - 16384);
}

inline void fill_chirp(int16_t *pcm, int n, float rate, float scale)
{
    for (int i = 0; i < n; ++i)
    {
        float f = 200.0f + 1800.0f * static_cast<float>(i) / static_cast<float>(n);
        pcm[i] = static_cast<int16_t>(
            sinf(2.0f * 3.14159265f * f * i / rate) * scale * (1.0f - static_cast<float>(i) / n));
    }
}

inline void fill_square(int16_t *pcm, int n, int period, int16_t level)
{
    for (int i = 0; i < n; ++i)
        pcm[i] = ((i / period) % 2 == 0) ? level : static_cast<int16_t>(-level);
}

// ---------------------------------------------------------------------------
// Production encoder sequence (encode.cpp Encode_Init/Encode_SetOptions/
// Encode_Sample). SPEEX_SET_VAD and SPEEX_SET_DTX are enabled exactly like
// Encode_SetOptions does; quality mirrors the Encode_Sample sv_voiceQuality
// sync (shipped dvar default 3, registered in sv_init_mp.cpp:786).
// ---------------------------------------------------------------------------
struct Encoder
{
    void *state = nullptr;
    SpeexBits bits{};
    int frame_size = 0;
};

inline const SpeexMode *production_mode(int bandwidth_enum)
{
    // encode.cpp Encode_Init: 0 = nb, 1 = wb, 2 = uwb.
    if (bandwidth_enum == 0) return &speex_nb_mode;
    if (bandwidth_enum == 1) return &speex_wb_mode;
    return &speex_uwb_mode;
}

inline bool encoder_open(Encoder &enc, int bandwidth_enum, int samplerate, int quality)
{
    enc.state = speex_encoder_init(production_mode(bandwidth_enum));
    if (enc.state == nullptr)
        return false;
    speex_bits_init(&enc.bits);
    int yes = 1;
    speex_encoder_ctl(enc.state, SPEEX_SET_SAMPLING_RATE, &samplerate);
    speex_encoder_ctl(enc.state, SPEEX_SET_QUALITY, &quality);
    speex_encoder_ctl(enc.state, SPEEX_GET_FRAME_SIZE, &enc.frame_size);
    speex_encoder_ctl(enc.state, SPEEX_SET_VAD, &yes);
    speex_encoder_ctl(enc.state, SPEEX_SET_DTX, &yes);
    return true;
}

inline int encode_frame(Encoder &enc, const int16_t *pcm, char *out, int max_length)
{
    speex_bits_reset(&enc.bits);
    speex_encode_int(enc.state, const_cast<int16_t *>(pcm), &enc.bits);
    return speex_bits_write(&enc.bits, out, max_length);
}

inline void encoder_close(Encoder &enc)
{
    if (enc.state != nullptr)
        speex_encoder_destroy(enc.state);
    enc.state = nullptr;
    speex_bits_destroy(&enc.bits);
}

// Production decoder sequence (decode.cpp Decode_Init/Decode_Sample):
// SET_ENH(1), decoder sampling rate follows the encoder's 8 kHz default.
struct Decoder
{
    void *state = nullptr;
    SpeexBits bits{};
    int frame_size = 0;
};

inline bool decoder_open(Decoder &dec, int bandwidth_enum, int samplerate)
{
    dec.state = speex_decoder_init(production_mode(bandwidth_enum));
    if (dec.state == nullptr)
        return false;
    int enh = 1;
    speex_decoder_ctl(dec.state, SPEEX_SET_ENH, &enh);
    speex_decoder_ctl(dec.state, SPEEX_SET_SAMPLING_RATE, &samplerate);
    speex_decoder_ctl(dec.state, SPEEX_GET_FRAME_SIZE, &dec.frame_size);
    speex_bits_init(&dec.bits);
    return true;
}

// decode.cpp Decode_Sample: speex_bits_read_from + speex_decode; the wrapper
// stores float results as int16 (truncation). Returns sample count, or 0 when
// speex_decode rejects the frame.
inline int decode_frame(Decoder &dec, const char *bytes, int length, int16_t *out)
{
    // speex 1.1.9's read_from takes a non-const char* but only reads it.
    speex_bits_read_from(&dec.bits, const_cast<char *>(bytes), length);
    float pcm[4097] = {};
    if (speex_decode(dec.state, &dec.bits, pcm) != 0)
        return 0;
    for (int i = 0; i < dec.frame_size; ++i)
        out[i] = static_cast<int16_t>(pcm[i]);
    return dec.frame_size;
}

inline void decoder_close(Decoder &dec)
{
    if (dec.state != nullptr)
        speex_decoder_destroy(dec.state);
    dec.state = nullptr;
    speex_bits_destroy(&dec.bits);
}

inline std::vector<char> encode_stream(int bandwidth_enum, int samplerate, int quality,
                                       const std::vector<std::vector<int16_t>> &frames,
                                       std::vector<int> *frame_lengths = nullptr)
{
    Encoder enc;
    if (!encoder_open(enc, bandwidth_enum, samplerate, quality))
        return {};
    std::vector<char> stream;
    char buffer[2048];
    for (const auto &frame : frames)
    {
        int n = encode_frame(enc, frame.data(), buffer, sizeof(buffer));
        stream.insert(stream.end(), buffer, buffer + n);
        if (frame_lengths)
            frame_lengths->push_back(n);
    }
    encoder_close(enc);
    return stream;
}

// Production decode path: the wire framing carries each encoder frame's byte
// length explicitly (CL_WriteVoicePacket writes size + data per frame), and
// Decode_Sample is invoked once per frame against one persistent decoder.
// The gate mirrors that: decode frame-by-frame with the recorded lengths.
// Narrowband only — every golden decode reference is an nb stream.
inline std::vector<int16_t> decode_stream(const std::vector<char> &stream,
                                          const std::vector<int> &frame_lengths, Decoder &dec)
{
    std::vector<int16_t> pcm;
    int offset = 0;
    for (int len : frame_lengths)
    {
        if (offset + len > static_cast<int>(stream.size()))
            break;
        std::vector<int16_t> frame(kFrameNb, 0);
        const int samples = decode_frame(dec, stream.data() + offset, len, frame.data());
        if (samples <= 0)
            break;
        pcm.insert(pcm.end(), frame.begin(), frame.begin() + samples);
        offset += len;
    }
    return pcm;
}

// ---------------------------------------------------------------------------
// Golden vectors (VOX-1a). Provenance: generated from this in-tree Speex
// 1.1.9 build with --dump-goldens at worktree base 270dc9cf (polecat/ki-dkeb
// stage-2 branch, September 2026). Never retail, never captured traffic.
// Defined in tests/voice_gate_tests.cpp (single copy across the suite).
// ---------------------------------------------------------------------------
struct GoldenStream
{
    int bandwidth_enum;
    int samplerate;
    int quality;
    const char *hex;
};

extern const char *kGoldenNbHex;
extern const char *kGoldenWbHex;

// VOX-1b decode reference: int16 little-endian PCM of the nb stream decoded
// frame-by-frame with the recorded per-frame lengths.
extern const char *kGoldenNbDecodeHex;

// Golden streams (VOX-1a): encoder bitstreams byte-for-byte, generated by
// --dump-goldens on this in-tree Speex 1.1.9 build. See the provenance note
// above. Ultra-wideband golden vectors are intentionally absent: see the
// FINDING note in tests/voice_gate_tests.cpp (the uwb encode path faults in
// this snapshot and production never selects it).

inline std::vector<std::vector<int16_t>> nb_stream_frames()
{
    // Six mixed-content frames: tone, noise, silence (DTX path), chirp,
    // square, tone again — the encoder runs sequentially so later frames
    // reflect encoder state (VOX-1a pins the whole stream).
    std::vector<std::vector<int16_t>> frames;
    frames.emplace_back(kFrameNb, 0);
    fill_sine(frames.back().data(), kFrameNb, 440.0f, 8000.0f, 12000.0f);
    frames.emplace_back(kFrameNb, 0);
    fill_noise(frames.back().data(), kFrameNb, 0x1234);
    frames.emplace_back(kFrameNb, 0);
    fill_silence(frames.back().data(), kFrameNb);
    frames.emplace_back(kFrameNb, 0);
    fill_chirp(frames.back().data(), kFrameNb, 8000.0f, 9000.0f);
    frames.emplace_back(kFrameNb, 0);
    fill_square(frames.back().data(), kFrameNb, 8, 20000);
    frames.emplace_back(kFrameNb, 0);
    fill_sine(frames.back().data(), kFrameNb, 1000.0f, 8000.0f, 6000.0f);
    return frames;
}

inline std::vector<std::vector<int16_t>> wb_stream_frames()
{
    std::vector<std::vector<int16_t>> frames;
    frames.emplace_back(kFrameWb, 0);
    fill_sine(frames.back().data(), kFrameWb, 440.0f, 16000.0f, 12000.0f);
    frames.emplace_back(kFrameWb, 0);
    fill_noise(frames.back().data(), kFrameWb, 0x1234);
    return frames;
}

// Narrowband production path: quality starts at the directsound.h default 1
// and Encode_Sample syncs it to the shipped sv_voiceQuality default 3 before
// the first frame. Wideband/ultra-wideband are not shipped production modes;
// their vectors use quality 3 directly at the canonical rates.
constexpr int kProductionSamplerate = 8000; // directsound.h g_encoder_samplerate
constexpr int kShippedVoiceQuality = 3;     // sv_init_mp.cpp:786 default

// Decode-side contracts, defined in tests/voice_gate_decode_test.cpp and
// invoked from the suite's main().
void test_decode_golden();
void test_corrupt_decode_rejected();
void test_round_trip_lossy_not_bitexact();
} // namespace voice_gate
