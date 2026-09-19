// Voice codec parity gates — decode-side contracts (VOX-1b,
// docs/AUDIO_VOICE_CINEMATIC_PARITY_GATES.md Section 4.2) against the
// in-tree Speex 1.1.9 build that ships with the game
// (src/groupvoice/speex/*.c, public headers deps/speex/).
//
// The shared production call sequences, PCM builders, and golden vector
// declarations live in tests/voice_gate_test_support.hpp; the golden wire
// pins and the suite entry point live in tests/voice_gate_tests.cpp (the
// suite is split across translation units to keep each analyzed file well
// under the static-analysis file-size limit).

#include "voice_gate_test_support.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <vector>

namespace voice_gate
{
// VOX-1b: decode the pinned nb golden stream and compare against the pinned
// decoder PCM within kDecodeTolerance per sample.
constexpr int kDecodeTolerance = 8; // recorded tolerance (measured max diff on
                                    // the generation build was 0 across the
                                    // stream; 8 absorbs residual
                                    // cross-platform fixed-point drift)
// Comfort-noise frames decode from the in-tree Speex noise synthesis (the
// null-submode comfort-noise branch and the nb submode-1 vocoder noise
// excitation via noise_codebook_unquant), which now draws from the
// deterministic fixed-point LCG in misc.c through per-decoder state — no
// libc randomness API remains in the decode path (CWE-327 repair). The gate
// only bounds their amplitude, so the bound must sit above any legal
// comfort-noise level while still catching runaway output. The pinned
// stream's single DTX-marked submode-1 silence frame peaks at 13775 on the
// generation build (the encoder transmits its noise-floor gain estimate in
// the frame's 5-bit excitation-gain field, so the decoder's comfort noise
// rides just under that scale); the bound sits above the recorded peak with
// headroom while staying below the 18000 speech-transient limit the
// round-trip gate allows.
constexpr int kDtxAmplitudeBound = 16000;

// Classify one transmitted frame as receiver-local comfort noise (true) or
// a deterministic, sample-pinnable vocoded frame (false).
//
// Wire classification mirrors the in-tree Speex nb codec (nb_celp.c):
//   - submode 0 (null submode: a first byte whose nb submode nibble is 0)
//     transmits no payload; the decoder synthesizes comfort noise locally.
//   - submode 1 is a 43-bit frame (modes.c nb_submode1 bits_per_frame,
//     padded to 6 bytes): 1 wideband + 4 submode + 3x6 LSP + 7 pitch +
//     4 forced pitch gain + 5 excitation gain + a trailing 4-bit DTX
//     marker. nb_encode packs 15 into that marker exactly when it
//     suppresses a silence frame under VAD+DTX (st->dtx_count), and
//     nb_decode enables DTX only for the marker value 15. The suite's
//     silence frame arrives as 0x0e... — a nonzero submode nibble — so the
//     marker, not the nibble, decides.
//   - any other submode is a deterministic vocoded frame.
bool is_dtx_frame(const std::vector<char> &stream, int frame_offset)
{
    if (frame_offset < 0 || frame_offset >= static_cast<int>(stream.size()))
        return true; // nothing transmitted: not sample-pinnable
    const unsigned int first =
        static_cast<unsigned char>(stream[frame_offset]);
    const unsigned int submode = (first & 0x78) >> 3;
    if (submode == 0)
        return true; // null submode: comfort noise, no payload
    if (submode != 1)
        return false; // deterministic vocoded frame
    // Submode 1: the DTX marker is the last 4 bits of the 43-bit frame
    // before byte padding — frame bit 39 is the LSB of byte 4 and frame
    // bits 40-42 are the top 3 bits of byte 5 (its low 5 bits are
    // zero padding). DTX is signaled by the marker value 15 (0b1111).
    constexpr int kSubmode1BitsPerFrame = 43; // modes.c nb_submode1
    constexpr int kSubmode1FrameBytes = (kSubmode1BitsPerFrame + 7) / 8;
    if (frame_offset + kSubmode1FrameBytes > static_cast<int>(stream.size()))
        return true; // truncated frame: not sample-pinnable
    const unsigned int marker =
        ((static_cast<unsigned int>(static_cast<unsigned char>(
              stream[frame_offset + 4])) &
          1u)
         << 3) |
        (static_cast<unsigned int>(static_cast<unsigned char>(
             stream[frame_offset + 5])) >>
         5);
    return marker == 15;
}

// Frame-class-aware comparison against the pinned decode reference. DTX
// frames decode to comfort noise — receiver-local synthesis that never
// appears on the wire, so their content is validated by the amplitude bound
// rather than sample-pinned; every deterministic frame is pinned against the
// recorded reference sample-for-sample. Returns the max abs diff over
// deterministic frames; reports the DTX frame count through *dtx_frames_out.
// Per-frame classification is is_dtx_frame() above.
int compare_decoded_to_reference(const std::vector<int16_t> &decoded,
                                 const std::vector<char> &reference,
                                 const std::vector<char> &stream,
                                 const std::vector<int> &lengths,
                                 int *dtx_frames_out)
{
    int max_diff = 0;
    int dtx_frames = 0;
    int offset = 0;
    for (size_t f = 0; f < lengths.size(); ++f)
    {
        const bool dtx = is_dtx_frame(stream, offset);
        for (int i = 0; i < kFrameNb; ++i)
        {
            const size_t s = f * static_cast<size_t>(kFrameNb) + static_cast<size_t>(i);
            const int16_t expected = static_cast<int16_t>(
                static_cast<unsigned char>(reference[2 * s]) |
                (static_cast<int>(static_cast<unsigned char>(reference[2 * s + 1])) << 8));
            const int diff = std::abs(static_cast<int>(decoded[s]) - static_cast<int>(expected));
            if (dtx)
            {
                check(static_cast<int>(std::abs(static_cast<int>(decoded[s]))) <= kDtxAmplitudeBound,
                      "VOX-1b: DTX comfort-noise frame stays within the amplitude bound");
            }
            else if (diff > max_diff)
            {
                max_diff = diff;
            }
        }
        if (dtx)
            ++dtx_frames;
        offset += lengths[f];
    }
    *dtx_frames_out = dtx_frames;
    return max_diff;
}

void test_decode_golden()
{
    const std::vector<char> stream = from_hex(kGoldenNbHex);
    const std::vector<char> reference = from_hex(kGoldenNbDecodeHex);
    check(stream.size() % 2 == 0 && !stream.empty(), "golden nb stream is well-formed");

    // Re-encode to recover the per-frame byte lengths the wire framing would
    // carry; encoding is byte-identical to the golden stream (VOX-1a), so the
    // lengths match the golden frames exactly.
    std::vector<int> lengths;
    const std::vector<char> reencoded = encode_stream(
        0, kProductionSamplerate, kShippedVoiceQuality, nb_stream_frames(), &lengths);
    check(to_hex(reencoded) == kGoldenNbHex, "re-encoded stream matches the golden hex");
    check(static_cast<int>(stream.size()) ==
              std::accumulate(lengths.begin(), lengths.end(), 0),
          "frame lengths cover the golden stream exactly");

    // The in-tree Speex decoder synthesizes noise-driven PCM (vocoder noise
    // excitation, DTX comfort noise, loss concealment) from the deterministic
    // fixed-point LCG in misc.c through per-decoder state seeded at decoder
    // init (formerly libc rand(), replaced by the CWE-327 repair), so the
    // decoded stream is reproducible with no seeding of any kind and is
    // independent of decoder interleaving.
    Decoder dec;
    check(decoder_open(dec, 0, kProductionSamplerate), "decoder opens (nb)");
    check(dec.frame_size == kFrameNb, "decoder frame geometry matches nb");
    const std::vector<int16_t> decoded = decode_stream(stream, lengths, dec);
    decoder_close(dec);
    // decode_stream returns a short vector when a frame is truncated or a
    // decode fails; stop before the reference comparison, which indexes the
    // decoded samples frame-for-frame.
    const bool decoded_all =
        decoded.size() == lengths.size() * static_cast<size_t>(kFrameNb);
    check(decoded_all, "every golden nb frame decodes");
    if (!decoded_all)
        return;

    const bool reference_matches =
        decoded.size() * 2 == reference.size();
    check(reference_matches,
          "VOX-1b: decoded sample count matches the pinned reference");
    if (!reference_matches)
        return;

    int dtx_frames = 0;
    const int max_diff =
        compare_decoded_to_reference(decoded, reference, stream, lengths, &dtx_frames);
    check(max_diff <= kDecodeTolerance,
          "VOX-1b: decoded PCM within the recorded tolerance");
    // The stream contains a VAD-suppressed silence frame transmitted as a
    // submode-1 DTX-marked frame; if classification ever regresses to the
    // nibble-only form, the comfort-noise amplitude bound becomes a dead
    // assertion path — pin the exercised path itself.
    check(dtx_frames > 0,
          "VOX-1b: at least one DTX frame exercised the comfort-noise amplitude bound");
    std::fprintf(stderr,
                 "note: decode max diff %d (tolerance %d), %d of %zu frames are DTX (bounded, not pinned)\n",
                 max_diff, kDecodeTolerance, dtx_frames, lengths.size());

    // Determinism: decoding is a deterministic function of the bitstream
    // alone — comfort noise draws from the fixed-point LCG, not from any
    // global PRNG state, so repeated passes are exactly comparable.
    Decoder dec2;
    check(decoder_open(dec2, 0, kProductionSamplerate), "second decoder opens (nb)");
    const std::vector<int16_t> decoded2 = decode_stream(stream, lengths, dec2);
    decoder_close(dec2);
    check(decoded == decoded2,
          "VOX-1b: decoding is deterministic without any global PRNG seed");
}

void test_corrupt_decode_rejected()
{
    // Speex frames carry no checksum: flipped wire bits either fail
    // speex_decode (production Decode_Sample maps that to 0 bytes) or decode
    // deterministically to garbage that production forwards as voice. The
    // gate pins the corruption PATH: bounded, no crash, and deterministic.
    // Each attempt uses a fresh decoder because decoder memory is stateful
    // across frames. The first golden frame is corrupted in place (its true
    // wire length comes from a deterministic re-encode) and fed to a decoder.
    std::vector<int> lengths;
    const std::vector<char> encoded =
        encode_stream(0, kProductionSamplerate, kShippedVoiceQuality, nb_stream_frames(), &lengths);
    const bool can_corrupt =
        !lengths.empty() && lengths[0] >= 2 &&
        encoded.size() >= static_cast<size_t>(lengths[0]);
    check(can_corrupt,
          "golden nb stream is long enough to corrupt");
    if (!can_corrupt)
        return;
    std::vector<char> corrupt(encoded.begin(), encoded.begin() + lengths[0]);
    for (int i = 0; i < static_cast<int>(corrupt.size()); ++i)
        corrupt[i] = static_cast<char>(corrupt[i] ^ 0xA5);

    std::vector<int16_t> first(kFrameNb, -1);
    std::vector<int16_t> second(kFrameNb, -1);
    const int len = static_cast<int>(corrupt.size());
    Decoder dec_a;
    decoder_open(dec_a, 0, kProductionSamplerate);
    const int samples_a = decode_frame(dec_a, corrupt.data(), len, first.data());
    decoder_close(dec_a);
    Decoder dec_b;
    decoder_open(dec_b, 0, kProductionSamplerate);
    const int samples_b = decode_frame(dec_b, corrupt.data(), len, second.data());
    decoder_close(dec_b);
    check(samples_a == samples_b, "corrupt decode is deterministic in sample count");
    check(first == second, "corrupt decode is deterministic in output PCM");
}

void test_round_trip_lossy_not_bitexact()
{
    // Section 5: a lossy codec does not reproduce source PCM bit-exactly.
    // The decoded stream must still track the input shape once aligned.
    //
    // The narrowband encoder runs a synthesis-analysis lookahead: the decoded
    // stream is delayed by kEncoderLookahead samples relative to the input
    // (upstream speex's own testenc measures the same delay with
    // SPEEX_GET_LOOKAHEAD and skips it before scoring SNR). Two identical
    // sine frames are encoded and both decoded frames are concatenated, then
    // compared to the input stream offset by the lookahead.
    const int kEncoderLookahead = 80; // nb encoder SPEEX_GET_LOOKAHEAD
    std::vector<int16_t> frame(kFrameNb, 0);
    fill_sine(frame.data(), kFrameNb, 440, 8000, 12000);
    std::vector<int> lengths;
    const std::vector<char> bytes = encode_stream(0, kProductionSamplerate, kShippedVoiceQuality,
                                                  {frame, frame}, &lengths);
    check(lengths.size() == 2 && bytes.size() >= 2, "round-trip frames encoded");

    Decoder dec;
    decoder_open(dec, 0, kProductionSamplerate);
    const std::vector<int16_t> decoded = decode_stream(bytes, lengths, dec);
    decoder_close(dec);
    check(decoded.size() == 2 * static_cast<size_t>(kFrameNb), "both frames decoded");
    // decode_stream stops early on a failed decode; the alignment window
    // below indexes decoded[sample] and divides by the compared count, so a
    // short decode must stop this test before those run.
    if (decoded.size() != 2 * static_cast<size_t>(kFrameNb))
        return;

    int max_diff = 0;
    int64_t sum_abs = 0;
    int compared = 0;
    // Skip the first decoded frame entirely: it carries the decoder's
    // cold-start warmup (LPC/pitch state ramp) on top of the 80-sample
    // algorithmic delay. Frame 2's decode is steady state.
    const int kSteadyStateStart = kFrameNb;
    for (int i = kSteadyStateStart; i < static_cast<int>(decoded.size()); ++i)
    {
        const int src = i - kEncoderLookahead; // input-stream index
        const int16_t expect = frame[src % kFrameNb];
        const int diff = std::abs(static_cast<int>(decoded[i]) - static_cast<int>(expect));
        if (diff > max_diff)
            max_diff = diff;
        sum_abs += diff;
        ++compared;
    }
    if (compared == 0)
    {
        check(false, "alignment window is empty");
        return;
    }
    std::fprintf(stderr,
                 "note: aligned round-trip max diff %d, mean abs diff %lld over %d samples at input scale 12000\n",
                 max_diff, static_cast<long long>(sum_abs / compared), compared);
    check(compared == kFrameNb, "alignment window covers the decoded tail");
    check(max_diff > 0, "round-trip is lossy (not bit-exact against the source PCM)");
    // Recorded sanity bounds measured on the generation build (q3, 8 kHz,
    // 440 Hz sine at scale 12000, 80-sample lookahead alignment, first decoded
    // frame excluded as cold-start warmup): mean abs diff stays in the low
    // thousands; the max diff is a ~4-sample transient at the interframe
    // boundary (decoded sample 240 = the first sample of frame 2's encoded
    // content), characteristic of q3 LSP/LTP re-quantization at frame seams.
    // A change that degrades steady-state quality or widens the seam transient
    // pushes past these recorded values.
    check(max_diff <= 18000 && sum_abs / compared <= 3000,
          "round-trip tracks the source within the loose sanity bound");
}
} // namespace voice_gate
