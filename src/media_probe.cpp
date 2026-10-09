// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "synaxis/media_probe.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>

namespace synaxis {

namespace {

struct FormatCloser {
    void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecFreer {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFreer {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFreer {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsFreer {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFreer>;
using FramePtr = std::unique_ptr<AVFrame, FrameFreer>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFreer>;
using SwsPtr = std::unique_ptr<SwsContext, SwsFreer>;

FormatPtr OpenInput(const std::filesystem::path& path) {
    // Broken files are expected in a scan; FFmpeg's stderr chatter isn't useful.
    static std::once_flag quiet;
    std::call_once(quiet, [] { av_log_set_level(AV_LOG_QUIET); });

    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, path.c_str(), nullptr, nullptr) < 0) return nullptr;
    FormatPtr fmt(raw);
    if (avformat_find_stream_info(fmt.get(), nullptr) < 0) return nullptr;
    return fmt;
}

// The main video stream: the largest one that isn't embedded cover art.
int FindVideoStream(const AVFormatContext* fmt) {
    int best = -1;
    int64_t best_area = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* s = fmt->streams[i];
        if (s->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
        int64_t area = int64_t(s->codecpar->width) * s->codecpar->height;
        if (area > best_area) {
            best = int(i);
            best_area = area;
        }
    }
    return best;
}

double Duration(const AVFormatContext* fmt, const AVStream* s) {
    if (fmt->duration > 0) return fmt->duration / double(AV_TIME_BASE);
    if (s->duration > 0) return s->duration * av_q2d(s->time_base);
    return 0.0;
}

// Pulls the next decoded frame of `stream` into `frame`. Returns false at
// end of stream or after too many packets without a frame (broken files).
bool DecodeNextFrame(AVFormatContext* fmt, AVCodecContext* dec, int stream, AVPacket* pkt,
                     AVFrame* frame) {
    bool draining = false;
    for (int budget = 0; budget < 4000; ++budget) {
        int r = avcodec_receive_frame(dec, frame);
        if (r == 0) return true;
        if (r != AVERROR(EAGAIN) || draining) return false;

        if (av_read_frame(fmt, pkt) < 0) {
            avcodec_send_packet(dec, nullptr);
            draining = true;
            continue;
        }
        // A single corrupt packet isn't fatal; keep feeding.
        if (pkt->stream_index == stream) avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
    }
    return false;
}

// Scales to 8-bit full-range YUV 4:2:0, which is what the JPEG encoder takes
// and makes brightness easy to measure from the Y plane.
FramePtr ScaleForJpeg(const AVFrame* src, int dst_w, int dst_h) {
    SwsPtr sws(sws_getContext(src->width, src->height, AVPixelFormat(src->format), dst_w, dst_h,
                              AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) return nullptr;

    int src_space = src->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(src_space),
                             src->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                             sws_getCoefficients(SWS_CS_ITU601), 1, 0, 1 << 16, 1 << 16);

    FramePtr dst(av_frame_alloc());
    if (!dst) return nullptr;
    dst->format = AV_PIX_FMT_YUV420P;
    dst->width = dst_w;
    dst->height = dst_h;
    dst->color_range = AVCOL_RANGE_JPEG;
    if (av_frame_get_buffer(dst.get(), 0) < 0) return nullptr;
    sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dst->data, dst->linesize);
    return dst;
}

// Trims letterbox/pillarbox bars from a YUV420P frame (as made by
// ScaleForJpeg) and rescales what's left to the same width. Bars only count
// when both opposite edges have them at about the same size, so a dark
// scene with a black sky isn't mistaken for letterboxing.
FramePtr CropBars(FramePtr frame) {
    const int w = frame->width, h = frame->height;
    const uint8_t* y_plane = frame->data[0];
    const int stride = frame->linesize[0];

    // A line is a bar when almost none of its pixels are brighter than black.
    auto dark = [&](int x0, int y0, int dx, int dy, int count) {
        int bright = 0;
        for (int i = 0; i < count; i += 2) {
            if (y_plane[ptrdiff_t(y0 + i * dy) * stride + x0 + i * dx] > 40) ++bright;
        }
        return bright * 2 < count / 50 + 1;
    };
    int top = 0, bottom = h, left = 0, right = w;
    while (top < h / 3 && dark(0, top, 1, 0, w)) ++top;
    while (bottom > h * 2 / 3 && dark(0, bottom - 1, 1, 0, w)) --bottom;
    while (left < w / 3 && dark(left, 0, 0, 1, h)) ++left;
    while (right > w * 2 / 3 && dark(right - 1, 0, 0, 1, h)) --right;

    auto symmetric = [](int a, int b, int size) {
        return a >= size / 50 && b >= size / 50 && std::abs(a - b) <= size / 50 + 2;
    };
    if (!symmetric(top, h - bottom, h)) top = 0, bottom = h;
    if (!symmetric(left, w - right, w)) left = 0, right = w;
    if (top == 0 && left == 0) return frame;

    // Even offsets keep the half-resolution chroma planes aligned.
    left &= ~1;
    top &= ~1;
    const int crop_w = (right - left) & ~1, crop_h = (bottom - top) & ~1;
    const int dst_h = int(std::lround(double(w) * crop_h / crop_w)) & ~1;
    if (crop_w <= 0 || crop_h <= 0 || dst_h <= 0) return frame;

    const uint8_t* planes[4] = {
        frame->data[0] + ptrdiff_t(top) * frame->linesize[0] + left,
        frame->data[1] + ptrdiff_t(top / 2) * frame->linesize[1] + left / 2,
        frame->data[2] + ptrdiff_t(top / 2) * frame->linesize[2] + left / 2,
        nullptr,
    };
    SwsPtr sws(sws_getContext(crop_w, crop_h, AV_PIX_FMT_YUV420P, w, dst_h, AV_PIX_FMT_YUV420P,
                              SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) return frame;
    const int* coeffs = sws_getCoefficients(SWS_CS_ITU601);
    sws_setColorspaceDetails(sws.get(), coeffs, 1, coeffs, 1, 0, 1 << 16, 1 << 16);

    FramePtr out(av_frame_alloc());
    if (!out) return frame;
    out->format = AV_PIX_FMT_YUV420P;
    out->width = w;
    out->height = dst_h;
    out->color_range = AVCOL_RANGE_JPEG;
    if (av_frame_get_buffer(out.get(), 0) < 0) return frame;
    sws_scale(sws.get(), planes, frame->linesize, 0, crop_h, out->data, out->linesize);
    return out;
}

struct LumaStats {
    double mean = 0;
    double stddev = 0;
};

LumaStats MeasureLuma(const AVFrame* f) {
    double sum = 0, sum_sq = 0;
    size_t n = 0;
    for (int y = 0; y < f->height; y += 4) {
        const uint8_t* row = f->data[0] + ptrdiff_t(y) * f->linesize[0];
        for (int x = 0; x < f->width; x += 4) {
            sum += row[x];
            sum_sq += double(row[x]) * row[x];
            ++n;
        }
    }
    if (n == 0) return {};
    double mean = sum / n;
    return {mean, std::sqrt(std::max(0.0, sum_sq / n - mean * mean))};
}

bool EncodeJpeg(const AVFrame* frame, std::string* out) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) return false;

    // Newer FFmpeg wants plain YUV420P tagged full-range; older builds only
    // accept the (now deprecated) YUVJ420P. Same memory layout either way.
    for (AVPixelFormat pix_fmt : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P}) {
        CodecPtr enc(avcodec_alloc_context3(codec));
        if (!enc) return false;
        enc->width = frame->width;
        enc->height = frame->height;
        enc->pix_fmt = pix_fmt;
        enc->color_range = AVCOL_RANGE_JPEG;
        enc->time_base = AVRational{1, 25};
        enc->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
        enc->flags |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * 3;
        if (avcodec_open2(enc.get(), codec, nullptr) < 0) continue;

        FramePtr input(av_frame_clone(frame));
        if (!input) return false;
        input->format = pix_fmt;
        input->quality = enc->global_quality;
        input->pts = 0;

        PacketPtr pkt(av_packet_alloc());
        if (!pkt || avcodec_send_frame(enc.get(), input.get()) < 0 ||
            avcodec_send_frame(enc.get(), nullptr) < 0 ||
            avcodec_receive_packet(enc.get(), pkt.get()) < 0) {
            return false;
        }
        out->assign(reinterpret_cast<const char*>(pkt->data), pkt->size);
        return true;
    }
    return false;
}

} // namespace

std::optional<MediaInfo> ProbeMedia(const std::filesystem::path& path) {
    FormatPtr fmt = OpenInput(path);
    if (!fmt) return std::nullopt;
    int index = FindVideoStream(fmt.get());
    if (index < 0) return std::nullopt;

    const AVStream* s = fmt->streams[index];
    MediaInfo info;
    info.duration = Duration(fmt.get(), s);
    info.width = s->codecpar->width;
    info.height = s->codecpar->height;
    info.video_codec = avcodec_get_name(s->codecpar->codec_id);
    info.hdr = s->codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
               s->codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67;
    return info;
}

bool WriteThumbnail(const std::filesystem::path& video, const std::filesystem::path& out,
                    double duration, int width) {
    FormatPtr fmt = OpenInput(video);
    if (!fmt) return false;
    int index = FindVideoStream(fmt.get());
    if (index < 0) return false;
    AVStream* stream = fmt->streams[index];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;
    CodecPtr dec(avcodec_alloc_context3(codec));
    if (!dec || avcodec_parameters_to_context(dec.get(), stream->codecpar) < 0) return false;
    dec->thread_count = 0;  // auto
    if (avcodec_open2(dec.get(), codec, nullptr) < 0) return false;

    // Only the video stream matters; skip demuxing work for the rest.
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (int(i) != index) fmt->streams[i]->discard = AVDISCARD_ALL;
    }

    PacketPtr pkt(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    if (!pkt || !frame) return false;

    FramePtr best;
    double best_score = -1;
    // Early points first (avoid spoilers), then further in if those are dark.
    static constexpr double kSeekPoints[] = {0.15, 0.25, 0.40, 0.55, 0.08};
    for (double point : kSeekPoints) {
        if (duration > 0) {
            auto ts = int64_t(duration * point * AV_TIME_BASE);
            if (av_seek_frame(fmt.get(), -1, ts, AVSEEK_FLAG_BACKWARD) < 0) continue;
            avcodec_flush_buffers(dec.get());
        }
        if (!DecodeNextFrame(fmt.get(), dec.get(), index, pkt.get(), frame.get())) {
            if (duration <= 0) break;
            continue;
        }

        // Respect the sample aspect ratio so anamorphic video isn't squashed.
        double sar = frame->sample_aspect_ratio.num > 0
                         ? av_q2d(frame->sample_aspect_ratio)
                         : 1.0;
        double display_aspect = frame->width * sar / std::max(1, frame->height);
        int dst_w = std::min(width, int(std::lround(frame->width * sar))) & ~1;
        int dst_h = int(std::lround(dst_w / display_aspect)) & ~1;
        FramePtr scaled = dst_w > 0 && dst_h > 0 ? ScaleForJpeg(frame.get(), dst_w, dst_h)
                                                 : nullptr;
        av_frame_unref(frame.get());
        if (!scaled) continue;
        scaled = CropBars(std::move(scaled));

        LumaStats luma = MeasureLuma(scaled.get());
        double score = std::min(luma.mean, 110.0) + luma.stddev;
        if (score > best_score) {
            best = std::move(scaled);
            best_score = score;
        }
        if (luma.mean >= 45 && luma.stddev >= 25) break;  // good enough
        if (duration <= 0) break;                         // nowhere else to seek
    }
    if (!best) return false;

    std::string jpeg;
    if (!EncodeJpeg(best.get(), &jpeg)) return false;

    std::filesystem::path tmp = out;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.write(jpeg.data(), std::streamsize(jpeg.size()))) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, out, ec);
    if (ec) std::filesystem::remove(tmp, ec);
    return !ec;
}

} // namespace synaxis
