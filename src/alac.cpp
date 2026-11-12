#include "alac.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
}
namespace ttp::aac {
struct AlacCore::Impl {
    AVCodecContext* context{};
    AVFrame* frame{av_frame_alloc()};
    AVPacket* packet{av_packet_alloc()};
    ~Impl() { av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&context); }
};
AlacCore::AlacCore() = default;
AlacCore::~AlacCore() = default;
void AlacCore::config(const Bytes& cookie) {
    require(cookie.size() == 36 && be32(cookie.data()) == 36 && be32(cookie.data() + 4) == alac_codec);
    const auto* p = cookie.data() + 12;
    frame_length = be32(p); bits = p[5]; channels = p[9]; rate = be32(p + 20);
    // Bound the allocation before handing hostile maximum-frame fields to FFmpeg.
    require(frame_length && frame_length <= 65536 && channels && channels <= 8 && rate &&
        (bits == 16 || bits == 20 || bits == 24 || bits == 32));
    bytes = WORD((bits + 7) / 8); require(uint64_t(rate) * channels * bytes <= MAXDWORD);
    auto next = std::make_unique<Impl>(); require(next->frame && next->packet, E_OUTOFMEMORY);
    const auto codec = avcodec_find_decoder(AV_CODEC_ID_ALAC); require(codec != nullptr, E_NOTIMPL);
    next->context = avcodec_alloc_context3(codec); require(next->context, E_OUTOFMEMORY);
    auto* c = next->context;
    c->extradata = static_cast<uint8_t*>(av_mallocz(cookie.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    require(c->extradata, E_OUTOFMEMORY); memcpy(c->extradata, cookie.data(), cookie.size()); c->extradata_size = int(cookie.size());
    c->thread_count = 1; c->err_recognition = AV_EF_CRCCHECK | AV_EF_BITSTREAM | AV_EF_EXPLODE;
    require(avcodec_open2(c, codec, nullptr) >= 0);
    require(c->sample_rate == int(rate) && c->ch_layout.nb_channels == int(channels) &&
        c->ch_layout.order == AV_CHANNEL_ORDER_NATIVE && c->ch_layout.u.mask <= MAXDWORD);
    channel_mask = DWORD(c->ch_layout.u.mask); impl_ = std::move(next);
}
Bytes AlacCore::decode(const Bytes& packet) {
    require(impl_ && !packet.empty() && packet.size() <= 64 * 1024 * 1024);
    auto& s = *impl_; av_packet_unref(s.packet); av_frame_unref(s.frame);
    require(av_new_packet(s.packet, int(packet.size())) >= 0, E_OUTOFMEMORY);
    memcpy(s.packet->data, packet.data(), packet.size());
    require(avcodec_send_packet(s.context, s.packet) >= 0);
    require(avcodec_receive_frame(s.context, s.frame) >= 0);
    const auto* f = s.frame;
    require(f->nb_samples > 0 && DWORD(f->nb_samples) <= frame_length && f->sample_rate == int(rate) &&
        f->ch_layout.nb_channels == int(channels) && f->ch_layout.order == AV_CHANNEL_ORDER_NATIVE && f->ch_layout.u.mask == channel_mask &&
        f->format == (bits == 16 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P));
    Bytes out(size_t(f->nb_samples) * channels * bytes);
    for (int frame = 0; frame < f->nb_samples; ++frame) for (DWORD channel = 0; channel < channels; ++channel) {
        uint32_t value{};
        if (bits == 16) { uint16_t sample{}; memcpy(&sample, f->extended_data[channel] + size_t(frame) * 2, 2); value = sample; }
        else { memcpy(&value, f->extended_data[channel] + size_t(frame) * 4, 4); value >>= (4 - bytes) * 8; }
        auto* dest = out.data() + (size_t(frame) * channels + channel) * bytes;
        for (WORD b = 0; b < bytes; ++b) dest[b] = BYTE(value >> (8 * b));
    }
    // FFmpeg already maps ALAC elements to its native Windows speaker order.
    require(avcodec_receive_frame(s.context, s.frame) == AVERROR(EAGAIN));
    return out;
}
}
