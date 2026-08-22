#include "video_decoder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "shared/core/logger.h"
#include "shared/graphics/backend/gpu_device_manager.h"

#define TAG "DECODER"
constexpr int kIoBufferSize = 64 * 1024;

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() {
    close();
}

int VideoDecoder::readPacket(void* opaque, uint8_t* buf, int buf_size) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (!input || input->bytes.empty()) return AVERROR_EOF;
    const size_t count = std::min(input->bytes.size() - input->position, (size_t)buf_size);
    if (count == 0) return AVERROR_EOF;
    memcpy(buf, input->bytes.data() + input->position, count);
    input->position += count;
    return (int)count;
}

int64_t VideoDecoder::seekMemory(void* opaque, int64_t offset, int whence) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (!input) return AVERROR(EINVAL);
    if (whence == AVSEEK_SIZE) return (int64_t)input->bytes.size();
    const int origin = whence & ~AVSEEK_FORCE;
    int64_t position = origin == SEEK_SET   ? offset
                       : origin == SEEK_CUR ? (int64_t)input->position + offset
                                            : (int64_t)input->bytes.size() + offset;
    if (position < 0 || position > (int64_t)input->bytes.size()) return AVERROR(EINVAL);
    input->position = (size_t)position;
    return position;
}

bool VideoDecoder::openFile(const char* path, ZeroCopyMetrics& zero_copy, const std::string& drm_render_node) {
    close();
    int result = avformat_open_input(&format_ctx_, path, nullptr, nullptr);
    if (result < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, err, sizeof(err));
        LOG_TAG_E(TAG, "avformat_open_input failed for %s: %s", path, err);
        return false;
    }
    return initDecoder(zero_copy, drm_render_node);
}

bool VideoDecoder::openMemory(const std::vector<uint8_t>& memory_data, ZeroCopyMetrics& zero_copy,
                              const std::string& drm_render_node) {
    close();
    if (memory_data.empty()) return false;

    memory_input_.bytes = memory_data;
    memory_input_.position = 0;

    io_buffer_ = (uint8_t*)av_malloc(kIoBufferSize);
    if (!io_buffer_) return false;

    io_ctx_ = avio_alloc_context(io_buffer_, kIoBufferSize, 0, &memory_input_, readPacket, nullptr, seekMemory);
    format_ctx_ = avformat_alloc_context();
    if (!io_ctx_ || !format_ctx_) {
        close();
        return false;
    }

    format_ctx_->pb = io_ctx_;
    format_ctx_->flags |= AVFMT_FLAG_CUSTOM_IO;

    int result = avformat_open_input(&format_ctx_, nullptr, nullptr, nullptr);
    if (result < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, err, sizeof(err));
        LOG_TAG_E(TAG, "avformat_open_input (custom memory io) failed: %s", err);
        close();
        return false;
    }

    return initDecoder(zero_copy, drm_render_node);
}

bool VideoDecoder::initDecoder(ZeroCopyMetrics& zero_copy, const std::string& custom_drm_node) {
    (void)zero_copy;
    int result = avformat_find_stream_info(format_ctx_, nullptr);
    if (result < 0) {
        LOG_TAG_E(TAG, "avformat_find_stream_info failed");
        close();
        return false;
    }

    video_stream_index_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index_ < 0) {
        LOG_TAG_E(TAG, "No video stream found in container");
        close();
        return false;
    }

    AVStream* stream = format_ctx_->streams[video_stream_index_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        LOG_TAG_E(TAG, "Decoder not found for codec ID %d", stream->codecpar->codec_id);
        close();
        return false;
    }

    decoder_ctx_ = avcodec_alloc_context3(codec);
    if (!decoder_ctx_) {
        LOG_TAG_E(TAG, "Failed to allocate codec context");
        close();
        return false;
    }

    std::string drm_device = custom_drm_node;
    if (drm_device.empty()) {
        drm_device = GpuDeviceManager::instance().getSelectedDrmRenderNode();
    }
    if (drm_device.empty()) {
        drm_device = "/dev/dri/renderD129";
    }

    result = av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_VAAPI, drm_device.c_str(), nullptr, 0);
    if (result < 0 && drm_device != "/dev/dri/renderD128") {
        LOG_TAG_W(TAG, "VAAPI device creation on %s failed, trying /dev/dri/renderD128", drm_device.c_str());
        result = av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0);
    }
    if (result < 0) {
        LOG_TAG_E(TAG, "VAAPI device creation failed on all candidate render nodes");
        close();
        return false;
    }

    decoder_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    decoder_ctx_->get_format = [](AVCodecContext*, const AVPixelFormat* formats) -> AVPixelFormat {
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == AV_PIX_FMT_VAAPI) return *f;
        }
        return formats[0];
    };

    result = avcodec_parameters_to_context(decoder_ctx_, stream->codecpar);
    if (result < 0 || (result = avcodec_open2(decoder_ctx_, codec, nullptr)) < 0) {
        LOG_TAG_E(TAG, "avcodec_open2 failed for VAAPI decoder");
        close();
        return false;
    }

    auto* hw_ctx = reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx_->data);
    auto* va_hwctx = reinterpret_cast<AVVAAPIDeviceContext*>(hw_ctx->hwctx);
    va_display_ = va_hwctx->display;

    width_ = decoder_ctx_->width;
    height_ = decoder_ctx_->height;
    time_base_ = av_q2d(stream->time_base);
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        fps_ = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
        fps_ = av_q2d(stream->r_frame_rate);
    }
    nominal_frame_duration_ = fps_ > 0.0 ? 1.0 / fps_ : (1.0 / 60.0);
    codec_name_ = avcodec_get_name(codec->id);
    container_name_ = format_ctx_->iformat->long_name ? format_ctx_->iformat->long_name : "unknown";

    packet_ = av_packet_alloc();

    LOG_TAG_I(TAG, "Zero-Copy VAAPI Initialized: %s (%dx%d @ %.2f fps, time_base=%.8f) on %s", codec_name_.c_str(),
              width_, height_, fps_, time_base_, drm_device.c_str());
    return true;
}

bool VideoDecoder::receive_frame(AVFrame* out_frame, bool& out_eof, ZeroCopyMetrics& zero_copy, PlaybackStats& stats,
                                 PerformanceTiming& perf) {
    out_eof = false;
    while (true) {
        auto t0 = std::chrono::steady_clock::now();
        int ret = avcodec_receive_frame(decoder_ctx_, out_frame);
        auto t1 = std::chrono::steady_clock::now();
        perf.decode_submit_cpu_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ret == 0) {
            if (out_frame->format == AV_PIX_FMT_VAAPI) {
                ++zero_copy.vaapi_frames_decoded;
                ++stats.frames_decoded;
                return true;
            } else {
                LOG_TAG_W(TAG, "Non-VAAPI frame received format: %d", out_frame->format);
                av_frame_unref(out_frame);
                return false;
            }
        }
        if (ret == AVERROR_EOF) {
            out_eof = true;
            return false;
        }
        if (ret != AVERROR(EAGAIN)) {
            LOG_TAG_W(TAG, "avcodec_receive_frame error: %d", ret);
            return false;
        }

        auto t_demux_start = std::chrono::steady_clock::now();
        int read_ret = av_read_frame(format_ctx_, packet_);
        auto t_demux_end = std::chrono::steady_clock::now();
        perf.demux_cpu_ms = std::chrono::duration<double, std::milli>(t_demux_end - t_demux_start).count();

        if (read_ret < 0) {
            if (read_ret == AVERROR_EOF) {
                avcodec_send_packet(decoder_ctx_, nullptr);
            } else {
                LOG_TAG_W(TAG, "av_read_frame error: %d", read_ret);
                out_eof = true;
                return false;
            }
        } else {
            ++stats.packets_read;
            if (packet_->stream_index == video_stream_index_) {
                int send_ret = avcodec_send_packet(decoder_ctx_, packet_);
                if (send_ret < 0 && send_ret != AVERROR(EAGAIN) && send_ret != AVERROR_EOF) {
                    LOG_TAG_W(TAG, "avcodec_send_packet error: %d", send_ret);
                }
            }
            av_packet_unref(packet_);
        }
    }
}

bool VideoDecoder::loop(PlaybackStats& stats) {
    if (decoder_ctx_) avcodec_flush_buffers(decoder_ctx_);
    if (format_ctx_ && video_stream_index_ >= 0) {
        av_seek_frame(format_ctx_, video_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
    }
    ++stats.loop_count;
    return true;
}

void VideoDecoder::close() {
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (decoder_ctx_) {
        avcodec_free_context(&decoder_ctx_);
        decoder_ctx_ = nullptr;
    }
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    if (io_ctx_) {
        avio_context_free(&io_ctx_);
        io_ctx_ = nullptr;
    }
    io_buffer_ = nullptr;
    memory_input_.bytes.clear();
    memory_input_.position = 0;
    va_display_ = nullptr;
    video_stream_index_ = -1;
}
