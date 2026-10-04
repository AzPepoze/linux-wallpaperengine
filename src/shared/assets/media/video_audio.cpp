#include "video_audio.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "shared/assets/media/media_source.h"
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
constexpr uint32_t kOutputSampleRate = 48000;
constexpr uint32_t kOutputChannels = 2;

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

    // Serialises decoder state between the feeder thread and the render thread (restart/setRate).
    std::mutex decode_mutex;
    std::thread feeder;
    std::atomic<bool> feeder_stop{false};
    std::atomic<bool> feeding{false};

    wallpaper_engine::MediaIo io;

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
        io.close();
    }
};

VideoAudioStream::~VideoAudioStream() {
    stopFeeder();
}

std::unique_ptr<VideoAudioStream> VideoAudioStream::open(const char* path) {
    if (!path) return nullptr;
    auto stream = std::unique_ptr<VideoAudioStream>(new VideoAudioStream());
    stream->impl = std::make_unique<Impl>();

    if (!stream->impl->io.open(wallpaper_engine::openMediaSource(path))) return nullptr;
    stream->impl->format = avformat_alloc_context();
    if (!stream->impl->format) return nullptr;
    stream->impl->format->pb = stream->impl->io.context();
    stream->impl->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&stream->impl->format, nullptr, nullptr, nullptr) < 0) return nullptr;

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
    std::lock_guard<std::mutex> lock(impl->decode_mutex);
    if (impl->initResampler(resampledAudioRate(kOutputSampleRate, rate))) return;
    LOG_TAG_W(TAG, "Failed to apply playback rate to audio; disabling it");
    impl->has_audio = false;
}

void VideoAudioStream::pump(AudioEngine::StreamHandle stream, uint32_t target_queued_frames) {
    if (!impl || !impl->has_audio || stream == AudioEngine::kInvalidStream) return;
    std::lock_guard<std::mutex> lock(impl->decode_mutex);
    if (impl->drained) return;
    if (impl->feeder.joinable() && !impl->feeding.load()) return;  // hidden

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
    std::lock_guard<std::mutex> lock(impl->decode_mutex);
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

void VideoAudioStream::startFeeder(AudioEngine::StreamHandle stream, uint32_t target_queued_frames) {
    if (!impl || !impl->has_audio || stream == AudioEngine::kInvalidStream || impl->feeder.joinable()) return;
    impl->feeder_stop = false;
    impl->feeding = true;
    impl->feeder = std::thread([this, stream, target_queued_frames] {
        while (!impl->feeder_stop.load(std::memory_order_relaxed)) {
            if (impl->feeding.load(std::memory_order_relaxed)) pump(stream, target_queued_frames);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
}

void VideoAudioStream::stopFeeder() {
    if (!impl || !impl->feeder.joinable()) return;
    impl->feeder_stop = true;
    impl->feeder.join();
}

void VideoAudioStream::setFeeding(AudioEngine::StreamHandle stream, bool feeding) {
    if (!impl || impl->feeding.exchange(feeding) == feeding) return;
    if (feeding || stream == AudioEngine::kInvalidStream) return;
    // Hold the decode lock so an in-flight pump cannot refill what we drop.
    std::lock_guard<std::mutex> lock(impl->decode_mutex);
    AudioEngine::instance().clearStream(stream);
}
