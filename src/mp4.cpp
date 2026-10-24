#include "mp4.h"
#include "genres.h"
#include <array>
#include <functional>

namespace ttp::aac {
namespace {
constexpr size_t max_samples = 10000000;
constexpr DWORD cc(const char *s) {
    return fourcc(s[0], s[1], s[2], s[3]);
}
struct Box {
    size_t at{}, size{}, head{};
    DWORD type{};
    size_t body() const { return at + head; }
    size_t end() const { return at + size; }
    explicit operator bool() const { return size != 0; }
};
std::vector<Box> boxes(const Bytes &b, size_t from, size_t end) {
    require(from <= end && end <= b.size());
    std::vector<Box> out;
    while (from < end) {
        require(end - from >= 8);
        std::uint64_t n = be32(b.data() + from);
        size_t h = 8;
        if (n == 1) {
            require(end - from >= 16);
            n = be64(b.data() + from + 8);
            h = 16;
        } else if (n == 0)
            n = end - from;
        require(n >= h && n <= end - from);
        out.push_back({from, size_t(n), h, be32(b.data() + from + 4)});
        from += size_t(n);
        require(out.size() <= max_samples);
    }
    return out;
}
Box find(const Bytes &b, size_t from, size_t end, DWORD type) {
    for (auto x : boxes(b, from, end))
        if (x.type == type)
            return x;
    return {};
}
Box child(const Bytes &b, Box parent, DWORD type, size_t skip = 0) {
    return parent ? find(b, parent.body() + skip, parent.end(), type) : Box{};
}
const BYTE *data(const Bytes &b, Box x, size_t required, size_t offset = 0) {
    require(x && offset <= x.size - x.head && required <= x.size - x.head - offset);
    return b.data() + x.body() + offset;
}
Bytes slice(const Bytes &b, Box x) {
    require(bool(x));
    return Bytes(b.begin() + x.at, b.begin() + x.end());
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    require(a <= UINT64_MAX - b);
    return a + b;
}
std::uint64_t signed_add(std::uint64_t base, std::int64_t delta) {
    if (delta < 0) {
        auto n = std::uint64_t(-(delta + 1)) + 1;
        require(base >= n);
        return base - n;
    }
    return add(base, std::uint64_t(delta));
}
void descriptors(const BYTE *p, size_t n, Bytes &asc, BYTE &object_type, int depth = 0) {
    require(depth < 8);
    while (n) {
        require(n >= 2);
        BYTE type = *p++;
        --n;
        DWORD length{};
        int bytes{};
        BYTE c{};
        do {
            require(n && bytes++ < 4);
            c = *p++;
            --n;
            require(length <= 0x1fffff);
            length = (length << 7) | (c & 127);
        } while (c & 128);
        require(length <= n);
        size_t skip = length;
        if (type == 5) {
            require(length <= 65513);
            asc.assign(p, p + length);
            return;
        }
        if (type == 3) {
            require(length >= 3);
            skip = 3;
            BYTE flags = p[2];
            if (flags & 128)
                skip += 2;
            if (flags & 64) {
                require(skip < length);
                skip += 1 + p[skip];
            }
            if (flags & 32)
                skip += 2;
        } else if (type == 4) {
            require(length >= 13);
            object_type = p[0];
            skip = 13;
        }
        if (skip < length)
            descriptors(p + skip, length - skip, asc, object_type, depth + 1);
        if (!asc.empty())
            return;
        p += length;
        n -= length;
    }
}
Bytes box(DWORD type, const Bytes &payload) {
    require(payload.size() <= MAXDWORD - 8);
    Bytes b;
    put32(b, DWORD(payload.size() + 8));
    put32(b, type);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
void append(Bytes &to, const Bytes &from) {
    require(to.size() + from.size() <= 64 * 1024 * 1024);
    to.insert(to.end(), from.begin(), from.end());
}
struct Key {
    DWORD atom;
    const char *key;
};
constexpr Key keys[] = {{fourcc('\xa9', 'n', 'a', 'm'), "Title"},
                        {fourcc('\xa9', 'A', 'R', 'T'), "Artist"},
                        {fourcc('\xa9', 'a', 'l', 'b'), "Album"},
                        {fourcc('\xa9', 'd', 'a', 'y'), "Date"},
                        {fourcc('\xa9', 'g', 'e', 'n'), "Genre"},
                        {fourcc('\xa9', 'c', 'm', 't'), "Comment"},
                        {fourcc('\xa9', 'w', 'r', 't'), "Writer"},
                        {fourcc('\xa9', 't', 'o', 'o'), "Tool"},
                        {cc("aART"), "AlbumArtist"},
                        {cc("trkn"), "Tracknumber"},
                        {cc("disk"), "Disc"},
                        {cc("tmpo"), "Tempo"},
                        {cc("cpil"), "Compilation"},
                        {fourcc('\xa9', 'l', 'y', 'r'), "Lyrics"}};
const char *key_for(DWORD atom) {
    for (auto k : keys)
        if (k.atom == atom)
            return k.key;
    return nullptr;
}
DWORD atom_for(const std::string &key) {
    for (auto k : keys)
        if (_stricmp(k.key, key.c_str()) == 0)
            return k.atom;
    return 0;
}
Bytes text_bytes(const std::string &s) {
    return Bytes(s.begin(), s.end());
}
Bytes tagged_data(DWORD kind, const Bytes &value) {
    Bytes p;
    put32(p, kind);
    put32(p, 0);
    append(p, value);
    return box(cc("data"), p);
}
Box metadata_box(const Bytes &b) {
    auto root = find(b, 0, b.size(), cc("moov"));
    auto udta = child(b, root, cc("udta"));
    auto meta = child(b, udta, cc("meta"));
    if (!meta)
        meta = child(b, root, cc("meta"));
    return meta;
}
Box ilst_box(const Bytes &b) {
    return child(b, metadata_box(b), cc("ilst"), 4);
}
} // namespace

const Tag *Mp4File::tag(const std::string &name) const {
    for (const auto &t : tags)
        if (!_stricmp(t.name.c_str(), name.c_str()))
            return &t;
    return nullptr;
}
void Mp4File::set_tag(const std::string &name, const std::wstring &value) {
    auto it = std::find_if(tags.begin(), tags.end(),
                           [&](const Tag &t) { return !_stricmp(t.name.c_str(), name.c_str()); });
    if (value.empty()) {
        if (it != tags.end())
            tags.erase(it);
    } else if (it != tags.end())
        it->value = value;
    else
        tags.push_back({name, value});
}
void Mp4File::read_tags() {
    auto ilst = ilst_box(moov_);
    if (!ilst)
        return;
    for (auto item : boxes(moov_, ilst.body(), ilst.end())) {
        auto d = child(moov_, item, cc("data"));
        if (!d)
            continue;
        const auto *p = data(moov_, d, 8);
        size_t n = d.size - d.head - 8;
        DWORD kind = be32(p) & 0xffffff;
        p += 8;
        if (item.type == cc("covr")) {
            if (cover.empty() && n)
                cover.assign(p, p + n);
            continue;
        }
        std::string name;
        std::wstring value;
        if (auto *key = key_for(item.type))
            name = key;
        else if (item.type == cc("----")) {
            auto nm = child(moov_, item, cc("name"));
            auto mn = child(moov_, item, cc("mean"));
            if (!nm || !mn)
                continue;
            auto *np = data(moov_, nm, 4);
            auto *mp = data(moov_, mn, 4);
            if (std::string(reinterpret_cast<const char *>(mp + 4), mn.size - mn.head - 4) !=
                "com.apple.iTunes")
                continue;
            name.assign(reinterpret_cast<const char *>(np + 4), nm.size - nm.head - 4);
        } else if (item.type == cc("gnre")) {
            // MP4's integer genre is one-based; ID3's table is zero-based.
            if (n >= 2) {
                auto genre = be16(p);
                if (genre && genre <= kTagGenres.size() && !tag("Genre"))
                    set_tag("Genre", wide(std::string(kTagGenres[genre - 1])));
            }
            continue;
        } else
            continue;
        if (item.type == cc("trkn") || item.type == cc("disk")) {
            if (n < 6)
                continue;
            value = std::to_wstring(be16(p + 2));
            if (be16(p + 4))
                value += L"/" + std::to_wstring(be16(p + 4));
        } else if (item.type == cc("tmpo")) {
            if (n < 2)
                continue;
            value = std::to_wstring(be16(p));
        } else if (item.type == cc("cpil")) {
            if (!n)
                continue;
            value = p[0] ? L"1" : L"0";
        } else if (kind == 2) {
            require(!(n & 1));
            for (size_t i = 0; i < n; i += 2)
                value.push_back(wchar_t(be16(p + i)));
        } else
            value = wide(std::string(reinterpret_cast<const char *>(p), n));
        if (!name.empty())
            set_tag(name, value);
    }
}

void Mp4File::classic(const Bytes &b) {
    Box root = find(b, 0, b.size(), cc("trak"));
    Box tkhd = child(b, root, cc("tkhd"));
    const BYTE *p = data(b, tkhd, 16);
    track_id = be32(data(b, tkhd, p[0] ? 24 : 16) + (p[0] ? 20 : 12));
    auto mdia = child(b, root, cc("mdia"));
    auto mdhd = child(b, mdia, cc("mdhd"));
    p = data(b, mdhd, 4);
    auto ver = p[0];
    require(ver <= 1);
    p = data(b, mdhd, ver ? 32 : 20);
    timescale = be32(p + (ver ? 20 : 12));
    declared_duration_ = ver ? be64(p + 24) : be32(p + 16);
    if ((!ver && declared_duration_ == MAXDWORD) || declared_duration_ == UINT64_MAX)
        declared_duration_ = UINT64_MAX;
    require(timescale);
    auto stbl = child(b, child(b, mdia, cc("minf")), cc("stbl"));
    require(bool(stbl));
    auto stsd = child(b, stbl, cc("stsd"));
    p = data(b, stsd, 8);
    require(be32(p + 4) >= 1);
    auto entries = boxes(b, stsd.body() + 8, stsd.end());
    require(!entries.empty());
    auto audio = entries[0];
    require(audio.type == cc("mp4a"), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    p = data(b, audio, 28);
    WORD version = be16(p + 8);
    channels = be16(p + 16);
    rate = be32(p + 24) >> 16;
    size_t skip = 28;
    if (version == 1)
        skip += 16;
    else
        require(version == 0, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    auto esds = child(b, audio, cc("esds"), skip);
    if (!esds) {
        auto wave = child(b, audio, cc("wave"), skip);
        esds = child(b, wave, cc("esds"));
    }
    require(!child(b, audio, cc("sinf"), skip), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    BYTE object_type{};
    p = data(b, esds, 4);
    descriptors(p + 4, esds.size - esds.head - 4, asc, object_type);
    if (object_type == 0x40 || (object_type >= 0x66 && object_type <= 0x68)) {
        codec = 0x43414146;
        require(asc.size() >= 2);
    } else if (object_type == 0x69 || object_type == 0x6b)
        codec = WAVE_FORMAT_MPEGLAYER3;
    else
        require(false, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    require(rate && channels);
    auto stsz = child(b, stbl, cc("stsz")), stz2 = child(b, stbl, cc("stz2"));
    DWORD count{};
    if (stsz) {
        p = data(b, stsz, 12);
        DWORD fixed = be32(p + 4);
        count = be32(p + 8);
        require(count <= max_samples);
        samples.resize(count);
        if (fixed)
            for (auto &sample : samples)
                sample.size = fixed;
        else {
            p = data(b, stsz, size_t(count) * 4, 12);
            for (DWORD i = 0; i < count; ++i)
                samples[i].size = be32(p + i * 4);
        }
    } else if (stz2) {
        p = data(b, stz2, 12);
        BYTE bits = p[7];
        count = be32(p + 8);
        require(count <= max_samples && (bits == 4 || bits == 8 || bits == 16));
        samples.resize(count);
        p = data(b, stz2, (size_t(count) * bits + 7) / 8, 12);
        for (DWORD i = 0; i < count; ++i)
            samples[i].size = bits == 4 ? ((p[i / 2] >> ((i & 1) ? 0 : 4)) & 15)
                                        : (bits == 8 ? p[i] : be16(p + i * 2));
    } else
        require(false);
    if (!count)
        return;
    auto stts = child(b, stbl, cc("stts"));
    p = data(b, stts, 8);
    DWORD runs = be32(p + 4);
    require(runs <= max_samples);
    p = data(b, stts, size_t(runs) * 8, 8);
    size_t index{};
    std::uint64_t time{};
    for (DWORD r = 0; r < runs; ++r) {
        DWORD n = be32(p + r * 8), duration = be32(p + r * 8 + 4);
        require(n <= samples.size() - index && duration);
        for (DWORD i = 0; i < n; ++i) {
            samples[index].time = time;
            samples[index++].duration = duration;
            time = add(time, duration);
        }
    }
    require(index == samples.size());
    auto ctts = child(b, stbl, cc("ctts"));
    if (ctts) {
        p = data(b, ctts, 8);
        BYTE v = p[0];
        require(v <= 1);
        DWORD n = be32(p + 4);
        require(n <= max_samples);
        p = data(b, ctts, size_t(n) * 8, 8);
        index = 0;
        for (DWORD r = 0; r < n; ++r) {
            DWORD c = be32(p + r * 8), vtime = be32(p + r * 8 + 4);
            require(c <= samples.size() - index);
            for (DWORD i = 0; i < c; ++i)
                samples[index++].composition = v ? std::int32_t(vtime) : std::int64_t(vtime);
        }
        require(index == samples.size());
    }
    auto stco = child(b, stbl, cc("stco"));
    bool large = false;
    if (!stco) {
        stco = child(b, stbl, cc("co64"));
        large = true;
    }
    p = data(b, stco, 8);
    DWORD chunks = be32(p + 4);
    require(chunks <= max_samples);
    p = data(b, stco, size_t(chunks) * (large ? 8 : 4), 8);
    auto stsc = child(b, stbl, cc("stsc"));
    auto *sc = data(b, stsc, 8);
    DWORD nr = be32(sc + 4);
    require(nr && nr <= chunks);
    sc = data(b, stsc, size_t(nr) * 12, 8);
    require(be32(sc) == 1);
    index = 0;
    DWORD r = 0;
    for (DWORD c = 1; c <= chunks; ++c) {
        while (r + 1 < nr && be32(sc + (r + 1) * 12) <= c)
            ++r;
        require(be32(sc + r * 12 + 8) == 1, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        DWORD n = be32(sc + r * 12 + 4);
        require(n && n <= samples.size() - index);
        std::uint64_t pos = large ? be64(p + size_t(c - 1) * 8) : be32(p + size_t(c - 1) * 4);
        for (DWORD i = 0; i < n; ++i) {
            samples[index].offset = pos;
            pos = add(pos, samples[index++].size);
        }
    }
    require(index == samples.size());
}

void Mp4File::fragment(IStream *stream, std::uint64_t start, std::uint64_t length,
                       std::map<DWORD, std::uint64_t> &end_time) {
    require(length <= 64 * 1024 * 1024);
    auto b = read_at(stream, start, size_t(length));
    auto root = find(b, 0, b.size(), cc("moof"));
    std::uint64_t previous_end = start;
    bool first = true;
    for (auto traf : boxes(b, root.body(), root.end())) {
        if (traf.type != cc("traf"))
            continue;
        auto tfhd = child(b, traf, cc("tfhd"));
        auto *p = data(b, tfhd, 8);
        DWORD flags = be32(p) & 0xffffff, id = be32(p + 4);
        size_t at = 8;
        if (id == track_id)
            require(!child(b, traf, cc("senc")), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        auto def = defaults_.find(id);
        require(def != defaults_.end());
        DWORD description = def->second[0], duration = def->second[1], size = def->second[2];
        std::uint64_t base = (flags & 0x020000) || first ? start : previous_end;
        if (flags & 1) {
            base = be64(data(b, tfhd, 8, at));
            at += 8;
        }
        auto field = [&](DWORD flag, DWORD &value) {
            if (flags & flag) {
                value = be32(data(b, tfhd, 4, at));
                at += 4;
            }
        };
        field(2, description);
        field(8, duration);
        field(16, size);
        DWORD sample_flags{};
        field(32, sample_flags);
        auto tfdt = child(b, traf, cc("tfdt"));
        std::uint64_t time = end_time[id];
        if (tfdt) {
            p = data(b, tfdt, 8);
            require(p[0] <= 1);
            time = p[0] ? be64(data(b, tfdt, 12) + 4) : be32(p + 4);
        }
        std::uint64_t cursor = base;
        bool had_run = false;
        for (auto trun : boxes(b, traf.body(), traf.end())) {
            if (trun.type != cc("trun"))
                continue;
            p = data(b, trun, 8);
            BYTE version = p[0];
            require(version <= 1);
            DWORD f = be32(p) & 0xffffff, n = be32(p + 4);
            require(n <= max_samples);
            size_t off = 8;
            if (f & 1) {
                cursor = signed_add(base, std::int32_t(be32(data(b, trun, 4, off))));
                off += 4;
            }
            if (f & 4) {
                data(b, trun, 4, off);
                off += 4;
            }
            DWORD stride = 4 * ((!!(f & 0x100)) + (!!(f & 0x200)) + (!!(f & 0x400)) + (!!(f & 0x800)));
            p = data(b, trun, size_t(n) * stride, off);
            size_t j{};
            if (id == track_id) {
                require(description == 1, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
                require(n <= max_samples - samples.size());
            }
            for (DWORD i = 0; i < n; ++i) {
                DWORD d = duration, s = size;
                std::int64_t composition{};
                if (f & 0x100) {
                    d = be32(p + j);
                    j += 4;
                }
                if (f & 0x200) {
                    s = be32(p + j);
                    j += 4;
                }
                if (f & 0x400)
                    j += 4;
                if (f & 0x800) {
                    DWORD c = be32(p + j);
                    composition = version ? std::int32_t(c) : std::int64_t(c);
                    j += 4;
                }
                require(s && d);
                if (id == track_id)
                    samples.push_back({cursor, time, s, d, composition});
                cursor = add(cursor, s);
                time = add(time, d);
            }
            had_run = true;
        }
        if (!had_run)
            require((flags & 0x010000) != 0);
        end_time[id] = time;
        previous_end = cursor;
        first = false;
    }
}

void Mp4File::open(IStream *stream) {
    *this = Mp4File{};
    file_size_ = stream_size(stream);
    require(file_size_ >= 8);
    struct FileBox {
        std::uint64_t at, size;
        DWORD type;
    };
    std::vector<FileBox> fragments;
    for (std::uint64_t pos = 0; pos < file_size_;) {
        require(file_size_ - pos >= 8);
        auto h = read_at(stream, pos, size_t(std::min<std::uint64_t>(16, file_size_ - pos)));
        std::uint64_t n = be32(h.data());
        DWORD type = be32(h.data() + 4);
        size_t header = 8;
        if (n == 1) {
            require(h.size() >= 16);
            n = be64(h.data() + 8);
            header = 16;
        } else if (!n) {
            n = file_size_ - pos;
            zero_size_boxes_.push_back({pos, n});
        }
        require(n >= header && n <= file_size_ - pos);
        if (type == cc("moov")) {
            require(moov_.empty() && n <= 64 * 1024 * 1024);
            moov_offset_ = pos;
            moov_ = read_at(stream, pos, size_t(n));
        } else if (type == cc("mdat")) {
            media_.push_back({pos + header, pos + n});
            audio_offset = DWORD(pos);
        } else if (type == cc("moof")) {
            require(fragments.size() < max_samples);
            fragments.push_back({pos, n, type});
        }
        pos += n;
    }
    require(!moov_.empty());
    auto root = find(moov_, 0, moov_.size(), cc("moov"));
    auto mvex = child(moov_, root, cc("mvex"));
    if (mvex)
        for (auto t : boxes(moov_, mvex.body(), mvex.end()))
            if (t.type == cc("trex")) {
                auto *p = data(moov_, t, 24);
                defaults_[be32(p + 4)] = {be32(p + 8), be32(p + 12), be32(p + 16), be32(p + 20)};
            }
    bool selected = false;
    for (auto t : boxes(moov_, root.body(), root.end())) {
        if (t.type != cc("trak"))
            continue;
        auto mdia = child(moov_, t, cc("mdia"));
        auto h = child(moov_, mdia, cc("hdlr"));
        if (!h)
            continue;
        if (be32(data(moov_, h, 12) + 8) != cc("soun"))
            continue;
        try {
            classic(slice(moov_, t));
            selected = true;
            break;
        } catch (const Failure &error) {
            if (error.code != HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
                throw;
            samples.clear();
            asc.clear();
        }
    }
    require(selected, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    fragmented = !fragments.empty() || bool(mvex);
    std::map<DWORD, std::uint64_t> ends;
    if (!samples.empty())
        ends[track_id] = add(samples.back().time, samples.back().duration);
    for (auto f : fragments)
        fragment(stream, f.at, f.size, ends);
    std::uint64_t total{}, end{}, last_time{};
    bool first = true;
    for (auto &s : samples) {
        require(s.size && s.size <= 64 * 1024 * 1024);
        require(first || s.time >= last_time);
        first = false;
        last_time = s.time;
        auto m = std::upper_bound(media_.begin(), media_.end(), s.offset,
                                  [](auto at, const auto &range) { return at < range.first; });
        require(m != media_.begin());
        --m;
        require(s.offset >= m->first && s.offset <= m->second && s.size <= m->second - s.offset);
        total = add(total, s.size);
        end = std::max(end, add(s.time, s.duration));
        max_packet = std::max(max_packet, s.size);
    }
    // Original 600232A3 reports mdhd duration minus the first ctts offset;
    // 60022FB5 adds that same offset before looking up a seek sample. Nero
    // writes a priming offset here even though it does not write an edit list.
    if (!fragmented && !samples.empty()) {
        seek_bias = samples.front().composition;
        if (declared_duration_ != UINT64_MAX)
            end = declared_duration_;
        if (seek_bias >= 0)
            end = end > std::uint64_t(seek_bias) ? end - std::uint64_t(seek_bias) : 0;
        else
            end = signed_add(end, -seek_bias);
    }
    duration_ms = milliseconds(end, timescale);
    bitrate = duration_ms ? dword(total * 8000 / duration_ms) : 0;
    read_tags();
    if (codec == 0x43414146 && !samples.empty()) {
        auto mvhd = child(moov_, root, cc("mvhd"));
        if (mvhd) {
            const auto* p = data(moov_, mvhd, 4);
            if (p[0] <= 1)
                edit_scale = be32(data(moov_, mvhd, p[0] ? 24 : 16) + (p[0] ? 20 : 12));
        }
        std::uint64_t edit_duration{};
        const auto coded = add(samples.back().time, samples.back().duration);
        uint64_t presented{};
        bool continuous = true;
        uint64_t expected_time{};
        for (const auto& s : samples) {
            if (s.time != expected_time || s.composition) continuous = false;
            expected_time = add(s.time, s.duration);
            const auto presentation_end = s.presentation_end();
            if (presentation_end > 0) presented = std::max(presented, uint64_t(presentation_end));
        }
        for (auto track : boxes(moov_, root.body(), root.end())) {
            if (track.type != cc("trak")) continue;
            auto tkhd = child(moov_, track, cc("tkhd"));
            if (!tkhd) continue;
            auto* p = data(moov_, tkhd, 4);
            if (p[0] > 1) continue;
            if (be32(data(moov_, tkhd, p[0] ? 24 : 16) + (p[0] ? 20 : 12)) != track_id) continue;
            auto elst = child(moov_, child(moov_, track, cc("edts")), cc("elst"));
            if (!elst) break;
            p = data(moov_, elst, 8);
            require(p[0] <= 1, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
            const auto count = be32(p + 4);
            require(count <= 4096 && (!count || edit_scale));
            const bool v1 = p[0] != 0;
            const size_t stride = v1 ? 20 : 12;
            p = data(moov_, elst, size_t(count) * stride, 8);
            for (DWORD i = 0; i < count; ++i, p += stride) {
                const auto length = v1 ? be64(p) : be32(p);
                const auto begin = v1 ? be64(p + 8) : be32(p + 4);
                const bool empty = begin == (v1 ? UINT64_MAX : UINT32_MAX);
                require(be32(p + (v1 ? 16 : 8)) == 0x10000,
                        HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
                require(empty || begin <= (v1 ? uint64_t(INT64_MAX) : uint64_t(INT32_MAX)));
                const bool to_end = fragmented && count == 1 && !empty && !length;
                if (!length && !to_end) continue;
                if (!empty) {
                    require(begin < presented && length <= UINT64_MAX / timescale);
                    // A movie clock tick may round the final edit beyond the
                    // last media tick. No larger overrun is accepted.
                    require(length * timescale / edit_scale <=
                            add(presented - begin, uint64_t(timescale / edit_scale) + 1));
                }
                edit_duration = add(edit_duration, length);
                edits.push_back({empty ? 0 : begin, length, empty, to_end});
            }
            require(!count || !edits.empty());
            break;
        }
        // A multi-part timeline (including empty edits) is authoritative.
        // A whole-file iTunSMPB must not erase cuts, repeats, or leading silence.
        const bool single_range = continuous && !fragmented &&
            (edits.empty() || (edits.size() == 1 && !edits.front().empty));
        if (const auto* smpb = single_range ? tag("iTunSMPB") : nullptr) {
            unsigned long long reserved{}, delay{}, padding{}, length{};
            if (swscanf_s(smpb->value.c_str(), L"%llx %llx %llx %llx", &reserved,
                          &delay, &padding, &length) == 4 && length &&
                delay <= UINT32_MAX && padding <= UINT32_MAX &&
                length <= UINT64_MAX-delay-padding) {
                if (rate && coded <= UINT64_MAX/rate &&
                    delay+length <= coded*rate/timescale) {
                    bool same_range = edits.empty();
                    if (!same_range && edits.front().begin <= UINT64_MAX / rate &&
                        edits.front().length <= UINT64_MAX / rate) {
                        const auto start = edits.front().begin * rate / timescale;
                        const auto count = edits.front().length * rate / edit_scale;
                        const auto tolerance = uint64_t(rate / edit_scale) + 1;
                        same_range = (start > delay ? start - delay : delay - start) <= tolerance &&
                                     (count > length ? count - length : length - count) <= tolerance;
                    }
                    if (same_range) {
                        gapless = true; trim_begin = delay; trim_length = length;
                        trim_begin_scale = trim_length_scale = rate;
                    }
                }
            }
        }
        // With no edit, fragmented timestamps still define real gaps/offsets.
        // Keep classic Nero's no-edit ctts compatibility path unchanged.
        if (fragmented && edits.empty() && !continuous)
            edits.push_back({0, 0, false, true});
        if (gapless) {
            edits.clear();
            duration_ms = milliseconds(trim_length, trim_length_scale);
        } else if (!edits.empty()) {
            // AAC packets keep decode order. Presentation gaps are silence;
            // overlapping/reordered composition intervals require a separate
            // mixing/reordering policy and are deliberately not guessed.
            int64_t previous_end = INT64_MIN;
            for (const auto& sample : samples) {
                require(sample.presentation() >= previous_end,
                        HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
                previous_end = sample.presentation_end();
            }
            duration_ms = edits.front().to_end
                ? milliseconds(presented - edits.front().begin, timescale)
                : milliseconds(edit_duration, edit_scale);
        }
    }
}

void Mp4File::save(IStream *stream) {
    // Keep all sample offsets unchanged: append a new moov, then retire the old
    // one as free. Unknown children remain byte-for-byte intact.
    require(!fragmented, E_ACCESSDENIED);
    for (auto [at, size] : zero_size_boxes_) {
        (void)at;
        require(size <= MAXDWORD, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    }
    Bytes items;
    auto old = ilst_box(moov_);
    if (old)
        for (auto item : boxes(moov_, old.body(), old.end())) {
            bool known = key_for(item.type) || item.type == cc("covr") || item.type == cc("gnre");
            if (item.type == cc("----")) {
                auto mn = child(moov_, item, cc("mean"));
                if (mn) {
                    auto *p = data(moov_, mn, 4);
                    known = std::string(reinterpret_cast<const char *>(p + 4), mn.size - mn.head - 4) ==
                            "com.apple.iTunes";
                }
            }
            if (!known)
                append(items, slice(moov_, item));
        }
    for (const auto &t : tags) {
        DWORD atom = atom_for(t.name);
        Bytes value;
        DWORD kind = 1;
        if (atom == cc("trkn") || atom == cc("disk")) {
            unsigned a{}, z{};
            swscanf_s(t.value.c_str(), L"%u/%u", &a, &z);
            require(a <= 65535 && z <= 65535, E_INVALIDARG);
            value = {0, 0, BYTE(a >> 8), BYTE(a), BYTE(z >> 8), BYTE(z), 0, 0};
            kind = 0;
        } else if (atom == cc("tmpo")) {
            unsigned n = wcstoul(t.value.c_str(), nullptr, 10);
            require(n <= 65535, E_INVALIDARG);
            value = {BYTE(n >> 8), BYTE(n)};
            kind = 21;
        } else if (atom == cc("cpil")) {
            value = {BYTE(t.value == L"0" ? 0 : 1)};
            kind = 21;
        } else
            value = text_bytes(utf8(t.value));
        Bytes content;
        if (!atom) {
            Bytes mean(4), name(4);
            append(mean, text_bytes("com.apple.iTunes"));
            append(name, text_bytes(t.name));
            append(content, box(cc("mean"), mean));
            append(content, box(cc("name"), name));
            atom = cc("----");
        }
        append(content, tagged_data(kind, value));
        append(items, box(atom, content));
    }
    if (!cover.empty()) {
        DWORD type =
            cover.size() >= 8 && cover[0] == 137 && cover[1] == 'P' && cover[2] == 'N' && cover[3] == 'G'
                ? 14
                : 13;
        append(items, box(cc("covr"), tagged_data(type, cover)));
    }
    auto new_ilst = box(cc("ilst"), items);
    auto oldmeta = metadata_box(moov_);
    Bytes newmeta;
    if (oldmeta) {
        Bytes body(data(moov_, oldmeta, 4), data(moov_, oldmeta, 4) + 4);
        bool inserted = false;
        for (auto x : boxes(moov_, oldmeta.body() + 4, oldmeta.end())) {
            if (x.type == cc("ilst")) {
                if (!inserted)
                    append(body, new_ilst);
                inserted = true;
            } else
                append(body, slice(moov_, x));
        }
        if (!inserted)
            append(body, new_ilst);
        newmeta = box(cc("meta"), body);
    } else {
        Bytes hdlr(24);
        set32(hdlr, 8, cc("mdir"));
        set32(hdlr, 12, cc("appl"));
        hdlr.push_back(0);
        Bytes body(4);
        append(body, box(cc("hdlr"), hdlr));
        append(body, new_ilst);
        newmeta = box(cc("meta"), body);
    }
    auto root = find(moov_, 0, moov_.size(), cc("moov"));
    Bytes body;
    bool replaced = false;
    for (auto x : boxes(moov_, root.body(), root.end())) {
        if (oldmeta && x.at == oldmeta.at) {
            append(body, newmeta);
            replaced = true;
        } else if (x.type == cc("udta")) {
            Bytes u;
            for (auto y : boxes(moov_, x.body(), x.end())) {
                if (oldmeta && y.at == oldmeta.at) {
                    append(u, newmeta);
                    replaced = true;
                } else
                    append(u, slice(moov_, y));
            }
            if (!oldmeta && !replaced) {
                append(u, newmeta);
                replaced = true;
            }
            append(body, box(cc("udta"), u));
        } else
            append(body, slice(moov_, x));
    }
    if (!replaced)
        append(body, box(cc("udta"), newmeta));
    Bytes updated = box(cc("moov"), body);
    auto size = stream_size(stream);
    require(size == file_size_, STG_E_REVERTED);
    require(updated.size() <= MAXDWORD, E_OUTOFMEMORY);
    // An EOF-sized free box remains parseable even after a short body write.
    // Rollback can retire it without SetSize (some original host streams cannot
    // truncate). Never append a second active moov before its body is complete.
    const BYTE vacant[] = {0, 0, 0, 0, 'f', 'r', 'e', 'e'};
    const BYTE active[] = {'m', 'o', 'o', 'v'};
    const BYTE inactive[] = {'f', 'r', 'e', 'e'};
    bool appending = false, retiring = false;
    size_t normalizing = 0;
    const HRESULT result = protect([&]() -> HRESULT {
        // Normalize size=0 only after all metadata validation/allocation. These
        // sizes otherwise swallow the appended index into an existing box.
        for (auto [at, length] : zero_size_boxes_) {
            ++normalizing;
            Bytes fixed; put32(fixed, DWORD(length));
            seek(stream, at); write_exact(stream, fixed.data(), 4);
        }
        seek(stream, size); appending = true;
        write_exact(stream, vacant, sizeof(vacant));
        write_exact(stream, updated.data() + 8, DWORD(updated.size() - 8));
        seek(stream, size); write_exact(stream, updated.data(), 4);
        commit_stream(stream);
        seek(stream, size + 4); write_exact(stream, active, 4);
        seek(stream, moov_offset_ + 4); retiring = true;
        write_exact(stream, inactive, 4);
        commit_stream(stream);
        return S_OK;
    });
    if (FAILED(result)) {
        HRESULT recovery = S_OK;
        auto recover = [&](auto &&operation) {
            const HRESULT hr = protect([&]() -> HRESULT { operation(); return S_OK; });
            if (FAILED(hr) && SUCCEEDED(recovery)) recovery = hr;
        };
        if (retiring)
            recover([&] { seek(stream, moov_offset_ + 4); write_exact(stream, active, 4); });
        if (appending) {
            recover([&] { seek(stream, size); write_exact(stream, vacant, sizeof(vacant)); });
            // Shrinking is an optimization, not a prerequisite for recovery.
            // If it fails, the unused tail is a valid EOF-sized free box.
            if (SUCCEEDED(recovery)) {
                ULARGE_INTEGER original{}; original.QuadPart = size;
                (void)stream->SetSize(original);
            }
        } else {
            // No tail exists yet: restore any partially normalized headers.
            const BYTE zero[4]{};
            for (size_t i = 0; i < normalizing; ++i)
                recover([&] { seek(stream, zero_size_boxes_[i].first); write_exact(stream, zero, 4); });
        }
        // Do not report success if restoring a header itself failed. A real
        // persistent device failure cannot be made atomic by this legacy ABI.
        if (FAILED(recovery)) throw Failure{STG_E_REVERTED};
        (void)protect([&]() -> HRESULT { commit_stream(stream); return S_OK; });
        throw Failure{result};
    }
    zero_size_boxes_.clear();
    moov_offset_ = size;
    moov_ = std::move(updated);
    file_size_ = size + moov_.size();
}
} // namespace ttp::aac
