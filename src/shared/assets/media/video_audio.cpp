#include "video_audio.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "shared/assets/media/video_rate.h"
#include "shared/core/logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#define TAG "VIDEO_AUDIO"

namespace {
constexpr int kIoBufferSize = 4096;
constexpr uint32_t kOutputSampleRate = 48000;
constexpr uint32_t kOutputChannels = 2;

struct MemoryInput {
    std::vector<uint8_t> bytes;
    size_t position = 0;
};

int readMemory(void* opaque, uint8_t* buf, int buf_size) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (!input || input->position >= input->bytes.size()) return AVERROR_EOF;
    const size_t available = input->bytes.size() - input->position;
    const size_t to_read = std::min((size_t)buf_size, available);
    memcpy(buf, input->bytes.data() + input->position, to_read);
    input->position += to_read;
    return (int)to_read;
}

int64_t seekMemory(void* opaque, int64_t offset, int whence) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (!input) return -1;
    int64_t position = 0;
    if (whence == SEEK_SET)
        position = offset;
    else if (whence == SEEK_CUR)
        position = (int64_t)input->position + offset;
    else if (whence == SEEK_END)
        position = (int64_t)input->bytes.size() + offset;
    else if (whence == AVSEEK_SIZE)
        return (int64_t)input->bytes.size();
    else
        return -1;
    if (position < 0 || position > (int64_t)input->bytes.size()) return -1;
    input->position = (size_t)position;
    return position;
}

bool readEmbeddedMp4(const char* path, std::vector<uint8_t>& mp4) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 8) {
        fclose(file);
        return false;
    }
    std::vector<uint8_t> data((size_t)size);
    const bool success = fread(data.data(), 1, data.size(), file) == data.size();
    fclose(file);
    if (!success) return false;
    static constexpr uint8_t marker[] = {'f', 't', 'y', 'p'};
    const auto ftyp = std::search(data.begin(), data.end(), std::begin(marker), std::end(marker));
    if (ftyp == data.end() || ftyp - data.begin() < 4) return false;
    const size_t offset = (size_t)(ftyp - data.begin() - 4);
    mp4.assign(data.begin() + offset, data.end());
    return true;
}

}  // namespace

struct VideoAudioStream::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr = nullptr;
    int stream_index = -1;
    bool has_audio = false;
    bool draining = false;
    bool drained = false;
    bool logged_started = false;
    std::vector<float> converted;

    MemoryInput memory_input;
    AVIOContext* io = nullptr;
    uint8_t* io_buffer = nullptr;

    bool initResampler(uint32_t output_rate) {
        AVChannelLayout out_layout;
        av_channel_layout_default(&out_layout, kOutputChannels);
        swr_free(&swr);
        return swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, (int)output_rate, &codec->ch_layout,
                                   codec->sample_fmt, codec->sample_rate, 0, nullptr) >= 0 &&
               swr_init(swr) >= 0;
    }

    ~Impl() {
        if (swr) swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) avio_context_free(&io);
    }
};

VideoAudioStream::~VideoAudioStream() = default;

std::unique_ptr<VideoAudioStream> VideoAudioStream::open(const char* path) {
    if (!path) return nullptr;
    auto stream = std::unique_ptr<VideoAudioStream>(new VideoAudioStream());
    stream->impl = std::make_unique<Impl>();

    std::vector<uint8_t> mp4_bytes;
    const bool is_embedded = readEmbeddedMp4(path, mp4_bytes);

    if (is_embedded) {
        stream->impl->memory_input.bytes = std::move(mp4_bytes);
        stream->impl->io_buffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
        stream->impl->io = avio_alloc_context(stream->impl->io_buffer, kIoBufferSize, 0, &stream->impl->memory_input,
                                              readMemory, nullptr, seekMemory);
        if (!stream->impl->io) return nullptr;
        stream->impl->format = avformat_alloc_context();
        if (!stream->impl->format) return nullptr;
        stream->impl->format->pb = stream->impl->io;
        if (avformat_open_input(&stream->impl->format, "memory", nullptr, nullptr) < 0) return nullptr;
    } else {
        if (avformat_open_input(&stream->impl->format, path, nullptr, nullptr) < 0) return nullptr;
    }

    if (avformat_find_stream_info(stream->impl->format, nullptr) < 0) return nullptr;
    const AVCodec* decoder = nullptr;
    const int index = av_find_best_stream(stream->impl->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (index < 0 || !decoder) {
        LOG_TAG_I(TAG, "Video has no audio stream: %s", path);
        return stream;  // Valid object with hasAudio() == false.
    }

    AVStream* av_stream = stream->impl->format->streams[index];
    stream->impl->codec = avcodec_alloc_context3(decoder);
    if (!stream->impl->codec || avcodec_parameters_to_context(stream->impl->codec, av_stream->codecpar) < 0 ||
        avcodec_open2(stream->impl->codec, decoder, nullptr) < 0) {
        LOG_TAG_W(TAG, "Failed to open audio decoder for: %s", path);
        return stream;
    }

    if (stream->impl->codec->ch_layout.nb_channels == 0) {
        av_channel_layout_default(&stream->impl->codec->ch_layout, stream->impl->codec->ch_layout.nb_channels > 0
                                                                       ? stream->impl->codec->ch_layout.nb_channels
                                                                       : 1);
    }
    if (!stream->impl->initResampler(kOutputSampleRate)) {
        LOG_TAG_W(TAG, "Failed to set up audio resampler for: %s", path);
        return stream;
    }

    stream->impl->stream_index = index;
    stream->impl->packet = av_packet_alloc();
    stream->impl->frame = av_frame_alloc();
    if (!stream->impl->packet || !stream->impl->frame) return stream;

    stream->impl->has_audio = true;
    LOG_TAG_I(TAG, "Video audio stream opened: %s (%d Hz, %d ch, %s)", path, stream->impl->codec->sample_rate,
              stream->impl->codec->ch_layout.nb_channels, avcodec_get_name(stream->impl->codec->codec_id));
    return stream;
}

bool VideoAudioStream::hasAudio() const {
    return impl && impl->has_audio;
}

void VideoAudioStream::setRate(float rate) {
    if (!impl || !impl->has_audio) return;
    if (impl->initResampler(resampledAudioRate(kOutputSampleRate, rate))) return;
    LOG_TAG_W(TAG, "Failed to apply playback rate to audio; disabling it");
    impl->has_audio = false;
}

void VideoAudioStream::pump(AudioEngine::StreamHandle stream, uint32_t target_queued_frames) {
    if (!impl || !impl->has_audio || stream == AudioEngine::kInvalidStream) return;
    if (impl->drained) return;

    AudioEngine& engine = AudioEngine::instance();
    int guard = 0;
    while (engine.streamQueuedFrames(stream) < target_queued_frames && ++guard < 20000) {
        const int received = avcodec_receive_frame(impl->codec, impl->frame);
        if (received == 0) {
            const int out_samples = swr_get_out_samples(impl->swr, impl->frame->nb_samples);
            if (out_samples > 0) {
                impl->converted.resize((size_t)out_samples * kOutputChannels);
                uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(impl->converted.data())};
                const int converted = swr_convert(impl->swr, out_planes, out_samples,
                                                  (const uint8_t**)impl->frame->extended_data, impl->frame->nb_samples);
                if (converted > 0) engine.pushStream(stream, impl->converted.data(), (uint32_t)converted);
                if (!impl->logged_started && converted > 0) {
                    impl->logged_started = true;
                    LOG_TAG_I(TAG, "Video audio playback started");
                }
            }
            av_frame_unref(impl->frame);
            continue;
        }
        if (received == AVERROR(EAGAIN)) {
            const int read = av_read_frame(impl->format, impl->packet);
            if (read < 0) {
                if (!impl->draining) {
                    avcodec_send_packet(impl->codec, nullptr);
                    impl->draining = true;
                } else {
                    impl->drained = true;
                    break;
                }
                continue;
            }
            if (impl->packet->stream_index == impl->stream_index) avcodec_send_packet(impl->codec, impl->packet);
            av_packet_unref(impl->packet);
            continue;
        }
        impl->drained = true;
        break;
    }
}

void VideoAudioStream::restart(AudioEngine::StreamHandle stream) {
    if (!impl || !impl->has_audio) return;
    if (av_seek_frame(impl->format, impl->stream_index, 0, AVSEEK_FLAG_BACKWARD) < 0) {
        LOG_TAG_W(TAG, "Failed to seek video audio to start");
    }
    avcodec_flush_buffers(impl->codec);
    if (impl->swr) {
        swr_close(impl->swr);
        swr_init(impl->swr);
    }
    impl->draining = false;
    impl->drained = false;
    AudioEngine::instance().clearStream(stream);
    LOG_TAG_D(TAG, "Video audio resynced to video loop");
}
