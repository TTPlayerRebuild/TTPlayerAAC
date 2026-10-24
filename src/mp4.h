#pragma once
#include "common.h"
#include <map>
namespace ttp::aac {
struct Sample {
    std::uint64_t offset{}, time{};
    DWORD size{}, duration{};
    std::int64_t composition{};
    std::int64_t presentation() const {
        require(time <= uint64_t(INT64_MAX));
        require(composition <= 0 || time <= uint64_t(INT64_MAX - composition));
        return int64_t(time) + composition;
    }
    std::int64_t presentation_end() const {
        const auto begin = presentation();
        require(begin <= INT64_MAX - duration);
        return begin + duration;
    }
};
struct Tag {
    std::string name;
    std::wstring value;
};
struct Edit {
    std::uint64_t begin{}, length{};
    bool empty{};
    // A single zero-duration edit in a fragmented initialization segment
    // maps all subsequent media, without rounding its end to the movie clock.
    bool to_end{};
};
class Mp4File {
    Bytes moov_;
    std::uint64_t moov_offset_{}, file_size_{}, declared_duration_{};
    std::vector<std::pair<std::uint64_t, std::uint64_t>> media_;
    std::map<DWORD, std::vector<DWORD>> defaults_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> zero_size_boxes_;
    void classic(const Bytes &track);
    void fragment(IStream *, std::uint64_t, std::uint64_t, std::map<DWORD, std::uint64_t> &);
    void read_tags();

  public:
    std::vector<Sample> samples;
    std::vector<Tag> tags;
    Bytes cover, asc;
    DWORD track_id{}, timescale{}, rate{}, channels{}, max_packet{}, bitrate{}, duration_ms{};
    DWORD codec{0x43414146}, audio_offset{};
    std::int64_t seek_bias{};
    bool fragmented{};
    // Edit lengths use the movie clock; media starts use the track clock.
    std::vector<Edit> edits;
    DWORD edit_scale{};
    // iTunSMPB can refine an unedited or single continuous playable range.
    bool gapless{};
    std::uint64_t trim_begin{}, trim_length{};
    DWORD trim_begin_scale{}, trim_length_scale{};
    void open(IStream *);
    void save(IStream *);
    void set_tag(const std::string &, const std::wstring &);
    const Tag *tag(const std::string &) const;
};
} // namespace ttp::aac
