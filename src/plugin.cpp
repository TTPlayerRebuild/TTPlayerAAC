#include "ttpcomm/audio_format_probe_impl.h"
#include "common.h"

namespace ttp::aac {
HMODULE module{};
StandardContent standard_content() {
    auto host = GetModuleHandleW(L"soundcore.dll");
    if (!host)
        host = GetModuleHandleW(nullptr);
    return reinterpret_cast<StandardContent>(GetProcAddress(host, "CreateStdContent"));
}
template <class Interface> class FactoryBase : public Interface {
    LONG references_{1};
    GUID category_;

  public:
    explicit FactoryBase(GUID category) : category_(category) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, category_))
            return E_NOINTERFACE;
        *out = static_cast<Interface *>(this);
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

  protected:
    virtual ~FactoryBase() = default; // after the recovered public slots
};
class ReaderFactory final : public FactoryBase<ReaderCreator>, public ttp::format::ProbeSupport {
protected:
    DWORD ProbeFamilies() const noexcept override { return mp4_ ? ttp::format::Mp4 : ttp::format::Aac; }
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return FactoryBase<ReaderCreator>::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return FactoryBase<ReaderCreator>::Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(iid, ttp::format::iid_probe)) { *out=static_cast<ttp::format::Probe*>(this); AddRef(); return S_OK; }
        return FactoryBase<ReaderCreator>::QueryInterface(iid,out);
    }
private:

    bool mp4_;

  public:
    explicit ReaderFactory(bool mp4) : FactoryBase(cat_reader), mp4_(mp4) {}
    HRESULT STDMETHODCALLTYPE Create(void **out) override { return make_reader(mp4_, out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override {
        return text(mp4_ ? L"MP4 Reader" : L"AAC Reader", out);
    }
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **out) override {
        return text(mp4_ ? L"MPEG-4 Audio (*.m4a;*.mp4;*.3gp;*.3g2)" : L"AAC Audio (*.aac)", out);
    }
};
class DecoderFactory final : public FactoryBase<DecoderCreator> {
  public:
    DecoderFactory() : FactoryBase(cat_decoder) {}
    HRESULT STDMETHODCALLTYPE Create(void **out) override { return make_decoder(out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override { return text(L"AAC Decoder", out); }
    HRESULT STDMETHODCALLTYPE Supports(const GUID *type) override {
        return !type ? E_POINTER : (same(*type, aac_subtype) ? S_OK : E_INVALIDARG);
    }
};
class EncoderFactory final : public FactoryBase<EncoderCreator> {
  public:
    EncoderFactory() : FactoryBase(cat_encoder) {}
    HRESULT STDMETHODCALLTYPE Create(void **out) override { return make_encoder(out); }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override { return text(L"Nero HE-AAC", out); }
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **out) override { return text(L"m4a;.aac", out); }
    HRESULT STDMETHODCALLTYPE CanConfigure() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Configure(HWND parent) override { return configure_nero(parent); }
    HRESULT STDMETHODCALLTYPE Availability(wchar_t **out) override { return nero_available(out); }
    HRESULT STDMETHODCALLTYPE InputBits(DWORD *low, DWORD *high) override {
        if (!low && !high)
            return E_POINTER;
        if (low)
            *low = 16;
        if (high)
            *high = 16;
        return S_OK;
    }
};
class SoundAddIn final : public AddIn {
    LONG references_{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_addin))
            return E_NOINTERFACE;
        *out = static_cast<AddIn *>(this);
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
    HRESULT STDMETHODCALLTYPE Enum(DWORD index, GUID *category, void **out) override {
        if (!out || !category)
            return E_POINTER;
        *out = nullptr;
        return protect([&]() -> HRESULT {
            switch (index) {
            case 0:
            case 1:
                *category = cat_reader;
                *out = static_cast<ReaderCreator *>(new ReaderFactory(index == 0));
                return S_OK;
            case 2:
                *category = cat_decoder;
                *out = static_cast<DecoderCreator *>(new DecoderFactory);
                return S_OK;
            case 3:
                *category = cat_encoder;
                *out = static_cast<EncoderCreator *>(new EncoderFactory);
                return S_OK;
            default:
                return E_INVALIDARG;
            }
        });
    }
};
} // namespace ttp::aac
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return ttp::aac::protect([&]() -> HRESULT {
        *out = static_cast<ttp::aac::AddIn *>(new ttp::aac::SoundAddIn);
        return S_OK;
    });
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ttp::aac::module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
