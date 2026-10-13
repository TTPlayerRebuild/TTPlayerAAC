#include "decoder.h"
#include "mp4.h"

namespace ttp::aac {
class AudioReader final : public Reader,
                          public Metadata,
                          public Thumbnail,
                          public ReferenceDuration,
                          public AudioOffset {
    LONG references_{1};
    bool mp4_{}, writable_{}, opened_{}, live_{}, live_eof_{};
    Bytes live_input_;
    ComPtr<IStream> stream_;
    ComPtr<Metadata> content_;
    Mp4File movie_;
    AacCore raw_;
    std::uint64_t begin_{}, end_{}, raw_position_{};
    std::vector<Sample> frames_;
    Bytes format_, pcm_;
    size_t sample_{}, pcm_position_{};
    DWORD duration_{}, bitrate_{}, buffer_size_{8192};
    Picture picture_{sizeof(Picture), L"", L"", 0, nullptr, 3};
    std::wstring mime_;
    DWORD reference_duration_{};
    const WAVEFORMATEX &wave() const { return *reinterpret_cast<const WAVEFORMATEX *>(format_.data()); }
    void float_format() {
        format_.assign(sizeof(WAVEFORMATEX), 0);
        auto *f = reinterpret_cast<WAVEFORMATEX *>(format_.data());
        f->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        f->nChannels = raw_.channels;
        f->nSamplesPerSec = raw_.rate;
        f->wBitsPerSample = 32;
        f->nBlockAlign = WORD(f->nChannels * 4);
        f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        buffer_size_ = raw_.frame_length * f->nBlockAlign;
    }
    void open_raw() {
        begin_ = 0;
        end_ = stream_size(stream_.p);
        seek(stream_.p, 0);
        if (auto create = standard_content()) {
            ULONGLONG bytes{};
            if (SUCCEEDED(create(stream_.p, 4, content_.put(), &bytes)) && content_) {
                begin_ = tell(stream_.p);
                require(begin_ <= end_ && bytes <= end_ - begin_);
                end_ = begin_ + bytes;
            }
        }
        if (!content_ && end_ >= 10) {
            auto h = read_at(stream_.p, 0, 10);
            if (!std::memcmp(h.data(), "ID3", 3)) {
                require((h[6] | h[7] | h[8] | h[9]) < 128);
                begin_ = 10 + (DWORD(h[6]) << 21) + (DWORD(h[7]) << 14) + (DWORD(h[8]) << 7) + h[9];
                if (h[5] & 16)
                    begin_ += 10;
            }
        }
        require(begin_ < end_);
        auto initial = read_at(stream_.p, begin_, size_t(std::min<std::uint64_t>(65536, end_ - begin_)));
        DWORD skip = raw_.raw(initial);
        raw_position_ = begin_ + skip;
        if (initial[0] == 0xff && (initial[1] & 0xf6) == 0xf0) {
            std::uint64_t pos = begin_, time = 0;
            DWORD expected_rate{};
            constexpr DWORD rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                       22050, 16000, 12000, 11025, 8000,  7350};
            while (pos < end_) {
                if (end_ - pos < 7)
                    break;
                auto h = read_at(stream_.p, pos, 7);
                // Trailing ID3/APEv2 is handled by CreateStdContent. A standalone
                // host may omit that export; stop only at a known tag signature.
                if (!std::memcmp(h.data(), "TAG", 3) || !std::memcmp(h.data(), "APETAGE", 7))
                    break;
                require(h[0] == 255 && (h[1] & 0xf6) == 0xf0);
                DWORD index = (h[2] >> 2) & 15;
                require(index < 13);
                DWORD sr = rates[index];
                require(!expected_rate || sr == expected_rate);
                expected_rate = sr;
                DWORD n = ((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5);
                require(n >= ((h[1] & 1) ? 7U : 9U) && n <= end_ - pos);
                DWORD samples = 1024 * ((h[6] & 3) + 1);
                require(frames_.size() < 10000000);
                frames_.push_back({pos, time, n, samples, 0});
                time += samples;
                pos += n;
            }
            require(!frames_.empty());
            end_ = pos;
            duration_ = dword((time * 1000 + expected_rate / 2) / expected_rate);
            bitrate_ = duration_ ? dword((end_ - begin_) * 8000 / duration_) : 0;
        } else if (initial.size() >= 8 && !std::memcmp(initial.data(), "ADIF", 4)) {
            size_t p = 4 + ((initial[4] & 128) ? 9 : 0);
            require(initial.size() >= p + 4);
            bitrate_ = ((DWORD(initial[p] & 15) << 19) | (DWORD(initial[p + 1]) << 11) |
                        (DWORD(initial[p + 2]) << 3) | (initial[p + 3] >> 5));
            duration_ = bitrate_ ? dword((end_ - begin_) * 8000 / bitrate_) : 1000;
        } else
            require(false, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        float_format();
    }
    void fill_live(size_t wanted) {
        require(wanted<=65536);
        while(!live_eof_ && live_input_.size()<wanted) {
            BYTE bytes[8192];ULONG got{};
            const auto count=static_cast<ULONG>(std::min(wanted-live_input_.size(),sizeof(bytes)));
            const HRESULT hr=stream_->Read(bytes,count,&got);check(hr);require(got<=count);
            live_input_.insert(live_input_.end(),bytes,bytes+got);
            if(hr==S_FALSE || !got) live_eof_=true;
        }
    }
    DWORD live_packet() {
        fill_live(7);
        if(live_input_.empty()) return 0;
        require(live_input_.size()>=7,HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
        const auto* h=live_input_.data();
        require(h[0]==255 && (h[1]&0xf6)==0xf0);
        const DWORD n=((h[3]&3)<<11)|(h[4]<<3)|(h[5]>>5);
        require(n>=((h[1]&1)?7u:9u));
        fill_live(n);require(live_input_.size()>=n,HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
        return n;
    }
    void open_live() {
        require(!mp4_,E_NOTIMPL);
        live_=true;fill_live(10);
        if(live_input_.size()>=10 && !memcmp(live_input_.data(),"ID3",3)) {
            const auto& h=live_input_;
            require(((h[6]|h[7]|h[8]|h[9])&128)==0);
            size_t skip=10+(size_t(h[6])<<21)+(size_t(h[7])<<14)+(size_t(h[8])<<7)+h[9];
            if(h[3]==4 && (h[5]&16)) skip+=10;
            require(skip<=16*1024*1024);
            while(skip) {
                const auto n=std::min(skip,live_input_.size());
                live_input_.erase(live_input_.begin(),live_input_.begin()+n);skip-=n;
                if(skip) {fill_live(std::min<size_t>(skip,8192));require(!live_input_.empty(),HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));}
            }
        }
        const auto n=live_packet();require(n>0);
        const DWORD used=raw_.raw(Bytes(live_input_.begin(),live_input_.begin()+n));
        require(used<=n);live_input_.erase(live_input_.begin(),live_input_.begin()+used);
        float_format();duration_=0;bitrate_=0;
    }
    HRESULT read_raw(Buffer *target) {
        return protect([&]() -> HRESULT {
            DWORD capacity{}, n{};
            BYTE *dest{};
            check(target->Capacity(&capacity));
            check(target->Data(&dest, &n));
            require(dest || !capacity, E_POINTER);
            check(target->SetLength(0));
            capacity -= capacity % wave().nBlockAlign;
            if (!capacity)
                return E_INVALIDARG;
            if (pcm_position_ >= pcm_.size()) {
                if(live_) {
                    pcm_.clear();pcm_position_=0;
                    // Decoder priming can consume a packet without PCM.
                    for(unsigned attempts=0;pcm_.empty() && attempts<64;++attempts) {
                        const auto n=live_packet();if(!n) return S_FALSE;
                        DWORD used{};pcm_=raw_.decode(live_input_.data(),n,used);
                        require(used>0 && used<=n);live_input_.erase(live_input_.begin(),live_input_.begin()+used);
                    }
                    require(!pcm_.empty());
                } else {
                if (raw_position_ >= end_)
                    return S_FALSE;
                auto input = read_at(stream_.p, raw_position_,
                                     size_t(std::min<std::uint64_t>(65536, end_ - raw_position_)));
                DWORD consumed{};
                pcm_ = raw_.decode(input.data(), DWORD(input.size()), consumed);
                require(consumed || !pcm_.empty());
                raw_position_ += consumed;
                pcm_position_ = 0;
                }
            }
            size_t take = std::min<size_t>(capacity, pcm_.size() - pcm_position_);
            if (take)
                std::memcpy(dest, pcm_.data() + pcm_position_, take);
            pcm_position_ += take;
            return target->SetLength(DWORD(take));
        });
    }
    HRESULT store_picture(const Picture *pic) {
        if (!pic)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            require(mp4_ && writable_, E_ACCESSDENIED);
            require(pic->size >= sizeof(Picture) && pic->data && pic->bytes && pic->bytes <= 60000,
                    E_INVALIDARG);
            Bytes previous = movie_.cover;
            movie_.cover.assign(pic->data, pic->data + pic->bytes);
            try {
                movie_.save(stream_.p);
            } catch (...) {
                movie_.cover = std::move(previous);
                throw;
            }
            return S_OK;
        });
    }

  public:
    explicit AudioReader(bool mp4) : mp4_(mp4) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (same(iid, IID_IUnknown) || same(iid, iid_reader))
            *out = static_cast<Reader *>(this);
        else if (same(iid, iid_metadata) && (mp4_ || content_))
            *out = static_cast<Metadata *>(this);
        else if (same(iid, iid_thumbnail) && mp4_)
            *out = static_cast<Thumbnail *>(this);
        else if (same(iid, iid_reference_duration) && !mp4_)
            *out = static_cast<ReferenceDuration *>(this);
        else if (same(iid, iid_audio_offset) && mp4_)
            *out = static_cast<AudioOffset *>(this);
        else
            return E_NOINTERFACE;
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
    HRESULT STDMETHODCALLTYPE Open(IStream *stream, DWORD) override {
        if (!stream)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            require(!opened_, E_UNEXPECTED);
            stream_ = ComPtr<IStream>(stream, true);
            STATSTG stat{};
            const HRESULT stat_result=stream->Stat(&stat, STATFLAG_NONAME);
            if(stat_result==E_PENDING || stat_result==E_NOTIMPL) {
                open_live();opened_=true;return S_OK;
            }
            check(stat_result);
            writable_ = (stat.grfMode & 3) == STGM_READWRITE;
            if (mp4_) {
                movie_.open(stream);
                if (movie_.codec == aac_subtype.Data1) {
                    AacCore inspect;
                    inspect.config(movie_.asc);
                }
                format_.assign(movie_.asc.empty() ? sizeof(WAVEFORMATEX)
                                                  : sizeof(WAVEFORMATEXTENSIBLE) + movie_.asc.size(),
                               0);
                auto *f = reinterpret_cast<WAVEFORMATEX *>(format_.data());
                f->wFormatTag = movie_.asc.empty() ? WORD(movie_.codec) : WAVE_FORMAT_EXTENSIBLE;
                f->nChannels = WORD(movie_.channels);
                f->nSamplesPerSec = movie_.rate;
                f->nAvgBytesPerSec = movie_.bitrate / 8;
                f->nBlockAlign = 1;
                f->wBitsPerSample = 16;
                if (!movie_.asc.empty()) {
                    f->cbSize = WORD(22 + movie_.asc.size());
                    auto *ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(f);
                    ext->SubFormat = aac_subtype;
                    ext->SubFormat.Data1 = movie_.codec;
                    std::copy(movie_.asc.begin(), movie_.asc.end(),
                              format_.begin() + sizeof(WAVEFORMATEXTENSIBLE));
                }
                duration_ = movie_.duration_ms;
                bitrate_ = movie_.bitrate;
                buffer_size_ = std::max<DWORD>(movie_.max_packet, 1024);
                writable_ = writable_ && !movie_.fragmented;
            } else
                open_raw();
            opened_ = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = live_ ? 1 : 2 | (writable_ ? 4 : 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Duration(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = duration_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (format_.empty())
            return E_UNEXPECTED;
        auto *p = CoTaskMemAlloc(format_.size());
        if (!p)
            return E_OUTOFMEMORY;
        std::memcpy(p, format_.data(), format_.size());
        *out = static_cast<WAVEFORMATEX *>(p);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = buffer_size_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **out) override {
        return text(mp4_ && movie_.codec == WAVE_FORMAT_MPEGLAYER3 ? L"MP3|MPEG Layer-3"
                                                                   : L"AAC|Advanced Audio Coding",
                    out);
    }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = bitrate_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *out) override {
        if (!out)
            return E_POINTER;
        *out = mp4_ ? 16 : 32;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *callback) override {
        // Original 60002224 queries the optional host stream extension and
        // forwards the callback through slot 4; reader methods ignore its HRESULT.
        constexpr GUID stream_extension{
            0x1717a4a7, 0xa3dc, 0x416b, {0xa2, 0x97, 0x72, 0x16, 0x74, 0x9b, 0xd7, 0xbf}};
        ComPtr<IUnknown> extension;
        if (stream_ && SUCCEEDED(stream_->QueryInterface(stream_extension,
                                                         reinterpret_cast<void **>(extension.put())))) {
            using Set = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
            auto **table = *reinterpret_cast<void ***>(extension.p);
            reinterpret_cast<Set>(table[4])(extension.p, callback);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Read(Buffer *target) override {
        if (!target)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        if (!mp4_)
            return read_raw(target);
        return protect([&]() -> HRESULT {
            check(target->SetLength(0));
            if (sample_ >= movie_.samples.size())
                return S_FALSE;
            auto s = movie_.samples[sample_];
            DWORD capacity{}, length{};
            BYTE *p{};
            check(target->Capacity(&capacity));
            check(target->Data(&p, &length));
            require(p && capacity >= s.size, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
            seek(stream_.p, s.offset);
            read_exact(stream_.p, p, s.size);
            check(target->SetLength(s.size));
            ++sample_;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *position) override {
        if (!position)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        if(live_) return E_NOTIMPL;
        return protect([&]() -> HRESULT {
            if (mp4_) {
                auto time = std::uint64_t(std::max<std::int64_t>(
                    0, std::int64_t(std::uint64_t(*position) * movie_.timescale / 1000) + movie_.seek_bias));
                auto &list = movie_.samples;
                if (list.empty())
                    return E_FAIL;
                auto it = std::upper_bound(list.begin(), list.end(), time,
                                           [](auto t, const Sample &s) { return t < s.time; });
                if (it != list.begin())
                    --it;
                sample_ = size_t(it - list.begin());
                *position = milliseconds(it->time, movie_.timescale);
                return S_OK;
            }
            raw_.reset();
            pcm_.clear();
            pcm_position_ = 0;
            if (!frames_.empty()) {
                auto h = read_at(stream_.p, begin_, 7);
                constexpr DWORD rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                           22050, 16000, 12000, 11025, 8000,  7350};
                DWORD sr = rates[(h[2] >> 2) & 15];
                auto target = std::uint64_t(*position) * sr / 1000;
                auto it = std::upper_bound(frames_.begin(), frames_.end(), target,
                                           [](auto t, const Sample &s) { return t < s.time; });
                if (it != frames_.begin())
                    --it;
                raw_position_ = it->offset;
                *position = dword(it->time * 1000 / sr);
                if (it != frames_.begin()) {
                    auto previous = it - 1;
                    auto input = read_at(stream_.p, previous->offset, previous->size);
                    DWORD consumed{};
                    raw_.decode(input.data(), DWORD(input.size()), consumed);
                }
            } else {
                auto initial =
                    read_at(stream_.p, begin_, size_t(std::min<std::uint64_t>(65536, end_ - begin_)));
                raw_position_ = begin_ + raw_.raw(initial);
                std::uint64_t target = std::uint64_t(*position) * raw_.rate / 1000, decoded{};
                while (decoded < target && raw_position_ < end_) {
                    auto input = read_at(stream_.p, raw_position_,
                                         size_t(std::min<std::uint64_t>(65536, end_ - raw_position_)));
                    DWORD used{};
                    pcm_ = raw_.decode(input.data(), DWORD(input.size()), used);
                    require(used);
                    raw_position_ += used;
                    auto frames = pcm_.size() / wave().nBlockAlign;
                    if (frames > target - decoded) {
                        pcm_position_ = size_t(target - decoded) * wave().nBlockAlign;
                        decoded = target;
                        break;
                    }
                    decoded += frames;
                    pcm_.clear();
                }
                *position = dword(decoded * 1000 / raw_.rate);
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *out) override {
        if (!out)
            return E_POINTER;
        if (!mp4_)
            return content_ ? content_->Count(out) : E_NOINTERFACE;
        *out = DWORD(movie_.tags.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE At(DWORD index, wchar_t **name, wchar_t **value) override {
        if (!name || !value)
            return E_POINTER;
        *name = *value = nullptr;
        if (!mp4_)
            return content_ ? content_->At(index, name, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            require(index < movie_.tags.size(), E_INVALIDARG);
            check(text(wide(movie_.tags[index].name), name));
            HRESULT hr = text(movie_.tags[index].value, value);
            if (FAILED(hr)) {
                CoTaskMemFree(*name);
                *name = nullptr;
            }
            return hr;
        });
    }
    HRESULT STDMETHODCALLTYPE Get(const char *name, wchar_t **value) override {
        if (!name || !value)
            return E_POINTER;
        *value = nullptr;
        if (!mp4_)
            return content_ ? content_->Get(name, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            auto *t = movie_.tag(name);
            return t ? text(t->value, value) : E_INVALIDARG;
        });
    }
    HRESULT STDMETHODCALLTYPE Set(const char *name, const wchar_t *value) override {
        if (!name)
            return E_POINTER;
        if (!writable_)
            return E_ACCESSDENIED;
        if (!mp4_)
            return content_ ? content_->Set(name, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            auto backup = movie_.tags;
            movie_.set_tag(name, value ? value : L"");
            try {
                movie_.save(stream_.p);
            } catch (...) {
                movie_.tags = std::move(backup);
                throw;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = movie_.cover.empty() ? 0 : 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 60000;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD *out) override {
        if (!out)
            return E_POINTER;
        *out = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD index, const Picture **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (index || movie_.cover.empty())
            return E_FAIL;
        picture_.bytes = DWORD(movie_.cover.size());
        picture_.data = movie_.cover.data();
        picture_.mime =
            movie_.cover.size() >= 4 && movie_.cover[0] == 137 ? L"image/png" : L"image/jpeg";
        *out = &picture_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD index, const Picture *pic, DWORD mask) override {
        if (index || movie_.cover.empty())
            return E_FAIL;
        return (mask & 8) ? store_picture(pic) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture *pic) override {
        if (!movie_.cover.empty())
            return E_FAIL;
        return store_picture(pic);
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD index) override {
        if (index || movie_.cover.empty())
            return E_FAIL;
        if (!writable_)
            return E_ACCESSDENIED;
        return protect([&]() -> HRESULT {
            auto previous = std::move(movie_.cover);
            try {
                movie_.save(stream_.p);
            } catch (...) {
                movie_.cover = std::move(previous);
                throw;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetReferenceDuration(DWORD value) override {
        reference_duration_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAudioOffset(DWORD *value) override {
        if (!value)
            return E_POINTER;
        *value = movie_.audio_offset;
        return S_OK;
    }
};
HRESULT make_reader(bool mp4, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&]() -> HRESULT {
        *out = static_cast<Reader *>(new AudioReader(mp4));
        return S_OK;
    });
}
} // namespace ttp::aac
