#include "video_texture.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include "core/logger.h"

#define TAG "VIDEO"

namespace wallpaper_engine {
namespace {

constexpr int kIoBufferSize = 4096;

struct MemoryInput {
    std::vector<uint8_t> bytes;
    size_t position = 0;
};

int readPacket(void* opaque, uint8_t* buffer, int buffer_size) {
    auto* input = static_cast<MemoryInput*>(opaque);
    const size_t count = std::min(input->bytes.size() - input->position, (size_t)buffer_size);
    if (count == 0) return AVERROR_EOF;
    memcpy(buffer, input->bytes.data() + input->position, count);
    input->position += count;
    return (int)count;
}

int64_t seek(void* opaque, int64_t offset, int whence) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (whence == AVSEEK_SIZE) return (int64_t)input->bytes.size();
    const int origin = whence & ~AVSEEK_FORCE;
    int64_t position = origin == SEEK_SET ? offset : origin == SEEK_CUR ? (int64_t)input->position + offset
                                                                        : (int64_t)input->bytes.size() + offset;
    if (position < 0 || position > (int64_t)input->bytes.size()) return AVERROR(EINVAL);
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
    mp4.assign(ftyp - 4, data.end());
    return true;
}

}  // namespace

struct VideoTexture::Impl {
    MemoryInput input;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    AVIOContext* io = nullptr;
    uint8_t* io_buffer = nullptr;
    int stream_index = -1;
    uint32_t video_width = 0;
    uint32_t video_height = 0;
    float frame_duration = 1.0f / 30.0f;

    ~Impl() {
        sws_freeContext(scaler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) avio_context_free(&io);
    }
};

VideoTexture::~VideoTexture() = default;

std::unique_ptr<VideoTexture> VideoTexture::open(const char* path) {
    if (!path) return nullptr;
    auto texture = std::unique_ptr<VideoTexture>(new VideoTexture());
    texture->impl = std::make_unique<Impl>();
    if (!readEmbeddedMp4(path, texture->impl->input.bytes)) return nullptr;

    texture->impl->io_buffer = (uint8_t*)av_malloc(kIoBufferSize);
    texture->impl->io = texture->impl->io_buffer ? avio_alloc_context(texture->impl->io_buffer, kIoBufferSize, 0,
        &texture->impl->input, readPacket, nullptr, seek) : nullptr;
    texture->impl->format = avformat_alloc_context();
    if (!texture->impl->io || !texture->impl->format) return nullptr;
    texture->impl->format->pb = texture->impl->io;
    texture->impl->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&texture->impl->format, nullptr, nullptr, nullptr) < 0 ||
        avformat_find_stream_info(texture->impl->format, nullptr) < 0)
        return nullptr;

    const int stream_index = av_find_best_stream(texture->impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) return nullptr;
    AVStream* stream = texture->impl->format->streams[stream_index];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    texture->impl->codec = decoder ? avcodec_alloc_context3(decoder) : nullptr;
    if (!texture->impl->codec || avcodec_parameters_to_context(texture->impl->codec, stream->codecpar) < 0 ||
        avcodec_open2(texture->impl->codec, decoder, nullptr) < 0)
        return nullptr;

    texture->impl->stream_index = stream_index;
    texture->impl->video_width = (uint32_t)texture->impl->codec->width;
    texture->impl->video_height = (uint32_t)texture->impl->codec->height;
    const AVRational frame_rate = av_guess_frame_rate(texture->impl->format, stream, nullptr);
    if (frame_rate.num > 0 && frame_rate.den > 0) texture->impl->frame_duration = (float)av_q2d(av_inv_q(frame_rate));
    texture->impl->frame = av_frame_alloc();
    texture->impl->packet = av_packet_alloc();
    if (!texture->impl->frame || !texture->impl->packet || texture->impl->video_width == 0 || texture->impl->video_height == 0)
        return nullptr;
    LOG_TAG_I(TAG, "Opened video texture: %s (%ux%u, %.2f FPS)", path, texture->impl->video_width,
              texture->impl->video_height, 1.0f / texture->impl->frame_duration);
    return texture;
}

uint32_t VideoTexture::width() const { return impl->video_width; }
uint32_t VideoTexture::height() const { return impl->video_height; }
float VideoTexture::frameDuration() const { return impl->frame_duration; }

bool VideoTexture::decodeNextFrame(std::vector<uint8_t>& output) {
    for (;;) {
        const int received = avcodec_receive_frame(impl->codec, impl->frame);
        if (received == 0) {
            impl->scaler = sws_getCachedContext(impl->scaler, impl->frame->width, impl->frame->height,
                (AVPixelFormat)impl->frame->format, impl->frame->width, impl->frame->height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!impl->scaler) return false;
            output.resize((size_t)impl->frame->width * impl->frame->height * 4);
            uint8_t* destination[] = {output.data()};
            int stride[] = {impl->frame->width * 4};
            sws_scale(impl->scaler, impl->frame->data, impl->frame->linesize, 0, impl->frame->height, destination, stride);
            return true;
        }
        if (av_read_frame(impl->format, impl->packet) < 0) {
            av_seek_frame(impl->format, impl->stream_index, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(impl->codec);
            continue;
        }
        if (impl->packet->stream_index == impl->stream_index) avcodec_send_packet(impl->codec, impl->packet);
        av_packet_unref(impl->packet);
    }
}

}  // namespace wallpaper_engine
