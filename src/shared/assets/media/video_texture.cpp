#include "video_texture.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>

#include "media_source.h"
#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_zero_copy.h"
#include "video_decoder.h"
#include "video_import_cache.h"
#include "video_scheduler.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#define TAG "VIDEO"

namespace wallpaper_engine {

struct VideoTexture::Impl {
    VideoDecoder hw_decoder;
    VideoImportCache import_cache;
    VideoScheduler scheduler;
    ZeroCopyMetrics zero_copy;
    PlaybackStats stats;
    PerformanceTiming perf;

    bool is_hw_active = false;
    bool zero_copy_disabled = false;
    bool is_playing = true;
    bool is_paused = false;
    uint32_t loop_count = 0;
    AVFrame* current_frame = nullptr;

    MediaIo io;
    AVFormatContext* sw_format = nullptr;
    AVCodecContext* sw_codec = nullptr;
    AVFrame* sw_frame = nullptr;
    AVPacket* sw_packet = nullptr;
    SwsContext* sw_scaler = nullptr;
    int sw_stream_index = -1;
    uint32_t video_width = 0;
    uint32_t video_height = 0;
    float frame_duration = 1.0f / 30.0f;

    void disableZeroCopy(const char* reason) {
        if (zero_copy_disabled) return;
        zero_copy_disabled = true;
        LOG_TAG_I(TAG, "Decode path: VAAPI hardware decode with CPU copy (%s)", reason);
    }

    bool convertToRgba(const AVFrame* frame, std::vector<uint8_t>& output) {
        ++zero_copy.sws_scale_calls;
        sw_scaler =
            sws_getCachedContext(sw_scaler, frame->width, frame->height, (AVPixelFormat)frame->format, frame->width,
                                 frame->height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!sw_scaler) return false;
        const size_t bytes = (size_t)frame->width * frame->height * 4;
        output.resize(bytes);
        zero_copy.cpu_rgba_bytes += bytes;
        uint8_t* destination[] = {output.data()};
        int stride[] = {frame->width * 4};
        sws_scale(sw_scaler, frame->data, frame->linesize, 0, frame->height, destination, stride);
        return true;
    }

    Impl() {
        current_frame = av_frame_alloc();
        sw_frame = av_frame_alloc();
    }

    ~Impl() {
        if (current_frame) av_frame_free(&current_frame);
        import_cache.destroy();
        hw_decoder.close();
        sws_freeContext(sw_scaler);
        av_frame_free(&sw_frame);
        av_packet_free(&sw_packet);
        avcodec_free_context(&sw_codec);
        avformat_close_input(&sw_format);
        io.close();
    }
};

VideoTexture::~VideoTexture() = default;

std::unique_ptr<VideoTexture> VideoTexture::open(const char* path) {
    if (!path) return nullptr;
    auto texture = std::unique_ptr<VideoTexture>(new VideoTexture());
    texture->impl = std::make_unique<Impl>();

    if (texture->impl->hw_decoder.openFile(path, texture->impl->zero_copy)) {
        texture->impl->is_hw_active = true;
    }

    if (texture->impl->is_hw_active) {
        texture->impl->video_width = (uint32_t)texture->impl->hw_decoder.get_width();
        texture->impl->video_height = (uint32_t)texture->impl->hw_decoder.get_height();
        texture->impl->frame_duration = (float)texture->impl->hw_decoder.get_nominal_frame_duration();
        texture->impl->scheduler.set_time_base_and_fps(texture->impl->hw_decoder.get_time_base(),
                                                       texture->impl->hw_decoder.get_fps());
        if (gpu_init_zero_copy_video(texture->impl->import_cache)) {
            LOG_TAG_I(TAG, "Decode path: VAAPI zero-copy: %s (%ux%u, %.2f FPS)", path, texture->impl->video_width,
                      texture->impl->video_height, 1.0f / texture->impl->frame_duration);
        } else {
            texture->impl->disableZeroCopy("Vulkan video import initialization failed");
        }
        return texture;
    }
    const std::string& reason = texture->impl->hw_decoder.fallback_reason();
    LOG_TAG_I(TAG, "VAAPI unavailable (%s); falling back to software decode",
              reason.empty() ? "could not open the file with VAAPI" : reason.c_str());

    texture->impl->sw_format = avformat_alloc_context();
    if (!texture->impl->sw_format) return nullptr;

    if (!texture->impl->io.open(openMediaSource(path))) return nullptr;
    texture->impl->sw_format->pb = texture->impl->io.context();
    texture->impl->sw_format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&texture->impl->sw_format, nullptr, nullptr, nullptr) < 0) return nullptr;

    if (avformat_find_stream_info(texture->impl->sw_format, nullptr) < 0) return nullptr;
    const int stream_index = av_find_best_stream(texture->impl->sw_format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) return nullptr;

    AVStream* stream = texture->impl->sw_format->streams[stream_index];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    texture->impl->sw_codec = decoder ? avcodec_alloc_context3(decoder) : nullptr;
    if (texture->impl->sw_codec) texture->impl->sw_codec->thread_count = 0;
    if (!texture->impl->sw_codec || avcodec_parameters_to_context(texture->impl->sw_codec, stream->codecpar) < 0 ||
        avcodec_open2(texture->impl->sw_codec, decoder, nullptr) < 0)
        return nullptr;

    texture->impl->sw_stream_index = stream_index;
    texture->impl->video_width = (uint32_t)texture->impl->sw_codec->width;
    texture->impl->video_height = (uint32_t)texture->impl->sw_codec->height;
    const AVRational frame_rate = av_guess_frame_rate(texture->impl->sw_format, stream, nullptr);
    if (frame_rate.num > 0 && frame_rate.den > 0) texture->impl->frame_duration = (float)av_q2d(av_inv_q(frame_rate));
    texture->impl->sw_packet = av_packet_alloc();
    if (!texture->impl->sw_frame || !texture->impl->sw_packet || texture->impl->video_width == 0 ||
        texture->impl->video_height == 0)
        return nullptr;

    LOG_TAG_I(TAG, "Decode path: software: %s (%ux%u, %.2f FPS)", path, texture->impl->video_width,
              texture->impl->video_height, 1.0f / texture->impl->frame_duration);
    return texture;
}

std::unique_ptr<VideoTexture> VideoTexture::openFile(const char* video_path) {
    return open(video_path);
}

uint32_t VideoTexture::width() const {
    return impl->video_width;
}
uint32_t VideoTexture::height() const {
    return impl->video_height;
}
float VideoTexture::frameDuration() const {
    return impl->frame_duration;
}
double VideoTexture::fps() const {
    return impl->frame_duration > 0.0f ? (1.0 / (double)impl->frame_duration) : 30.0;
}
bool VideoTexture::isZeroCopy() const {
    return impl->is_hw_active && !impl->zero_copy_disabled;
}
const std::string& VideoTexture::codecName() const {
    static const std::string kEmpty = "";
    if (impl->is_hw_active) return impl->hw_decoder.get_codec_name();
    if (impl->sw_codec && impl->sw_codec->codec && impl->sw_codec->codec->name) {
        static std::string sw_name;
        sw_name = impl->sw_codec->codec->name;
        return sw_name;
    }
    return kEmpty;
}
const std::string& VideoTexture::containerName() const {
    static const std::string kEmpty = "";
    if (impl->is_hw_active) return impl->hw_decoder.get_container_name();
    if (impl->sw_format && impl->sw_format->iformat && impl->sw_format->iformat->name) {
        static std::string sw_cont;
        sw_cont = impl->sw_format->iformat->name;
        return sw_cont;
    }
    return kEmpty;
}

uint32_t VideoTexture::loopCount() const {
    return impl->loop_count;
}

void VideoTexture::start() {
    impl->is_playing = true;
    impl->is_paused = false;
}

void VideoTexture::stop() {
    impl->is_playing = false;
}

void VideoTexture::pause() {
    impl->is_paused = true;
}

void VideoTexture::resume() {
    impl->is_paused = false;
    impl->is_playing = true;
}

bool VideoTexture::isPlaying() const {
    return impl->is_playing && !impl->is_paused;
}

bool VideoTexture::isPaused() const {
    return impl->is_paused;
}

const ZeroCopyMetrics& VideoTexture::getMetrics() const {
    return impl->zero_copy;
}
const PlaybackStats& VideoTexture::getStats() const {
    return impl->stats;
}
const PerformanceTiming& VideoTexture::getTiming() const {
    return impl->perf;
}

bool VideoTexture::decodeNextFrameZeroCopy(ImportedVideoSurface*& out_surface, AVFrame*& out_av_frame) {
    out_surface = nullptr;
    out_av_frame = nullptr;
    if (!impl->is_hw_active || impl->zero_copy_disabled || !impl->is_playing || impl->is_paused) return false;

    bool eof = false;
    av_frame_unref(impl->current_frame);

    while (!impl->hw_decoder.receive_frame(impl->current_frame, eof, impl->zero_copy, impl->stats, impl->perf)) {
        if (eof) {
            impl->hw_decoder.loop(impl->stats);
            ++impl->loop_count;
            continue;
        }
        return false;
    }

    if (impl->current_frame->format != AV_PIX_FMT_VAAPI) {
        impl->disableZeroCopy("decoder produced software frames");
        return false;
    }

    VASurfaceID surface_id = (VASurfaceID)(uintptr_t)impl->current_frame->data[3];
    VADisplay va_disp = impl->hw_decoder.get_va_display();

    auto t_sync_start = std::chrono::steady_clock::now();
    vaSyncSurface(va_disp, surface_id);
    auto t_sync_end = std::chrono::steady_clock::now();
    impl->perf.va_sync_cpu_ms = std::chrono::duration<double, std::milli>(t_sync_end - t_sync_start).count();

    out_surface = impl->import_cache.get_or_import(va_disp, surface_id, (int)impl->video_width, (int)impl->video_height,
                                                   impl->zero_copy, impl->perf);
    if (!out_surface) {
        LOG_TAG_W(TAG,
                  "Zero-copy DMA-BUF import failed (surface 0x%x) — "
                  "cross-adapter import unsupported or modifier rejected",
                  surface_id);
        impl->disableZeroCopy("DMA-BUF import failed");
        return false;
    }

    out_av_frame = impl->current_frame;
    return true;
}

bool VideoTexture::decodeNextFrame(std::vector<uint8_t>& output) {
    if (!impl->is_playing || impl->is_paused) return false;

    if (impl->is_hw_active) {
        bool eof = false;
        av_frame_unref(impl->current_frame);
        while (!impl->hw_decoder.receive_frame(impl->current_frame, eof, impl->zero_copy, impl->stats, impl->perf)) {
            if (eof) {
                impl->hw_decoder.loop(impl->stats);
                ++impl->loop_count;
                continue;
            }
            return false;
        }
        if (impl->current_frame->format != AV_PIX_FMT_VAAPI) return impl->convertToRgba(impl->current_frame, output);

        VASurfaceID surface_id = (VASurfaceID)(uintptr_t)impl->current_frame->data[3];
        vaSyncSurface(impl->hw_decoder.get_va_display(), surface_id);

        if (!impl->sw_frame) impl->sw_frame = av_frame_alloc();
        av_frame_unref(impl->sw_frame);
        if (av_hwframe_transfer_data(impl->sw_frame, impl->current_frame, 0) >= 0 &&
            impl->convertToRgba(impl->sw_frame, output))
            return true;
        LOG_TAG_W(TAG, "HW frame CPU transfer failed for VA surface 0x%x — skipping frame", surface_id);
        return true;
    }

    if (!impl->sw_codec || !impl->sw_format) return false;
    int loops_without_frame = 0;
    for (;;) {
        const int received = avcodec_receive_frame(impl->sw_codec, impl->sw_frame);
        if (received == 0) return impl->convertToRgba(impl->sw_frame, output);
        if (av_read_frame(impl->sw_format, impl->sw_packet) < 0) {
            if (++loops_without_frame > 1) return false;
            av_seek_frame(impl->sw_format, impl->sw_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(impl->sw_codec);
            ++impl->loop_count;
            continue;
        }
        if (impl->sw_packet->stream_index == impl->sw_stream_index) {
            avcodec_send_packet(impl->sw_codec, impl->sw_packet);
        }
        av_packet_unref(impl->sw_packet);
    }
}

}  // namespace wallpaper_engine
