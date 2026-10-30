#include "decoder.h"
#include <cmath>

namespace ttp::aac {
void AacCore::create() {
    close();
    handle_ = NeAACDecOpen();
    require(handle_ != nullptr, E_OUTOFMEMORY);
    auto *cfg = NeAACDecGetCurrentConfiguration(handle_);
    cfg->outputFormat = FAAD_FMT_FLOAT;
    require(NeAACDecSetConfiguration(handle_, cfg) != 0, E_FAIL);
}
void AacCore::config(const Bytes &asc) {
    require(asc.size() >= 2 && asc.size() <= 65513);
    create();
    require(NeAACDecInit2(handle_, const_cast<BYTE *>(asc.data()),
                          static_cast<unsigned long>(asc.size()), &rate, &channels) >= 0);
    mp4AudioSpecificConfig info{};
    require(NeAACDecAudioSpecificConfig(const_cast<BYTE *>(asc.data()),
                                        static_cast<unsigned long>(asc.size()), &info) >= 0);
    // FAAD2 2.11.4 correctly reports the AAC core channel count at Init2.
    // Explicit Parametric Stereo expands that mono core to stereo output.
    if (channels == 1 && info.ps_present_flag == 1)
        channels = 2;
    // Original 600076E0's sixth argument reports frameLength, including LD/SBR.
    frame_length = info.frameLengthFlag ? 960 : 1024;
    if (info.objectTypeIndex == LD)
        frame_length /= 2;
    if ((info.sbr_present_flag == 1 && !info.downSampledSBR) || info.forceUpSampling)
        frame_length *= 2;
    require(rate && channels && channels <= 64);
}
DWORD AacCore::raw(const Bytes &initial) {
    require(initial.size() >= 4);
    create();
    long used = NeAACDecInit(handle_, const_cast<BYTE *>(initial.data()),
                             static_cast<unsigned long>(initial.size()), &rate, &channels);
    require(used >= 0 && static_cast<size_t>(used) <= initial.size());
    // ADTS channel_configuration zero carries its layout in an in-band PCE.
    // Init has not decoded that element yet; discover_format resolves it from
    // the first PCM frame before the Reader publishes a WAVEFORMATEX.
    const bool pending_pce = initial[0] == 0xff && (initial[1] & 0xf6) == 0xf0 &&
        !(initial[2] & 1) && !(initial[3] & 0xc0);
    require(rate && channels <= 64 && (channels || pending_pce));
    return DWORD(used);
}
Bytes AacCore::decode(const BYTE *data, DWORD bytes, DWORD &consumed, bool discover_format) {
    require(handle_ && data && bytes);
    NeAACDecFrameInfo info{};
    auto *pcm = NeAACDecDecode(handle_, &info, const_cast<BYTE *>(data), bytes);
    consumed = info.bytesconsumed;
    require(info.error == 0 && consumed <= bytes);
    require(info.samples <= 64 * 4096 && (!info.samples || pcm));
    if (info.samples) {
        require(info.channels && info.channels <= 64 && info.samplerate);
        if (discover_format) {
            rate = info.samplerate;
            channels = info.channels;
        }
        require(info.samplerate == rate && info.channels == channels);
        frame_length = info.samples / channels;
    }
    Bytes result(info.samples * sizeof(float));
    if (!result.empty()) {
        // FAAD reports MPEG order. Sort by the Windows speaker bits before
        // exposing PCM, including PCE layouts rather than guessing by count.
        constexpr DWORD bits[]{0,4,1,2,0x200,0x400,0x10,0x20,0x100,8};
        std::vector<std::pair<DWORD,unsigned>> order;
        DWORD mask{};
        for (unsigned c = 0; c < info.channels; ++c) {
            const auto pos = info.channel_position[c];
            require(pos > 0 && pos < std::size(bits), E_NOTIMPL);
            DWORD bit = bits[pos];
            // A PCE with four/five front channels places the inner pair
            // before FL/FR. FAAD labels both pairs FL/FR; recover FLC/FRC
            // from the front count instead of rejecting valid 7.1-wide PCM.
            const unsigned first_pair = info.num_front_channels & 1;
            if ((info.num_front_channels == 4 || info.num_front_channels == 5) &&
                c >= first_pair && c < first_pair + 2 && (pos == 2 || pos == 3))
                bit = pos == 2 ? SPEAKER_FRONT_LEFT_OF_CENTER : SPEAKER_FRONT_RIGHT_OF_CENTER;
            require(!(mask & bit), E_NOTIMPL);
            mask |= bit;
            order.push_back({bit, c});
        }
        std::sort(order.begin(),order.end());channel_mask=mask;
        const auto* source=static_cast<const float*>(pcm);auto* dest=reinterpret_cast<float*>(result.data());
        for(size_t i=0;i<info.samples;i+=info.channels)for(unsigned c=0;c<info.channels;++c)dest[i+c]=source[i+order[c].second];
    }
    return result;
}

class AacDecoder final : public Decoder {
    LONG references_{1};
    AacCore core_;
    Bytes input_, pcm_, configuration_;
    size_t offset_{};
    DWORD input_size_{1024}, output_size_{8192};
    bool ready_{};
    bool stereo_ps_{};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_decoder))
            return E_NOINTERFACE;
        *out = static_cast<Decoder *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Initialize(const WAVEFORMATEX *in, WAVEFORMATEX *out) override {
        if (!in || !out)
            return E_POINTER;
        // The original public ABI permits Initialize(format, format).
        // Capture the header before clearing the (possibly aliased) output.
        const WAVEFORMATEX input_format = *in;
        ready_ = false;
        stereo_ps_ = false;
        input_.clear();
        pcm_.clear();
        configuration_.clear();
        offset_ = 0;
        core_.close();
        *out = {};
        return protect([&]() -> HRESULT {
            require(input_format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && input_format.cbSize >= 2, E_INVALIDARG);
            auto *p = reinterpret_cast<const BYTE *>(in) + sizeof(WAVEFORMATEX);
            DWORD n = input_format.cbSize;
            if (n >= 24) {
                GUID type{};
                std::memcpy(&type, reinterpret_cast<const BYTE *>(in) + 24, 16);
                require(same(type, aac_subtype), E_INVALIDARG);
                p += 22;
                n -= 22;
            }
            configuration_.assign(p, p + n);
            core_.config(configuration_);
            // The MP4 Reader probes implicit PS before publishing its format.
            stereo_ps_ = core_.channels == 1 && input_format.nChannels == 2;
            if (stereo_ps_)
                core_.channels = 2;
            input_size_ = std::max<DWORD>(1024, input_format.nAvgBytesPerSec >> 4);
            output_size_ = core_.frame_length * core_.channels * 4;
            *out = {};
            out->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            out->nChannels = core_.channels;
            out->nSamplesPerSec = core_.rate;
            out->wBitsPerSample = 32;
            out->nBlockAlign = WORD(core_.channels * 4);
            out->nAvgBytesPerSec = out->nSamplesPerSec * out->nBlockAlign;
            input_.clear();
            pcm_.clear();
            offset_ = 0;
            ready_ = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE BufferSizes(DWORD *in, DWORD *out) override {
        if (in)
            *in = input_size_;
        if (out)
            *out = output_size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NeedsInput() override {
        return input_.empty() && offset_ >= pcm_.size() ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OutputAvailable() override {
        return !input_.empty() || offset_ < pcm_.size() ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Input(Buffer *buffer) override {
        if (!buffer)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            require(ready_, E_UNEXPECTED);
            require(NeedsInput() == S_OK, E_UNEXPECTED);
            BYTE *p{};
            DWORD n{};
            check(buffer->Data(&p, &n));
            require(p && n && n <= 64 * 1024 * 1024, E_INVALIDARG);
            input_.assign(p, p + n);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Output(Buffer *buffer) override {
        if (!buffer)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            check(buffer->SetLength(0));
            require(ready_, E_UNEXPECTED);
            if (!input_.empty()) {
                DWORD consumed{};
                pcm_ = core_.decode(input_.data(), DWORD(input_.size()), consumed);
                require(consumed > 0);
                input_.clear();
                offset_ = 0;
            }
            BYTE *p{};
            DWORD n{}, capacity{};
            check(buffer->Capacity(&capacity));
            check(buffer->Data(&p, &n));
            require(offset_ >= pcm_.size() || capacity >= DWORD(core_.channels) * 4,
                    HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
            size_t count = std::min<size_t>(capacity, pcm_.size() - offset_);
            count -= count % (core_.channels * 4);
            require(!count || p, E_POINTER);
            if (count)
                std::memcpy(p, pcm_.data() + offset_, count);
            offset_ += count;
            return buffer->SetLength(DWORD(count));
        });
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        input_.clear();
        pcm_.clear();
        offset_ = 0;
        if (!ready_)
            return S_OK;
        ready_ = false;
        return protect([&]() -> HRESULT {
            // PostSeekReset only signals a discontinuity to FAAD. A fresh
            // packet sequence must also discard prediction, overlap, PNS,
            // coupling and SSR histories, just like a newly created decoder.
            core_.config(configuration_);
            if (stereo_ps_)
                core_.channels = 2;
            ready_ = true;
            return S_OK;
        });
    }
};
HRESULT make_decoder(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&]() -> HRESULT {
        *out = static_cast<Decoder *>(new AacDecoder);
        return S_OK;
    });
}
} // namespace ttp::aac
