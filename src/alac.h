#pragma once
#include "common.h"
namespace ttp::aac {
inline constexpr DWORD alac_codec = 0x616c6163;
class AlacCore {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    DWORD rate{}, channels{}, channel_mask{}, frame_length{};
    WORD bits{}, bytes{};
    AlacCore();
    ~AlacCore();
    void config(const Bytes& cookie);
    Bytes decode(const Bytes& packet);
};
}
