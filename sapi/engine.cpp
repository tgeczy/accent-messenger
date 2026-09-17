// SAPI adapter: COM ownership, fragment grouping and events. The speech runtime
// and file settings are separate modules, as in Tomi's Panthera project.
#include "runtime.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <mutex>
#include <new>
#include <olectl.h>
#include <sapi.h>
#include <sphelper.h>
#include <stdexcept>

using namespace messenger_sapi;
static HMODULE moduleHandle;
static LONG objects = 0;
static const CLSID engineClsid = {
    0x6c5aa6a4, 0xc560, 0x4c3f, {0x9f, 0x7b, 0x9d, 0xa2, 0xa2, 0x4d, 0x2b, 0x61}};
static const wchar_t* tokenKey = L"Software\\Microsoft\\Speech\\Voices\\Tokens\\AccentMessenger";
struct Mark {
    size_t position;
    std::wstring name;
};
struct Span {
    size_t position, length;
    ULONG source;
};
struct Segment {
    std::wstring text;
    SPVSTATE state{};
    std::vector<Mark> marks;
    std::vector<Span> words;
    ULONG silence = 0;
};
static bool compatible(const SPVSTATE& a, const SPVSTATE& b) {
    return a.eAction == b.eAction && a.RateAdj == b.RateAdj && a.Volume == b.Volume &&
           a.PitchAdj.MiddleAdj == b.PitchAdj.MiddleAdj;
}
static std::vector<Segment> segments(const SPVTEXTFRAG* fragment) {
    std::vector<Segment> result;
    Segment current;
    auto flush = [&] {
        if (!current.text.empty() || !current.marks.empty() || current.silence)
            result.push_back(std::move(current));
        current = Segment{};
    };
    size_t total = 0, count = 0;
    for (auto f = fragment; f; f = f->pNext) {
        if (++count > 65536 || (total += f->ulTextLen) > 1000000)
            throw std::runtime_error("Excessive SAPI input");
        if (f->ulTextLen && !f->pTextStart)
            throw std::runtime_error("Missing fragment text");
        if (f->State.eAction == SPVA_Bookmark) {
            current.marks.push_back(
                {current.text.size(),
                 std::wstring(f->pTextStart ? f->pTextStart : L"", f->ulTextLen)});
            continue;
        }
        if (f->State.eAction == SPVA_Silence) {
            flush();
            current.silence = std::min<ULONG>(f->State.SilenceMSecs, 10000);
            flush();
            continue;
        }
        if (f->State.eAction != SPVA_Speak && f->State.eAction != SPVA_SpellOut &&
            f->State.eAction != SPVA_Pronounce)
            continue;
        if (!current.text.empty() && !compatible(current.state, f->State))
            flush();
        current.state = f->State;
        // Preserve adjacent fragments and intervening bookmarks in one request.
        // Splitting at each bookmark made other SAPI engines speak word by word.
        for (ULONG i = 0; i < f->ulTextLen;) {
            size_t available = 160 - current.text.size();
            size_t n = std::min<size_t>(available, f->ulTextLen - i);
            if (n < f->ulTextLen - i) {
                size_t cut = n;
                while (cut && !iswspace(f->pTextStart[i + cut - 1]))
                    --cut;
                if (cut)
                    n = cut;
            }
            size_t base = current.text.size();
            current.text.append(f->pTextStart + i, n);
            for (size_t j = 0; j < n;) {
                if (iswspace(f->pTextStart[i + j])) {
                    ++j;
                    continue;
                }
                size_t begin = j;
                while (j < n && !iswspace(f->pTextStart[i + j]))
                    ++j;
                current.words.push_back(
                    {base + begin, j - begin, f->ulTextSrcOffset + i + ULONG(begin)});
            }
            i += ULONG(n);
            if (current.text.size() >= 160 || i < f->ulTextLen) {
                flush();
                current.state = f->State;
            }
        }
    }
    flush();
    return result;
}

class SapiEngine final : public ISpTTSEngine, public ISpObjectWithToken {
    LONG refs = 1;
    ISpObjectToken* token = nullptr;
    Runtime runtime;
    std::mutex speaking;

  public:
    SapiEngine() {
        InterlockedIncrement(&objects);
    }
    ~SapiEngine() {
        runtime.close();
        if (token)
            token->Release();
        InterlockedDecrement(&objects);
    }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_ISpTTSEngine)
            *result = static_cast<ISpTTSEngine*>(this);
        else if (iid == IID_ISpObjectWithToken)
            *result = static_cast<ISpObjectWithToken*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&refs);
    }
    STDMETHODIMP_(ULONG) Release() override {
        auto n = InterlockedDecrement(&refs);
        if (!n)
            delete this;
        return n;
    }
    STDMETHODIMP SetObjectToken(ISpObjectToken* value) override {
        if (!value)
            return E_INVALIDARG;
        if (token)
            return E_UNEXPECTED;
        try {
            auto hr = runtime.open(moduleHandle);
            if (FAILED(hr))
                return hr;
            token = value;
            token->AddRef();
            return S_OK;
        } catch (...) {
            return E_FAIL;
        }
    }
    STDMETHODIMP GetObjectToken(ISpObjectToken** value) override {
        if (!value)
            return E_POINTER;
        *value = token;
        if (token)
            token->AddRef();
        return token ? S_OK : S_FALSE;
    }
    STDMETHODIMP GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* id,
                                 WAVEFORMATEX** format) override {
        if (!id || !format)
            return E_POINTER;
        *format = nullptr;
        auto f = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
        if (!f)
            return E_OUTOFMEMORY;
        *f = {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
        *id = SPDFID_WaveFormatEx;
        *format = f;
        return S_OK;
    }
    HRESULT speakInner(const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) {
        if (!site || !token)
            return E_UNEXPECTED;
        std::lock_guard<std::mutex> lock(speaking);
        Settings config = readSettings();
        ULONGLONG byteOffset = 0;
        auto groups = segments(fragments);
        for (auto& group : groups) {
            DWORD actions = site->GetActions();
            if (actions & SPVES_ABORT)
                return S_OK;
            if (actions & SPVES_SKIP) {
                auto hr = site->CompleteSkip(0);
                if (FAILED(hr))
                    return hr;
            }
            std::vector<short> pcm;
            if (group.silence)
                pcm.resize(size_t(group.silence) * 16, 0);
            else if (!group.text.empty()) {
                std::string text =
                    normalise(group.text, config.readDigits, group.state.eAction == SPVA_SpellOut);
                // Number expansion can lengthen a request. Keep the same bounded
                // chunks as NVDA while leaving source event positions intact.
                for (size_t start = 0; start < text.size();) {
                    size_t end = std::min(start + 160, text.size());
                    if (end < text.size()) {
                        auto space = text.rfind(' ', end);
                        if (space != std::string::npos && space > start + 40)
                            end = space;
                    }
                    std::string chunk = text.substr(start, end - start);
                    while (!chunk.empty() && chunk.back() == ' ')
                        chunk.pop_back();
                    auto first = chunk.find_first_not_of(' ');
                    if (first != std::string::npos)
                        chunk.erase(0, first);
                    else
                        chunk.clear();
                    long siteRate = 0;
                    auto hr = site->GetRate(&siteRate);
                    if (FAILED(hr))
                        return hr;
                    int percent =
                        std::clamp(config.rate + int(std::clamp(siteRate, -10L, 10L)) * 5 +
                                       int(std::clamp(group.state.RateAdj, -10L, 10L)) * 5,
                                   0, 100);
                    int rate = percent <= 50 ? int(std::nearbyint(percent / 10.0))
                                             : int(std::nearbyint(5 + (percent - 50) * 12 / 50.0));
                    int pitch = std::clamp(
                        (config.pitch +
                         int(std::clamp(group.state.PitchAdj.MiddleAdj, -10L, 10L)) * 5) /
                            10,
                        0, 9);
                    bool aborted = false;
                    if (!chunk.empty())
                        hr = runtime.synthesize(chunk, config, rate, pitch, site, pcm, aborted);
                    if (FAILED(hr) || aborted)
                        return hr;
                    start = end;
                    while (start < text.size() && text[start] == ' ')
                        ++start;
                }
            }
            // The original driver gives no word timestamps. These events are
            // ordered estimates within a chunk, not recovered phoneme timing.
            ULONGLONG interest = 0;
            auto hr = site->GetEventInterest(&interest);
            if (FAILED(hr))
                return hr;
            struct Pending {
                size_t position;
                bool mark;
                size_t index;
            };
            std::vector<Pending> pending;
            for (size_t i = 0; i < group.marks.size(); i++)
                pending.push_back({group.marks[i].position, true, i});
            for (size_t i = 0; i < group.words.size(); i++)
                pending.push_back({group.words[i].position, false, i});
            std::stable_sort(pending.begin(), pending.end(),
                             [](auto& a, auto& b) { return a.position < b.position; });
            for (auto& item : pending) {
                SPEVENT event{};
                event.ullAudioStreamOffset =
                    byteOffset +
                    2 * (pcm.size() * item.position / std::max<size_t>(1, group.text.size()));
                if (item.mark) {
                    auto& mark = group.marks[item.index];
                    event.eEventId = SPEI_TTS_BOOKMARK;
                    event.elParamType = SPET_LPARAM_IS_STRING;
                    event.lParam = reinterpret_cast<LPARAM>(mark.name.c_str());
                    event.wParam = wcstoul(mark.name.c_str(), nullptr, 10);
                } else {
                    auto& word = group.words[item.index];
                    event.eEventId = SPEI_WORD_BOUNDARY;
                    event.wParam = word.length;
                    event.lParam = word.source;
                }
                if (interest & SPFEI(event.eEventId)) {
                    hr = site->AddEvents(&event, 1);
                    if (FAILED(hr))
                        return hr;
                }
            }
            for (size_t offset = 0; offset < pcm.size();) {
                if (site->GetActions() & SPVES_ABORT)
                    return S_OK;
                USHORT volume = 100;
                hr = site->GetVolume(&volume);
                if (FAILED(hr))
                    return hr;
                size_t count = std::min<size_t>(320, pcm.size() - offset);
                short block[320];
                double gain = std::min<unsigned>(volume, 100) / 100.0 * config.volume / 100.0 *
                              std::min<ULONG>(group.silence ? 100 : group.state.Volume, 100) /
                              100.0;
                for (size_t i = 0; i < count; i++)
                    block[i] = short(std::nearbyint(pcm[offset + i] * gain));
                ULONG bytes = ULONG(count * 2), written = 0;
                ULONGLONG stalled = GetTickCount64();
                while (written < bytes) {
                    DWORD actions = site->GetActions();
                    if (actions & SPVES_ABORT)
                        return S_OK;
                    if (actions & SPVES_SKIP)
                        return site->CompleteSkip(0);
                    ULONG n = 0;
                    hr = site->Write(reinterpret_cast<const BYTE*>(block) + written,
                                     bytes - written, &n);
                    if (FAILED(hr))
                        return hr;
                    if (n > bytes - written || n % 2)
                        return E_FAIL;
                    // Votrax's adapter documents zero-byte backpressure on old
                    // SAPI hosts. Retry while remaining cancellable, but bound a
                    // permanently stalled host rather than hanging its application.
                    if (!n) {
                        if (GetTickCount64() - stalled > 5000)
                            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                        Sleep(5);
                        continue;
                    }
                    stalled = GetTickCount64();
                    written += n;
                }
                offset += count;
                byteOffset += bytes;
            }
        }
        return S_OK;
    }
    STDMETHODIMP Speak(DWORD, REFGUID formatId, const WAVEFORMATEX* format,
                       const SPVTEXTFRAG* fragments, ISpTTSEngineSite* site) override {
        if (formatId != SPDFID_WaveFormatEx || !format || format->wFormatTag != WAVE_FORMAT_PCM ||
            format->nChannels != 1 || format->nSamplesPerSec != 16000 ||
            format->wBitsPerSample != 16 || format->nBlockAlign != 2)
            return E_INVALIDARG;
        try {
            return speakInner(fragments, site);
        } catch (...) {
            return E_FAIL;
        }
    }
};
class SapiFactory final : public IClassFactory {
    LONG refs = 1;

  public:
    SapiFactory() {
        InterlockedIncrement(&objects);
    }
    ~SapiFactory() {
        InterlockedDecrement(&objects);
    }
    STDMETHODIMP QueryInterface(REFIID i, void** p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (i != IID_IUnknown && i != IID_IClassFactory)
            return E_NOINTERFACE;
        *p = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&refs);
    }
    STDMETHODIMP_(ULONG) Release() override {
        auto n = InterlockedDecrement(&refs);
        if (!n)
            delete this;
        return n;
    }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID i, void** p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;
        try {
            auto e = new SapiEngine;
            auto hr = e->QueryInterface(i, p);
            e->Release();
            return hr;
        } catch (...) {
            return E_OUTOFMEMORY;
        }
    }
    STDMETHODIMP LockServer(BOOL value) override {
        InterlockedExchangeAdd(&objects, value ? 1 : -1);
        return S_OK;
    }
};
STDAPI DllCanUnloadNow() {
    return objects ? S_FALSE : S_OK;
}
STDAPI DllGetClassObject(REFCLSID c, REFIID i, void** p) {
    if (!p)
        return E_POINTER;
    *p = nullptr;
    if (c != engineClsid)
        return CLASS_E_CLASSNOTAVAILABLE;
    try {
        auto f = new SapiFactory;
        auto hr = f->QueryInterface(i, p);
        f->Release();
        return hr;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}
static LSTATUS setString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY h = nullptr;
    auto result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE,
                                  nullptr, &h, nullptr);
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                DWORD((value.size() + 1) * 2));
        RegCloseKey(h);
    }
    return result;
}
static HRESULT registration(bool add) {
    wchar_t clsid[64];
    StringFromGUID2(engineClsid, clsid, 64);
    std::wstring key = L"Software\\Classes\\CLSID\\" + std::wstring(clsid);
    if (!add) {
        auto a = RegDeleteTreeW(HKEY_LOCAL_MACHINE, tokenKey),
             b = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
        return (a == ERROR_SUCCESS || a == ERROR_FILE_NOT_FOUND) &&
                       (b == ERROR_SUCCESS || b == ERROR_FILE_NOT_FOUND)
                   ? S_OK
                   : SELFREG_E_CLASS;
    }
    auto result = setString(key, nullptr, L"Accent Messenger SAPI");
    if (!result)
        result = setString(key + L"\\InprocServer32", nullptr, modulePath(moduleHandle));
    if (!result)
        result = setString(key + L"\\InprocServer32", L"ThreadingModel", L"Both");
    if (!result)
        result = setString(tokenKey, nullptr, L"Accent Messenger");
    if (!result)
        result = setString(tokenKey, L"409", L"Accent Messenger");
    if (!result)
        result = setString(tokenKey, L"CLSID", clsid);
    auto attributes = std::wstring(tokenKey) + L"\\Attributes";
    if (!result)
        result = setString(attributes, L"Name", L"Accent Messenger");
    if (!result)
        result = setString(attributes, L"Language", L"409");
    if (!result)
        result = setString(attributes, L"Vendor", L"Tamas Geczy");
    if (!result)
        result = setString(attributes, L"Gender", L"Male");
    return HRESULT_FROM_WIN32(result);
}
STDAPI DllRegisterServer() {
    try {
        auto hr = prepareMachineSettings();
        if (FAILED(hr))
            return hr;
        return registration(true);
    } catch (...) {
        return E_FAIL;
    }
}
STDAPI DllUnregisterServer() {
    try {
        return registration(false);
    } catch (...) {
        return E_FAIL;
    }
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        moduleHandle = h;
    return TRUE;
}
