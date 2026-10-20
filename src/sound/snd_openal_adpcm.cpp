#include "snd_openal_adpcm.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace snd_openal
{
namespace
{
// The smallest block holds each channel's 4-byte header; libavcodec caps a
// packet's block_align well below this bound.
constexpr std::uint32_t kMaxBlockSize = 0x10000;

bool HeaderFits(const void *data, std::uint32_t dataLen, std::uint32_t blockSize, int channels, std::uint32_t rate)
{
    const bool channelsOk = channels == 1 || channels == 2;
    return data && channelsOk && rate > 0 && blockSize >= 4u * static_cast<std::uint32_t>(channels)
        && blockSize <= kMaxBlockSize && blockSize <= dataLen;
}

// Appends the frame's planar 16-bit samples, interleaved; false on any other layout.
bool AppendFrame(const AVFrame *frame, int channels, std::vector<std::int16_t> &out)
{
    if (frame->format != AV_SAMPLE_FMT_S16P || frame->ch_layout.nb_channels != channels)
        return false;
    for (int i = 0; i < frame->nb_samples; ++i)
        for (int c = 0; c < channels; ++c)
            out.push_back(reinterpret_cast<const std::int16_t *>(frame->extended_data[c])[i]);
    return true;
}
} // namespace

bool DecodeImaAdpcm(const void *data, std::uint32_t dataLen, std::uint32_t blockSize, int channels,
                    std::uint32_t rate, std::vector<std::int16_t> &out)
{
    out.clear();
    if (!HeaderFits(data, dataLen, blockSize, channels, rate))
        return false;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_IMA_WAV);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool ok = ctx && packet && frame;
    if (ok)
    {
        ctx->sample_rate = static_cast<int>(rate);
        ctx->block_align = static_cast<int>(blockSize);
        ctx->bits_per_coded_sample = 4;
        av_channel_layout_default(&ctx->ch_layout, channels);
        ok = avcodec_open2(ctx, codec, nullptr) >= 0;
    }
    const std::uint8_t *bytes = static_cast<const std::uint8_t *>(data);
    for (std::uint32_t offset = 0; ok && blockSize <= dataLen - offset; offset += blockSize)
    {
        packet->data = const_cast<std::uint8_t *>(bytes + offset);
        packet->size = static_cast<int>(blockSize);
        ok = avcodec_send_packet(ctx, packet) >= 0;
        while (ok && avcodec_receive_frame(ctx, frame) == 0)
            ok = AppendFrame(frame, channels, out);
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    if (!ok)
        out.clear();
    return ok && !out.empty();
}
} // namespace snd_openal
