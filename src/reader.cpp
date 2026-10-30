#include "ttpcomm/audio_format_probe_impl.h"
#include "decoder.h"
#include "mp4.h"
#include "ttpcomm/pcm_format.h"

namespace ttp::aac {
class AudioReader final : public Reader,
                          public Metadata,
                          public Thumbnail,
                          public ReferenceDuration,
                          public AudioOffset,
                          public MetadataCommit, public ttp::format::InfoSupport {
    LONG references_{1};
    bool mp4_{}, writable_{}, opened_{}, live_{}, live_eof_{}, live_adif_{};
    bool dirty_{}, committed_{};
    HRESULT commit_result_{S_OK};
    Bytes live_input_;
    ComPtr<IStream> stream_;
    ComPtr<Metadata> content_;
    Mp4File movie_;
    AacCore raw_;
    std::uint64_t begin_{}, end_{}, raw_position_{};
    std::uint64_t raw_play_frame_{}, raw_seek_target_{};
    bool raw_seek_pending_{};
    std::vector<Sample> frames_;
    Bytes format_, pcm_;
    size_t sample_{}, pcm_position_{};
    DWORD duration_{}, bitrate_{}, buffer_size_{8192}, raw_core_rate_{};
    Picture picture_{sizeof(Picture), L"", L"", 0, nullptr, 3};
    std::wstring mime_;
    DWORD reference_duration_{};
    bool trimmed_pcm_{};
    struct Segment {
        uint64_t source{}, begin{}, end{};
        bool silent{};
    };
    std::vector<Segment> segments_;
    Bytes decoded_;
    uint64_t play_position_{}, play_length_{}, media_length_{};
    size_t decoded_sample_{SIZE_MAX};
    DWORD active_description_{}, movie_channel_mask_{};
    static uint64_t rescale(uint64_t ticks, DWORD from, DWORD to) {
        require(from && to);
        const auto whole = ticks / from, fraction = (ticks % from) * uint64_t(to) / from;
        require(whole <= (UINT64_MAX - fraction) / to);
        return whole * to + fraction;
    }
    int64_t presentation_frame(int64_t ticks) const {
        // Floor negative times too, so a partial priming packet ending after
        // time zero has an exact nonnegative skip into its decoded PCM.
        const auto magnitude = ticks < 0 ? uint64_t(-(ticks + 1)) + 1 : uint64_t(ticks);
        auto frames = rescale(magnitude, movie_.timescale, movie_.rate);
        if (ticks < 0 && (magnitude % movie_.timescale) * movie_.rate % movie_.timescale) ++frames;
        require(frames <= uint64_t(INT64_MAX));
        return ticks < 0 ? -int64_t(frames) : int64_t(frames);
    }
    void configure_movie(DWORD description) {
        require(description && description <= movie_.descriptions.size());
        raw_.config(movie_.descriptions[description - 1].asc);
        // Format discovery has already resolved implicit SBR/PS. Keep the
        // fixed output contract when a new AAC configuration starts warming.
        raw_.rate = movie_.rate;
        raw_.channels = BYTE(movie_.channels);
        raw_.channel_mask = movie_channel_mask_;
        active_description_ = description;
    }
    void inspect_movie() {
        if (movie_.samples.empty()) {
            AacCore inspect; inspect.config(movie_.asc);
            movie_.channels = inspect.channels; movie_.rate = inspect.rate;
            raw_.channel_mask = movie_channel_mask_ = inspect.channel_mask;
            return;
        }
        std::vector<bool> seen(movie_.descriptions.size());
        std::map<Bytes, bool> configurations;
        bool initial = true;
        for (size_t at = 0; at < movie_.samples.size(); ++at) {
            const auto id = movie_.samples[at].description;
            require(id && id <= seen.size());
            if (seen[id - 1]) continue;
            seen[id - 1] = true;
            const auto &description = movie_.descriptions[id - 1];
            if (configurations.find(description.configuration) != configurations.end()) continue;
            AacCore inspect;
            inspect.config(description.asc);
            bool produced = false;
            for (size_t i = at; i < movie_.samples.size() && i - at < 64; ++i) {
                const auto &sample = movie_.samples[i];
                if (!description.same_configuration(movie_.descriptions[sample.description - 1])) break;
                auto bytes = read_at(stream_.p, sample.offset, sample.size);
                DWORD used{};
                if (!inspect.decode(bytes.data(), DWORD(bytes.size()), used, true).empty()) {
                    produced = true;
                    break;
                }
            }
            // A changing configuration must establish its real layout before
            // publishing one WAVEFORMATEX to an unmodified legacy host.
            require(!movie_.configuration_changes || produced, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            if (initial) {
                movie_.channels = inspect.channels; movie_.rate = inspect.rate;
                raw_.channel_mask = movie_channel_mask_ = inspect.channel_mask;
                initial = false;
            } else {
                require(inspect.rate == movie_.rate && inspect.channels == movie_.channels &&
                        inspect.channel_mask == movie_channel_mask_, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            }
            configurations.emplace(description.configuration, true);
        }
    }
    void restart_trimmed() {
        configure_movie(movie_.samples.front().description);
        sample_ = 0; decoded_sample_ = SIZE_MAX; decoded_.clear();
    }
    bool next_trimmed() {
        pcm_.clear(); pcm_position_ = 0;
        const auto segment = std::lower_bound(segments_.begin(), segments_.end(), play_position_,
            [](const Segment& s, uint64_t position) { return s.end <= position; });
        if (segment == segments_.end()) return false;
        auto left = segment->end - play_position_;
        const auto source = segment->source + (play_position_ - segment->begin);
        if (segment->silent || source >= media_length_) {
            pcm_.assign(size_t(std::min<uint64_t>(left, 4096)) * wave().nBlockAlign, 0);
            return true;
        }
        require(source <= uint64_t(INT64_MAX));
        const auto packet = std::lower_bound(movie_.samples.begin(), movie_.samples.end(), int64_t(source),
            [&](const Sample& s, int64_t frame) { return presentation_frame(s.presentation_end()) <= frame; });
        require(packet != movie_.samples.end(), STG_E_READFAULT);
        const auto begin = presentation_frame(packet->presentation());
        if (begin > int64_t(source)) {
            left = std::min(left, uint64_t(begin) - source);
            pcm_.assign(size_t(std::min<uint64_t>(left, 4096)) * wave().nBlockAlign, 0);
            return true;
        }
        const auto wanted = size_t(packet - movie_.samples.begin());
        // Timestamp gaps do not reset AAC overlap. Backward/repeated edits
        // replay compressed packets, preserving the decoder's true history.
        if (decoded_sample_ != SIZE_MAX && wanted < decoded_sample_) restart_trimmed();
        while (decoded_sample_ != wanted) {
            require(sample_ < movie_.samples.size(), STG_E_READFAULT);
            const auto& s = movie_.samples[sample_];
            const bool changed = !movie_.descriptions[active_description_ - 1].same_configuration(
                movie_.descriptions[s.description - 1]);
            if (changed) configure_movie(s.description);
            else active_description_ = s.description;
            auto bytes = read_at(stream_.p, s.offset, s.size);
            DWORD used{};
            decoded_ = raw_.decode(bytes.data(), DWORD(bytes.size()), used);
            require(used == bytes.size());
            if (decoded_.empty()) {
                // FAAD suppresses the first overlap frame. If an edit selects
                // it, retain its media time as silence rather than shortening
                // the track or shifting all subsequent sample coordinates.
                require(sample_ == 0 || changed, STG_E_READFAULT);
                const auto frames = presentation_frame(s.presentation_end()) - presentation_frame(s.presentation());
                require(frames && frames <= 4096);
                decoded_.assign(size_t(frames) * wave().nBlockAlign, 0);
            }
            require(raw_.channel_mask == movie_channel_mask_, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            decoded_sample_ = sample_++;
        }
        const auto packet_end = presentation_frame(packet->presentation_end());
        const auto frames = std::min(left, uint64_t(packet_end) - source);
        require(frames > 0);
        const auto skip = begin < 0 ? source + uint64_t(-begin) : source - uint64_t(begin);
        require(skip <= decoded_.size() / wave().nBlockAlign &&
                frames <= decoded_.size() / wave().nBlockAlign - skip, STG_E_READFAULT);
        const auto offset = size_t(skip) * wave().nBlockAlign;
        pcm_.assign(decoded_.begin() + offset, decoded_.begin() + offset + size_t(frames) * wave().nBlockAlign);
        return true;
    }
    HRESULT read_trimmed(Buffer* target) {
        return protect([&]()->HRESULT{
            check(target->SetLength(0));DWORD capacity{},length{};BYTE* dest{};
            check(target->Capacity(&capacity));check(target->Data(&dest,&length));require(dest,E_POINTER);
            require(capacity>=wave().nBlockAlign,E_INVALIDARG);
            if(pcm_position_==pcm_.size()&&!next_trimmed())return S_FALSE;
            auto n=std::min<size_t>(capacity,pcm_.size()-pcm_position_);n-=n%wave().nBlockAlign;
            memcpy(dest,pcm_.data()+pcm_position_,n);pcm_position_+=n;play_position_+=n/wave().nBlockAlign;
            return target->SetLength(DWORD(n));
        });
    }
    const WAVEFORMATEX &wave() const { return *reinterpret_cast<const WAVEFORMATEX *>(format_.data()); }
    void float_format() {
        require(raw_.rate && raw_.channels && raw_.channels <= 64);
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
        require(initial.size() >= 2);
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
            raw_core_rate_ = expected_rate;
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
        // ADTS may signal PS only in the first audio frames. Discover the
        // output format before the host opens its device, retaining all PCM.
        for (unsigned attempt = 0; pcm_.empty() && raw_position_ < end_ && attempt < 64; ++attempt) {
            auto bytes = read_at(stream_.p, raw_position_, size_t(std::min<std::uint64_t>(65536, end_ - raw_position_)));
            DWORD used{};
            pcm_ = raw_.decode(bytes.data(), DWORD(bytes.size()), used, true);
            require(used > 0 && used <= bytes.size());
            raw_position_ += used;
        }
        float_format();
    }
    uint64_t seek_raw(uint64_t target, bool restart = false) {
        // Keep the intended destination if a host read is cancelled. A retry
        // reconstructs all predictive/SBR state before returning target PCM.
        raw_seek_target_ = target;
        raw_seek_pending_ = true;
        if (restart || target < raw_play_frame_) {
            pcm_.clear(); pcm_position_ = 0;
            auto initial = read_at(stream_.p, begin_,
                size_t(std::min<uint64_t>(65536, end_ - begin_)));
            raw_position_ = begin_ + raw_.raw(initial);
            raw_.channels = static_cast<unsigned char>(wave().nChannels);
            raw_.rate = wave().nSamplesPerSec;
            raw_play_frame_ = 0;
        }
        unsigned empty{};
        while (raw_play_frame_ < target) {
            if (pcm_position_ == pcm_.size()) {
                if (raw_position_ >= end_) break;
                auto input = read_at(stream_.p, raw_position_,
                    size_t(std::min<uint64_t>(65536, end_ - raw_position_)));
                DWORD used{};
                pcm_ = raw_.decode(input.data(), DWORD(input.size()), used);
                require(used > 0);
                raw_position_ += used; pcm_position_ = 0;
                if (pcm_.empty()) { require(++empty <= 64); continue; }
                empty = 0;
            }
            const auto take = std::min<uint64_t>(target - raw_play_frame_,
                (pcm_.size() - pcm_position_) / wave().nBlockAlign);
            require(take > 0);
            pcm_position_ += size_t(take) * wave().nBlockAlign;
            raw_play_frame_ += take;
        }
        raw_seek_pending_ = false;
        return raw_play_frame_;
    }
    void fill_live(size_t wanted) {
        require(wanted<=65536);
        while(!live_eof_ && live_input_.size()<wanted) {
            BYTE bytes[8192];ULONG got{};
            const auto count=static_cast<ULONG>(std::min(wanted-live_input_.size(),sizeof(bytes)));
            const HRESULT hr=stream_->Read(bytes,count,&got);require(got<=count);
            // A cancelled host read may already have consumed and returned
            // part of this packet. Keep those bytes before propagating the
            // failure so retrying a live, non-seekable stream cannot lose them.
            live_input_.insert(live_input_.end(),bytes,bytes+got);
            check(hr);
            if(hr==S_FALSE || !got) live_eof_=true;
        }
    }
    DWORD live_packet() {
        if (live_adif_) {
            // ADIF has no per-frame transport length. Supply the same bounded
            // lookahead as local raw decoding; only consume what FAAD reports.
            // Fill before decoding, so a cancelled short read preserves both
            // the compressed prefix and the decoder's previous frame state.
            fill_live(65536);
            return DWORD(live_input_.size());
        }
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
        fill_live(4);
        live_adif_=live_input_.size()>=4 && !memcmp(live_input_.data(),"ADIF",4);
        const auto n=live_packet();require(n>0);
        const DWORD used=raw_.raw(Bytes(live_input_.begin(),live_input_.begin()+n));
        require(used<=n);live_input_.erase(live_input_.begin(),live_input_.begin()+used);
        for (unsigned attempt = 0; pcm_.empty() && attempt < 64; ++attempt) {
            const auto count = live_packet();
            if (!count) break;
            DWORD consumed{};
            pcm_ = raw_.decode(live_input_.data(), count, consumed, true);
            require(consumed > 0 && consumed <= count);
            live_input_.erase(live_input_.begin(), live_input_.begin() + consumed);
        }
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
            if (!live_ && raw_seek_pending_) seek_raw(raw_seek_target_, true);
            if (pcm_position_ >= pcm_.size()) {
                if(live_) {
                    pcm_.clear();pcm_position_=0;
                    // Decoder priming can consume a packet without PCM.
                    for(unsigned attempts=0;pcm_.empty() && attempts<64;++attempts) {
                        const auto packet_bytes=live_packet();if(!packet_bytes) return S_FALSE;
                        DWORD used{};pcm_=raw_.decode(live_input_.data(),packet_bytes,used);
                        require(used>0 && used<=packet_bytes);live_input_.erase(live_input_.begin(),live_input_.begin()+used);
                    }
                    require(!pcm_.empty());
                } else {
                    pcm_.clear();pcm_position_=0;
                    // A reset decoder can consume its overlap/priming frame
                    // without producing PCM. Keep reading instead of exposing
                    // a successful zero-length buffer to the playback host.
                    for(unsigned attempts=0;pcm_.empty() && attempts<64;++attempts) {
                        if (raw_position_ >= end_) return S_FALSE;
                        auto input = read_at(stream_.p, raw_position_,
                            size_t(std::min<std::uint64_t>(65536, end_ - raw_position_)));
                        DWORD consumed{};
                        pcm_ = raw_.decode(input.data(), DWORD(input.size()), consumed);
                        require(consumed>0);
                        raw_position_ += consumed;
                    }
                    require(!pcm_.empty());
                }
            }
            size_t take = std::min<size_t>(capacity, pcm_.size() - pcm_position_);
            if (take)
                std::memcpy(dest, pcm_.data() + pcm_position_, take);
            pcm_position_ += take;
            if (!live_) raw_play_frame_ += take / wave().nBlockAlign;
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
            require(!committed_, E_UNEXPECTED);
            movie_.cover.assign(pic->data, pic->data + pic->bytes);
            dirty_ = true;
            return S_OK;
        });
    }

  public:
    explicit AudioReader(bool mp4) : mp4_(mp4) {}
    ~AudioReader() {
        if (mp4_ && FAILED(Commit()))
            OutputDebugStringW(L"ttp_aac: metadata save failed; original moov recovery attempted.\n");
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(iid, ttp::format::iid_info)) { *out=static_cast<ttp::format::SourceInfo*>(this); AddRef(); return S_OK; }

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
        else if (same(iid, iid_metadata_commit) && mp4_)
            *out = static_cast<MetadataCommit *>(this);
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
    HRESULT STDMETHODCALLTYPE Open(IStream* probe_stream, DWORD probe_flags) override {
        const auto probed = PrepareFormatInfo(probe_stream, mp4_ ? ttp::format::Mp4 : ttp::format::Aac);
        if (FAILED(probed)) return probed;
        const auto opened = OpenImpl(probe_stream, probe_flags);
        if (opened == S_OK) { SetSourceCodec(mp4_ && movie_.codec == WAVE_FORMAT_MPEGLAYER3 ? "mp3" : "aac"); }
        return CompleteFormatInfo(opened);
    }
    HRESULT OpenImpl(IStream *stream, DWORD flags) {
        if (!stream)
            return E_POINTER;
        return protect([&]() -> HRESULT {
            require(!opened_ && !committed_, E_UNEXPECTED);
            stream_ = ComPtr<IStream>(stream, true);
            STATSTG stat{};
            const HRESULT stat_result=stream->Stat(&stat, STATFLAG_NONAME);
            if(stat_result==E_PENDING || stat_result==E_NOTIMPL) {
                open_live();opened_=true;return S_OK;
            }
            check(stat_result);
            writable_ = !(flags & 1) && (stat.grfMode & 3) == STGM_READWRITE;
            if (mp4_) {
                movie_.open(stream);
                if (movie_.codec == aac_subtype.Data1) {
                    inspect_movie();
                    movie_.resolve_audio_clock(movie_.rate);
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
                // Fragmented files can update metadata in existing padding.
                // Commit reports insufficient space without moving media.
                if ((movie_.gapless || !movie_.edits.empty() || movie_.configuration_changes) &&
                    movie_.codec == aac_subtype.Data1) {
                    trimmed_pcm_ = true;
                    const auto& last = movie_.samples.back();
                    const auto media_end = presentation_frame(last.presentation_end());
                    media_length_ = media_end > 0 ? uint64_t(media_end) : 0;
                    if (movie_.gapless) {
                        const auto begin = rescale(movie_.trim_begin, movie_.trim_begin_scale, movie_.rate);
                        play_length_ = rescale(movie_.trim_length, movie_.trim_length_scale, movie_.rate);
                        require(play_length_ && begin <= media_length_ && play_length_ <= media_length_ - begin);
                        segments_.push_back({begin, 0, play_length_, false});
                    } else if (movie_.edits.empty()) {
                        const auto begin = movie_.seek_bias > 0 ? uint64_t(presentation_frame(movie_.seek_bias)) : 0;
                        require(begin < media_length_);
                        play_length_ = media_length_ - begin;
                        segments_.push_back({begin, 0, play_length_, false});
                    } else {
                        uint64_t ticks{};
                        for (const auto& edit : movie_.edits) {
                            require(edit.length <= UINT64_MAX - ticks);
                            const auto begin = rescale(edit.begin, movie_.timescale, movie_.rate);
                            ticks += edit.length;
                            const auto end = edit.to_end ? media_length_ - begin
                                : rescale(ticks, movie_.edit_scale, movie_.rate);
                            require(edit.empty || (begin <= media_length_ && end - play_length_ <= UINT64_MAX - begin));
                            if (end != play_length_)
                                segments_.push_back({begin, play_length_, end, edit.empty});
                            play_length_ = end;
                        }
                        require(play_length_ > 0);
                    }
                    restart_trimmed(); float_format(); duration_ = milliseconds(play_length_, movie_.rate);
                }
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
        *out = duration_ ? duration_ : reference_duration_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (format_.empty())
            return E_UNEXPECTED;
        if(!mp4_ || trimmed_pcm_)return ttp::pcm::copy_format(wave(),raw_.channel_mask,32,out);
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
        *out = mp4_ && !trimmed_pcm_ ? 16 : 32;
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
        if (committed_) return E_UNEXPECTED;
        if (!target)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        if(trimmed_pcm_)return read_trimmed(target);
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
        if (committed_) return E_UNEXPECTED;
        if (!position)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        if(live_) return E_NOTIMPL;
        return protect([&]() -> HRESULT {
            if(trimmed_pcm_) {
                play_position_ = std::min<uint64_t>(uint64_t(*position) * movie_.rate / 1000, play_length_);
                pcm_.clear(); pcm_position_ = 0;
                *position=milliseconds(play_position_,movie_.rate);return S_OK;
            }
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
            uint64_t target = uint64_t(*position) * wave().nSamplesPerSec / 1000;
            if (!frames_.empty()) {
                const DWORD sr = raw_core_rate_;
                const auto core_target = std::uint64_t(*position) * sr / 1000;
                auto it = std::upper_bound(frames_.begin(), frames_.end(), core_target,
                                           [](auto t, const Sample &s) { return t < s.time; });
                if (it != frames_.begin())
                    --it;
                // Preserve ADTS frame-granular positioning, but map it onto
                // the same PCM origin as sequential playback. Resetting FAAD
                // and decoding one prior packet does not rebuild its history.
                target = rescale(it->time, sr, wave().nSamplesPerSec);
            }
            *position = dword(seek_raw(target, raw_seek_pending_) * 1000 / wave().nSamplesPerSec);
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
        if (committed_)
            return E_UNEXPECTED;
        if (!mp4_)
            return content_ ? content_->Set(name, value) : E_NOINTERFACE;
        return protect([&]() -> HRESULT {
            movie_.set_tag(name, value ? value : L"");
            dirty_ = true;
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
        if (committed_)
            return E_UNEXPECTED;
        return protect([&]() -> HRESULT {
            movie_.cover.clear();
            dirty_ = true;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (committed_) return commit_result_;
        committed_ = true;
        commit_result_ = protect([&]() -> HRESULT {
            if (dirty_) {
                require(mp4_ && writable_ && stream_, E_ACCESSDENIED);
                movie_.save(stream_.p);
                dirty_ = false;
            }
            return S_OK;
        });
        return commit_result_;
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
