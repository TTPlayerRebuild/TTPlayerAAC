#pragma once
#include "common.h"
#include <neaacdec.h>
namespace ttp::aac {
class AacCore {
    NeAACDecHandle handle_{};

  public:
    unsigned long rate{};
    unsigned char channels{};
    DWORD frame_length{1024};
    AacCore() = default;
    ~AacCore() { close(); }
    AacCore(const AacCore &) = delete;
    AacCore &operator=(const AacCore &) = delete;
    void close() {
        if (handle_)
            NeAACDecClose(handle_);
        handle_ = nullptr;
    }
    void create();
    void config(const Bytes &asc);
    DWORD raw(const Bytes &initial);
    void reset() {
        if (handle_)
            NeAACDecPostSeekReset(handle_, -1);
    }
    Bytes decode(const BYTE *data, DWORD bytes, DWORD &consumed, bool discover_format = false);
};
} // namespace ttp::aac
