#pragma once

#include "video_types.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>
}

#include <string>
#include <vector>

class VideoDecoder {
   public:
    VideoDecoder();
    ~VideoDecoder();

    bool openFile(const char* path, ZeroCopyMetrics& zero_copy, const std::string& drm_render_node = "");
    bool openMemory(const std::vector<uint8_t>& memory_data, ZeroCopyMetrics& zero_copy,
                    const std::string& drm_render_node = "");

    bool receive_frame(AVFrame* out_frame, bool& out_eof, ZeroCopyMetrics& zero_copy, PlaybackStats& stats,
                       PerformanceTiming& perf);
    bool loop(PlaybackStats& stats);
    void close();

    int get_width() const {
        return width_;
    }
    int get_height() const {
        return height_;
    }
    double get_time_base() const {
        return time_base_;
    }
    double get_fps() const {
        return fps_;
    }
    double get_nominal_frame_duration() const {
        return nominal_frame_duration_;
    }
    VADisplay get_va_display() const {
        return va_display_;
    }
    const std::string& get_codec_name() const {
        return codec_name_;
    }
    const std::string& get_container_name() const {
        return container_name_;
    }

   private:
    struct MemoryInput {
        std::vector<uint8_t> bytes;
        size_t position = 0;
    };

    bool initDecoder(ZeroCopyMetrics& zero_copy, const std::string& drm_render_node);

    static int readPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t seekMemory(void* opaque, int64_t offset, int whence);

    AVFormatContext* format_ctx_ = nullptr;
    AVCodecContext* decoder_ctx_ = nullptr;
    AVBufferRef* hw_device_ctx_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVIOContext* io_ctx_ = nullptr;
    uint8_t* io_buffer_ = nullptr;
    MemoryInput memory_input_;

    int video_stream_index_ = -1;
    int width_ = 0;
    int height_ = 0;
    double time_base_ = 0.0;
    double fps_ = 60.0;
    double nominal_frame_duration_ = 1.0 / 60.0;
    VADisplay va_display_ = nullptr;
    std::string codec_name_;
    std::string container_name_;
};
