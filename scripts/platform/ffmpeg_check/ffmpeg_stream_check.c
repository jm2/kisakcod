/*
 * Checks that the runtime's FFmpeg reads what the engine streams (#942):
 * the game's sound/**.wav are 16-bit PCM and its music is MP3. Each sample
 * goes through the path snd_openal.cpp's stream decoder takes: a custom AVIO
 * over the bytes, avformat_open_input with no file name, the best audio
 * stream, its decoder, and one decoded frame. Exits non-zero, naming the
 * format, if any step fails.
 */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const uint8_t *data;
    int64_t size;
    int64_t pos;
} Memory;

static int ReadMemory(void *opaque, uint8_t *buf, int size)
{
    Memory *m = opaque;
    int64_t left = m->size - m->pos;
    if (left <= 0)
        return AVERROR_EOF;
    if (size > left)
        size = (int)left;
    for (int i = 0; i < size; ++i)
        buf[i] = m->data[m->pos + i];
    m->pos += size;
    return size;
}

static int64_t SeekMemory(void *opaque, int64_t offset, int whence)
{
    Memory *m = opaque;
    if (whence == AVSEEK_SIZE)
        return m->size;
    int64_t base = (whence & 3) == SEEK_CUR ? m->pos : (whence & 3) == SEEK_END ? m->size : 0;
    if (base + offset < 0 || base + offset > m->size)
        return -1;
    m->pos = base + offset;
    return m->pos;
}

static void Put16(uint8_t *p, unsigned v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static void Put32(uint8_t *p, unsigned v) { Put16(p, v & 0xffff); Put16(p + 2, v >> 16); }
static void PutTag(uint8_t *p, const char tag[4]) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)tag[i]; }

enum { WAV_SAMPLES = 4410, WAV_SIZE = 44 + WAV_SAMPLES * 2, MP3_FRAMES = 30, MP3_FRAME = 417 };

/* A mono 22050 Hz 16-bit PCM WAV of 0.2 s of silence; out holds WAV_SIZE. */
static int64_t MakeWav(uint8_t *out)
{
    const unsigned bytes = WAV_SAMPLES * 2;
    PutTag(out, "RIFF"); Put32(out + 4, 36 + bytes); PutTag(out + 8, "WAVE"); PutTag(out + 12, "fmt ");
    Put32(out + 16, 16); Put16(out + 20, 1); Put16(out + 22, 1); Put32(out + 24, 22050);
    Put32(out + 28, 22050 * 2); Put16(out + 32, 2); Put16(out + 34, 16);
    PutTag(out + 36, "data"); Put32(out + 40, bytes);
    memset(out + 44, 0, bytes);
    return WAV_SIZE;
}

/* Silent MPEG-1 Layer III frames: 128 kbit/s, 44.1 kHz, mono, no CRC, 417
 * bytes each; out holds MP3_FRAMES * MP3_FRAME. All-zero side info and main
 * data decode to silence. */
static int64_t MakeMp3(uint8_t *out)
{
    memset(out, 0, (size_t)MP3_FRAMES * MP3_FRAME);
    for (int i = 0; i < MP3_FRAMES; ++i)
    {
        uint8_t *f = out + i * MP3_FRAME;
        f[0] = 0xff; f[1] = 0xfb; f[2] = 0x90; f[3] = 0xc0;
    }
    return (int64_t)MP3_FRAMES * MP3_FRAME;
}

static int Check(const char *what, const uint8_t *data, int64_t size)
{
    Memory m = {data, size, 0};
    const int bufferSize = 32 * 1024;
    uint8_t *buffer = av_malloc(bufferSize);
    AVIOContext *io = buffer ? avio_alloc_context(buffer, bufferSize, 0, &m, ReadMemory, NULL, SeekMemory) : NULL;
    AVFormatContext *format = io ? avformat_alloc_context() : NULL;
    AVCodecContext *codec = NULL;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const AVCodec *decoder = NULL;
    const char *failed = NULL;
    int decoded = 0;

    if (!format || !packet || !frame)
        failed = "out of memory";
    else
    {
        format->pb = io;
        int stream = -1;
        if (avformat_open_input(&format, NULL, NULL, NULL) < 0)
            failed = "no demuxer recognises it";
        else if (avformat_find_stream_info(format, NULL) < 0)
            failed = "can't read its stream info";
        else if ((stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0)) < 0 || !decoder)
            failed = "no audio stream with a decoder";
        else if (!(codec = avcodec_alloc_context3(decoder))
                 || avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) < 0
                 || avcodec_open2(codec, decoder, NULL) < 0)
            failed = "can't open its decoder";
        else
        {
            while (!decoded && av_read_frame(format, packet) >= 0)
            {
                if (packet->stream_index == stream && avcodec_send_packet(codec, packet) >= 0)
                    decoded = avcodec_receive_frame(codec, frame) >= 0;
                av_packet_unref(packet);
            }
            if (!decoded)
                failed = "decoded no audio";
        }
    }
    if (failed)
        fprintf(stderr, "ffmpeg_stream_check: %s: %s\n", what, failed);
    else
        printf("ffmpeg_stream_check: %s: %s via %s, %d Hz\n", what, format->iformat->name, decoder->name,
               codec->sample_rate);

    avcodec_free_context(&codec);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_close_input(&format);
    if (io)
        av_freep(&io->buffer);
    avio_context_free(&io);
    return failed ? 1 : 0;
}

int main(void)
{
    uint8_t *wav = av_malloc(WAV_SIZE), *mp3 = av_malloc((size_t)MP3_FRAMES * MP3_FRAME);
    if (!wav || !mp3)
    {
        fprintf(stderr, "ffmpeg_stream_check: out of memory\n");
        return 1;
    }
    int failures = Check("16-bit PCM WAV", wav, MakeWav(wav));
    failures += Check("MP3", mp3, MakeMp3(mp3));
    av_free(wav);
    av_free(mp3);
    return failures ? 1 : 0;
}
