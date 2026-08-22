#include "video_texture.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <chrono>

#include "core/logger.h"
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
namespace {

constexpr int kIoBufferSize = 4096;

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
    mp4.resize(data.size() - offset);
    memcpy(mp4.data(), data.data() + offset, mp4.size());
    return true;
}

}  // namespace

struct VideoTexture::Impl {
    VideoDecoder hw_decoder;
    VideoImportCache import_cache;
    VideoScheduler scheduler;
    ZeroCopyMetrics zero_copy;
    PlaybackStats stats;
    PerformanceTiming perf;

    bool is_hw_active = false;
    AVFrame* current_frame = nullptr;

    // Software fallback structures if VAAPI is unavailable
    MemoryInput input;
    AVFormatContext* sw_format = nullptr;
    AVCodecContext* sw_codec = nullptr;
    AVFrame* sw_frame = nullptr;
    AVPacket* sw_packet = nullptr;
    SwsContext* sw_scaler = nullptr;
    AVIOContext* sw_io = nullptr;
    uint8_t* sw_io_buffer = nullptr;
    int sw_stream_index = -1;
    uint32_t video_width = 0;
    uint32_t video_height = 0;
    float frame_duration = 1.0f / 30.0f;

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
        if (sw_io) avio_context_free(&sw_io);
    }
};

VideoTexture::~VideoTexture() = default;

std::unique_ptr<VideoTexture> VideoTexture::open(const char* path) {
    if (!path) return nullptr;
    auto texture = std::unique_ptr<VideoTexture>(new VideoTexture());
    texture->impl = std::make_unique<Impl>();

    std::vector<uint8_t> mp4_bytes;
    const bool is_embedded = readEmbeddedMp4(path, mp4_bytes);

    // Try hardware VAAPI decoder first
    if (is_embedded) {
        if (texture->impl->hw_decoder.openMemory(mp4_bytes, texture->impl->zero_copy)) {
            texture->impl->is_hw_active = true;
        }
    } else {
        if (texture->impl->hw_decoder.openFile(path, texture->impl->zero_copy)) {
            texture->impl->is_hw_active = true;
        }
    }

    if (texture->impl->is_hw_active) {
        texture->impl->video_width = (uint32_t)texture->impl->hw_decoder.get_width();
        texture->impl->video_height = (uint32_t)texture->impl->hw_decoder.get_height();
        texture->impl->frame_duration = (float)texture->impl->hw_decoder.get_nominal_frame_duration();
        texture->impl->scheduler.set_time_base_and_fps(texture->impl->hw_decoder.get_time_base(),
                                                       texture->impl->hw_decoder.get_fps());
        LOG_TAG_I(TAG, "Hardware Zero-Copy VideoTexture opened: %s (%ux%u, %.2f FPS)", path, texture->impl->video_width,
                  texture->impl->video_height, 1.0f / texture->impl->frame_duration);
        return texture;
    }

    // Fallback: Software FFmpeg decoder
    texture->impl->sw_format = avformat_alloc_context();
    if (!texture->impl->sw_format) return nullptr;

    if (is_embedded) {
        texture->impl->input.bytes = std::move(mp4_bytes);
        texture->impl->sw_io_buffer = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
        texture->impl->sw_io = avio_alloc_context(texture->impl->sw_io_buffer, kIoBufferSize, 0, &texture->impl->input,
                                                  readMemory, nullptr, seekMemory);
        if (!texture->impl->sw_io) return nullptr;
        texture->impl->sw_format->pb = texture->impl->sw_io;
        if (avformat_open_input(&texture->impl->sw_format, "memory", nullptr, nullptr) < 0) return nullptr;
    } else {
        if (avformat_open_input(&texture->impl->sw_format, path, nullptr, nullptr) < 0) return nullptr;
    }

    if (avformat_find_stream_info(texture->impl->sw_format, nullptr) < 0) return nullptr;
    const int stream_index = av_find_best_stream(texture->impl->sw_format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) return nullptr;

    AVStream* stream = texture->impl->sw_format->streams[stream_index];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    texture->impl->sw_codec = decoder ? avcodec_alloc_context3(decoder) : nullptr;
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

    LOG_TAG_I(TAG, "Software VideoTexture opened: %s (%ux%u, %.2f FPS)", path, texture->impl->video_width,
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
    return impl->is_hw_active;
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
    if (!impl->is_hw_active) return false;

    bool eof = false;
    av_frame_unref(impl->current_frame);

    while (!impl->hw_decoder.receive_frame(impl->current_frame, eof, impl->zero_copy, impl->stats, impl->perf)) {
        if (eof) {
            impl->hw_decoder.loop(impl->stats);
            continue;
        }
        return false;
    }

    if (impl->current_frame->format == AV_PIX_FMT_VAAPI) {
        VASurfaceID surface_id = (VASurfaceID)(uintptr_t)impl->current_frame->data[3];
        VADisplay va_disp = impl->hw_decoder.get_va_display();

        auto t_sync_start = std::chrono::steady_clock::now();
        vaSyncSurface(va_disp, surface_id);
        auto t_sync_end = std::chrono::steady_clock::now();
        impl->perf.va_sync_cpu_ms = std::chrono::duration<double, std::milli>(t_sync_end - t_sync_start).count();

        out_surface = impl->import_cache.get_or_import(va_disp, surface_id, (int)impl->video_width,
                                                       (int)impl->video_height, impl->zero_copy, impl->perf);
        out_av_frame = impl->current_frame;
        return (out_surface != nullptr);
    }

    return false;
}

bool VideoTexture::decodeNextFrame(std::vector<uint8_t>& output) {
    if (impl->is_hw_active) {
        bool eof = false;
        av_frame_unref(impl->current_frame);
        while (!impl->hw_decoder.receive_frame(impl->current_frame, eof, impl->zero_copy, impl->stats, impl->perf)) {
            if (eof) {
                impl->hw_decoder.loop(impl->stats);
                continue;
            }
            return false;
        }
        if (impl->current_frame->format == AV_PIX_FMT_VAAPI) {
            VASurfaceID surface_id = (VASurfaceID)(uintptr_t)impl->current_frame->data[3];
            VADisplay va_disp = impl->hw_decoder.get_va_display();
            vaSyncSurface(va_disp, surface_id);

            if (!impl->sw_frame) impl->sw_frame = av_frame_alloc();
            av_frame_unref(impl->sw_frame);
            if (av_hwframe_transfer_data(impl->sw_frame, impl->current_frame, 0) >= 0) {
                ++impl->zero_copy.sws_scale_calls;
                impl->sw_scaler = sws_getCachedContext(impl->sw_scaler, impl->sw_frame->width, impl->sw_frame->height,
                                                       (AVPixelFormat)impl->sw_frame->format, impl->sw_frame->width,
                                                       impl->sw_frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                                       nullptr, nullptr);
                if (impl->sw_scaler) {
                    size_t bytes = (size_t)impl->sw_frame->width * impl->sw_frame->height * 4;
                    output.resize(bytes);
                    impl->zero_copy.cpu_rgba_bytes += bytes;
                    uint8_t* dest[] = {output.data()};
                    int stride[] = {impl->sw_frame->width * 4};
                    sws_scale(impl->sw_scaler, impl->sw_frame->data, impl->sw_frame->linesize, 0,
                              impl->sw_frame->height, dest, stride);
                    return true;
                }
            }
            return true;
        }
    }

    // Software decode fallback
    if (!impl->sw_codec || !impl->sw_format) return false;
    for (;;) {
        const int received = avcodec_receive_frame(impl->sw_codec, impl->sw_frame);
        if (received == 0) {
            ++impl->zero_copy.sws_scale_calls;
            impl->sw_scaler =
                sws_getCachedContext(impl->sw_scaler, impl->sw_frame->width, impl->sw_frame->height,
                                     (AVPixelFormat)impl->sw_frame->format, impl->sw_frame->width,
                                     impl->sw_frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!impl->sw_scaler) return false;
            size_t bytes = (size_t)impl->sw_frame->width * impl->sw_frame->height * 4;
            output.resize(bytes);
            impl->zero_copy.cpu_rgba_bytes += bytes;
            uint8_t* destination[] = {output.data()};
            int stride[] = {impl->sw_frame->width * 4};
            sws_scale(impl->sw_scaler, impl->sw_frame->data, impl->sw_frame->linesize, 0, impl->sw_frame->height,
                      destination, stride);
            return true;
        }
        if (av_read_frame(impl->sw_format, impl->sw_packet) < 0) {
            av_seek_frame(impl->sw_format, impl->sw_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(impl->sw_codec);
            continue;
        }
        if (impl->sw_packet->stream_index == impl->sw_stream_index) {
            avcodec_send_packet(impl->sw_codec, impl->sw_packet);
        }
        av_packet_unref(impl->sw_packet);
    }
}

}  // namespace wallpaper_engine
