// Real COM interfaces with a capturing output site; never opens an audio device.
#include "settings.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sapi.h>
#include <sapiddk.h>
#include <sphelper.h>
#include <stdexcept>
#include <vector>

using namespace messenger_sapi;
static const CLSID clsid = {
    0x6c5aa6a4, 0xc560, 0x4c3f, {0x9f, 0x7b, 0x9d, 0xa2, 0xa2, 0x4d, 0x2b, 0x61}};
static void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
static void okay(HRESULT hr, const char* text) {
    if (FAILED(hr)) {
        printf("HRESULT %08lx: ", hr);
        throw std::runtime_error(text);
    }
    printf("ok: %s\n", text);
}
// Copy only values and subkeys; RegCopyTree also copies ACLs, which can make
// Windows-owned speech metadata unsuitable for a disposable user-owned tree.
static LSTATUS copyMetadata(HKEY source, HKEY target) {
    DWORD valueNameMax = 0, valueBytesMax = 0, subkeyMax = 0;
    auto status = RegQueryInfoKeyW(source, nullptr, nullptr, nullptr, nullptr, &subkeyMax, nullptr,
                                   nullptr, &valueNameMax, &valueBytesMax, nullptr, nullptr);
    if (status)
        return status;
    std::vector<wchar_t> name(std::max(valueNameMax, subkeyMax) + 2);
    std::vector<BYTE> value(valueBytesMax + 1);
    for (DWORD i = 0;; i++) {
        DWORD chars = DWORD(name.size()), bytes = DWORD(value.size()), type = 0;
        status =
            RegEnumValueW(source, i, name.data(), &chars, nullptr, &type, value.data(), &bytes);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status)
            return status;
        status = RegSetValueExW(target, name.data(), 0, type, value.data(), bytes);
        if (status)
            return status;
    }
    for (DWORD i = 0;; i++) {
        DWORD chars = DWORD(name.size());
        status = RegEnumKeyExW(source, i, name.data(), &chars, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS)
            return ERROR_SUCCESS;
        if (status)
            return status;
        HKEY childSource = nullptr, childTarget = nullptr;
        status = RegOpenKeyExW(source, name.data(), 0, KEY_READ, &childSource);
        if (status)
            return status;
        status = RegCreateKeyExW(target, name.data(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                 &childTarget, nullptr);
        if (!status)
            status = copyMetadata(childSource, childTarget);
        RegCloseKey(childSource);
        if (childTarget)
            RegCloseKey(childTarget);
        if (status)
            return status;
    }
}
class Site final : public ISpTTSEngineSite {
  public:
    std::vector<BYTE> audio;
    std::vector<ULONGLONG> marks;
    std::vector<size_t> markWrites;
    std::vector<std::wstring> markNames;
    std::vector<long> completedSkips;
    LONG refs = 1;
    long rate = 0;
    USHORT volume = 100;
    ULONG partial = 0, zeros = 0;
    bool fail = false, abort = false;
    size_t abortAfter = 0;
    ULONGLONG abortAt = 0;
    ULONGLONG skipAt = 0;
    bool skip = false, skipDone = false, skipInfoFails = false;
    long skipCount = 0;
    std::wstring skipOnMark;
    ULONG writeCalls = 0, zeroOnWrite = 0;
    STDMETHODIMP QueryInterface(REFIID i, void** p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (i != IID_IUnknown && i != IID_ISpTTSEngineSite && i != IID_ISpEventSink)
            return E_NOINTERFACE;
        *p = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
        return ++refs;
    }
    STDMETHODIMP_(ULONG) Release() override {
        return --refs;
    }
    STDMETHODIMP AddEvents(const SPEVENT* events, ULONG count) override {
        for (ULONG i = 0; i < count; i++)
            if (events[i].eEventId == SPEI_TTS_BOOKMARK) {
                marks.push_back(events[i].ullAudioStreamOffset);
                markWrites.push_back(audio.size());
                markNames.emplace_back(reinterpret_cast<const wchar_t*>(events[i].lParam));
                if (!skipDone && markNames.back() == skipOnMark)
                    skip = true;
            }
        return S_OK;
    }
    STDMETHODIMP GetEventInterest(ULONGLONG* p) override {
        *p = SPFEI_ALL_TTS_EVENTS;
        return S_OK;
    }
    STDMETHODIMP_(DWORD) GetActions() override {
        return abort || (abortAfter && audio.size() >= abortAfter) ||
                       (abortAt && GetTickCount64() >= abortAt)
                   ? SPVES_ABORT
                   : ((skip || (skipAt && GetTickCount64() >= skipAt)) && !skipDone ? SPVES_SKIP
                                                                                    : 0);
    }
    STDMETHODIMP Write(const void* data, ULONG count, ULONG* written) override {
        if (fail)
            return E_FAIL;
        ++writeCalls;
        if (zeros || (zeroOnWrite && writeCalls == zeroOnWrite)) {
            if (zeros)
                --zeros;
            *written = 0;
            return S_OK;
        }
        if (partial)
            count = std::min(count, partial);
        auto p = static_cast<const BYTE*>(data);
        audio.insert(audio.end(), p, p + count);
        *written = count;
        return S_OK;
    }
    STDMETHODIMP GetRate(long* p) override {
        *p = rate;
        return S_OK;
    }
    STDMETHODIMP GetVolume(USHORT* p) override {
        *p = volume;
        return S_OK;
    }
    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE* type, long* count) override {
        *type = SPVST_SENTENCE;
        *count = skipCount;
        return skipInfoFails ? E_FAIL : S_OK;
    }
    STDMETHODIMP CompleteSkip(long count) override {
        completedSkips.push_back(count);
        skipDone = true;
        return S_OK;
    }
};
static SPVTEXTFRAG fragment(const wchar_t* text, SPVACTIONS action = SPVA_Speak) {
    SPVTEXTFRAG f{};
    f.State.eAction = action;
    f.State.Volume = 100;
    f.pTextStart = text;
    f.ulTextLen = ULONG(wcslen(text));
    return f;
}
static HRESULT speak(ISpTTSEngine* engine, Site& site, SPVTEXTFRAG* fragment) {
    WAVEFORMATEX f{WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
    return engine->Speak(0, SPDFID_WaveFormatEx, &f, fragment, &site);
}
static std::vector<BYTE> say(ISpTTSEngine* engine, const wchar_t* text) {
    Site site;
    auto f = fragment(text);
    okay(speak(engine, site, &f), "Speak");
    require(!site.audio.empty(), "No speech");
    return site.audio;
}

// Regression cases adapted to this original-driver engine from TGSpeechBox's
// SAPI fixes. The site never opens an audio device or registers a real voice.
static void hostRegressions(ISpTTSEngine* engine) {
    unsigned failures = 0;
    auto check = [&](const char* name, auto body) {
        try {
            body();
            printf("PASS host: %s\n", name);
        } catch (const std::exception& error) {
            ++failures;
            printf("FAIL host: %s: %s\n", name, error.what());
        }
    };
    check("partial writes and a later zero write", [&] {
        auto expected = say(engine, L"Hello there.");
        Site site;
        site.partial = 62;
        site.zeroOnWrite = 5;
        auto text = fragment(L"Hello there.");
        okay(speak(engine, site, &text), "Partial write");
        require(site.audio == expected, "Lost or repeated samples");
    });
    check("application and markup volume multiply", [&] {
        auto expected = say(engine, L"Hello there.");
        Site site;
        site.volume = 50;
        auto text = fragment(L"Hello there.");
        text.State.Volume = 50;
        okay(speak(engine, site, &text), "Combined volume");
        require(site.audio.size() == expected.size(), "Volume duration");
        for (size_t i = 0; i < expected.size(); i += 2) {
            short x, y;
            memcpy(&x, expected.data() + i, 2);
            memcpy(&y, site.audio.data() + i, 2);
            require(abs(int(x) - 4 * int(y)) <= 2, "Volume does not compose");
        }
    });
    check("application and markup rates add", [&] {
        Site combined, equivalent;
        auto text = fragment(L"Hello there.");
        combined.rate = 2;
        text.State.RateAdj = 2;
        say(engine, L"Ready.");
        okay(speak(engine, combined, &text), "Combined rate");
        equivalent.rate = 4;
        text.State.RateAdj = 0;
        say(engine, L"Ready.");
        okay(speak(engine, equivalent, &text), "Equivalent rate");
        require(combined.audio == equivalent.audio, "Rate does not compose");
    });
    check("spelling uses independent character requests", [&] {
        say(engine, L"Ready.");
        std::vector<BYTE> expected;
        for (const wchar_t* letter : {L"A", L"B", L"C", L"1"}) {
            auto part = say(engine, letter);
            expected.insert(expected.end(), part.begin(), part.end());
        }
        say(engine, L"Ready.");
        Site site;
        auto text = fragment(L"ABC1", SPVA_SpellOut);
        okay(speak(engine, site, &text), "Spell out");
        require(site.audio == expected, "Letters pronounced as one utterance");
    });
    check("silent bookmarks and bookmarks at accepted audio offsets", [&] {
        Site silent;
        auto markOnly = fragment(L"9", SPVA_Bookmark);
        okay(speak(engine, silent, &markOnly), "Silent bookmark");
        require(silent.audio.empty() && silent.marks == std::vector<ULONGLONG>{0},
                "Silent bookmark missing");
        Site site;
        site.partial = 62;
        auto a = fragment(L"Hello "), mark = fragment(L"7", SPVA_Bookmark), b = fragment(L"there.");
        a.pNext = &mark;
        mark.pNext = &b;
        okay(speak(engine, site, &a), "Timed bookmark");
        require(site.marks.size() == 1 && site.marks[0] == site.markWrites[0] &&
                    site.marks[0] > 0 && site.marks[0] < site.audio.size(),
                "Bookmark queued before its audio position");
    });
    check("abort does not queue later bookmarks and next request recovers", [&] {
        auto baseline = say(engine, L"Hello there.");
        Site site;
        site.partial = 62;
        site.abortAfter = 62;
        auto a = fragment(L"Hello "), mark = fragment(L"7", SPVA_Bookmark), b = fragment(L"there.");
        a.pNext = &mark;
        mark.pNext = &b;
        okay(speak(engine, site, &a), "Abort");
        require(site.audio.size() == 62 && site.marks.empty(), "Cancelled events leaked");
        require(say(engine, L"Hello there.") == baseline, "Cancel contaminated next request");
    });
    check("skip interrupts synthesis and next request recovers", [&] {
        auto baseline = say(engine, L"Hello there.");
        Site site;
        site.skipAt = GetTickCount64() + 2;
        site.skipCount = 1;
        auto a = fragment(L"This is a longer sentence that should be skipped during synthesis. "),
             mark = fragment(L"2", SPVA_Bookmark), b = fragment(L"Two.");
        a.pNext = &mark;
        mark.pNext = &b;
        okay(speak(engine, site, &a), "Skip synthesis");
        require(site.completedSkips == std::vector<long>{1} &&
                    site.markNames == std::vector<std::wstring>{L"2"} && site.markWrites[0] == 0,
                "Skipped synthesis leaked audio or lost destination");
        require(say(engine, L"Hello there.") == baseline, "Skip contaminated next request");
    });
    auto navigation = [&](long count, const wchar_t* trigger, std::vector<std::wstring> names,
                          long completed, bool badInfo = false) {
        Site site;
        site.skipCount = count;
        site.skip = !*trigger;
        site.skipOnMark = trigger;
        site.skipInfoFails = badInfo;
        auto m1 = fragment(L"1", SPVA_Bookmark), a = fragment(L"One. "),
             m2 = fragment(L"2", SPVA_Bookmark), b = fragment(L"Two. "),
             m3 = fragment(L"3", SPVA_Bookmark), c = fragment(L"Three.");
        b.State.RateAdj = 1; // Exercise a bookmark followed by a markup change.
        m1.pNext = &a;
        a.pNext = &m2;
        m2.pNext = &b;
        b.pNext = &m3;
        m3.pNext = &c;
        okay(speak(engine, site, &m1), "Skip Speak");
        require(site.completedSkips == std::vector<long>{completed}, "Wrong CompleteSkip count");
        require(site.markNames == names, "Skip resumed at wrong sentence");
        if (names.empty())
            require(site.audio.empty(), "Incomplete skip continued speaking");
    };
    check("forward sentence skip", [&] { navigation(1, L"", {L"2", L"3"}, 1); });
    check("backward sentence skip",
          [&] { navigation(-1, L"2", {L"1", L"2", L"1", L"2", L"3"}, -1); });
    check("zero skip restarts current sentence",
          [&] { navigation(0, L"2", {L"1", L"2", L"2", L"3"}, 0); });
    check("incomplete forward skip reports actual count and ends",
          [&] { navigation(8, L"", {}, 2); });
    check("incomplete backward skip reports actual count and ends",
          [&] { navigation(-8, L"", {}, 0); });
    check("failed skip query ends cleanly", [&] { navigation(1, L"", {}, 0, true); });
    require(!failures, "SAPI host regressions failed");
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 4)
        return 2;
    HKEY registry = nullptr;
    std::wstring key =
        L"Software\\AccentMessengerTestSession-" + std::to_wstring(GetCurrentProcessId());
    HMODULE dll = nullptr;
    ISpTTSEngine* engine = nullptr;
    ISpObjectToken* token = nullptr;
    IClassFactory* factory = nullptr;
    bool overridden = false;
    int result = 1;
    ISpVoice* voice = nullptr;
    ISpStream* stream = nullptr;
    IStream* memory = nullptr;
    ISpTTSEngine* tokenEngine = nullptr;
    DWORD cookie = 0;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS", argv[2]);
        const auto sharedPath = std::wstring(argv[2]) + L".shared\\settings.toml";
        SetEnvironmentVariableW(L"ACCENT_MESSENGER_SETTINGS_MACHINE", sharedPath.c_str());
        okay(prepareMachineSettings(), "Prepare isolated shared preferences");
        Settings original;
        require(writeSettings(original), "Write settings");
        auto read = readSettings();
        require(read.voice == 5 && read.inflection == 100 && read.volume == 100, "Read defaults");
        // Verify the native English parser against generated Python reference cases.
        std::ifstream fixture(argv[3]);
        std::string line;
        unsigned lexical = 0;
        while (std::getline(fixture, line)) {
            auto tab = line.find('\t'), next = line.find('\t', tab + 1);
            require(tab != std::string::npos && next != std::string::npos, "Fixture");
            std::string text = line.substr(tab + 1, next - tab - 1),
                        expected = line.substr(next + 1);
            auto actual = normalise(std::wstring(text.begin(), text.end()), line[0] == '1');
            if (actual != expected) {
                printf("number case %s: %s != %s\n", text.c_str(), actual.c_str(),
                       expected.c_str());
                throw std::runtime_error("Number parity");
            }
            ++lexical;
        }
        require(lexical > 20, "Number cases missing");
        // Cache system COM factories before redirecting this process's HKLM.
        // Registration tests use a disposable HKCU subtree, never real voices.
        okay(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER, IID_ISpVoice,
                              reinterpret_cast<void**>(&voice)),
             "Windows SpVoice");
        okay(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_INPROC_SERVER, IID_ISpStream,
                              reinterpret_cast<void**>(&stream)),
             "Windows SpStream");
        okay(CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_INPROC_SERVER,
                              IID_ISpObjectToken, reinterpret_cast<void**>(&token)),
             "Windows token");
        require(RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                                nullptr, &registry, nullptr) == ERROR_SUCCESS,
                "Test registry");
        // SAPI also reads machine lexicon/token metadata while preparing speech.
        // Copy that metadata into the isolated tree before hiding the real hive.
        HKEY speechSource = nullptr, speechCopy = nullptr;
        require(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Speech", 0, KEY_READ,
                              &speechSource) == ERROR_SUCCESS,
                "System speech metadata");
        require(RegCreateKeyExW(registry, L"Software\\Microsoft\\Speech", 0, nullptr, 0,
                                KEY_ALL_ACCESS, nullptr, &speechCopy, nullptr) == ERROR_SUCCESS,
                "Scratch speech metadata");
        LSTATUS copied = ERROR_SUCCESS;
        for (auto name : {L"PhoneConverters", L"AppLexicons", L"UserTokens"}) {
            HKEY source = nullptr, target = nullptr;
            auto opened = RegOpenKeyExW(speechSource, name, 0, KEY_READ, &source);
            if (opened == ERROR_FILE_NOT_FOUND)
                continue;
            require(opened == ERROR_SUCCESS, "Read speech category");
            require(RegCreateKeyExW(speechCopy, name, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
                                    &target, nullptr) == ERROR_SUCCESS,
                    "Copy speech category");
            copied = copyMetadata(source, target);
            RegCloseKey(source);
            RegCloseKey(target);
            if (copied) {
                printf("Registry copy error %ld\n", copied);
                break;
            }
        }
        RegCloseKey(speechSource);
        RegCloseKey(speechCopy);
        require(copied == ERROR_SUCCESS, "Copy speech metadata");
        require(RegOverridePredefKey(HKEY_LOCAL_MACHINE, registry) == ERROR_SUCCESS,
                "Registry override");
        overridden = true;
        dll = LoadLibraryExW(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        require(dll != nullptr, "Load engine DLL");
        auto reg =
            reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllRegisterServer"));
        auto unreg = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(
            GetProcAddress(dll, "DllUnregisterServer"));
        auto canUnload =
            reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll, "DllCanUnloadNow"));
        auto getClass = reinterpret_cast<HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**)>(
            GetProcAddress(dll, "DllGetClassObject"));
        require(reg && unreg && canUnload && getClass, "COM exports");
        okay(reg(), "Register");
        okay(
            token->SetId(
                nullptr,
                L"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Speech\\Voices\\Tokens\\AccentMessenger",
                FALSE),
            "Registered token");
        okay(getClass(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory)),
             "Class factory");
        okay(factory->CreateInstance(nullptr, IID_ISpTTSEngine, reinterpret_cast<void**>(&engine)),
             "COM engine");
        require(canUnload() == S_FALSE, "Live COM object not counted");
        ISpObjectWithToken* withToken = nullptr;
        okay(engine->QueryInterface(IID_ISpObjectWithToken, reinterpret_cast<void**>(&withToken)),
             "Token interface");
        auto tokenResult = withToken->SetObjectToken(token);
        withToken->Release();
        okay(tokenResult, "Open original driver");
        GUID formatId;
        WAVEFORMATEX* wave = nullptr;
        okay(engine->GetOutputFormat(nullptr, nullptr, &formatId, &wave), "Format");
        require(formatId == SPDFID_WaveFormatEx && wave->nSamplesPerSec == 16000 &&
                    wave->nChannels == 1 && wave->wBitsPerSample == 16,
                "PCM format");
        CoTaskMemFree(wave);
        hostRegressions(engine);
        auto baseline = say(engine, L"Hello. This is my voice.");
        require(baseline == say(engine, L"Hello. This is my voice."), "Warm voice differs");
        auto a = fragment(L"Hello. "), mark = fragment(L"12", SPVA_Bookmark),
             b = fragment(L"This is my voice.");
        a.pNext = &mark;
        mark.pNext = &b;
        b.ulTextSrcOffset = 7;
        Site grouped;
        okay(speak(engine, grouped, &a), "Grouped fragments");
        require(grouped.audio == baseline && grouped.marks.size() == 1,
                "Bookmark split changed speech");
        require(grouped.marks[0] > 0 && grouped.marks[0] < baseline.size(),
                "Bookmark outside speech");
        auto plain = fragment(L"Hello. This is my voice.");
        Site partial;
        partial.partial = 62;
        partial.zeros = 3;
        okay(speak(engine, partial, &plain), "Backpressure");
        require(partial.audio == baseline, "Partial write lost audio");
        Site abort;
        abort.abortAfter = 640;
        okay(speak(engine, abort, &plain), "Abort playback");
        require(abort.audio.size() == 640, "Abort wrote excess audio");
        Site during;
        during.abortAt = GetTickCount64() + 2;
        auto longText =
            fragment(L"This is a longer sentence that should be cancelled during synthesis.");
        okay(speak(engine, during, &longText), "Abort synthesis");
        require(during.audio.empty(), "Cancelled synthesis leaked audio");
        require(say(engine, L"Hello. This is my voice.") == baseline, "Cancel recovery");
        Site bad;
        bad.fail = true;
        require(FAILED(speak(engine, bad, &plain)), "Write error swallowed");
        require(say(engine, L"Hello. This is my voice.") == baseline, "Write error recovery");
        Site quiet;
        quiet.volume = 50;
        okay(speak(engine, quiet, &plain), "Application volume");
        require(quiet.audio.size() == baseline.size(), "Volume changed length");
        for (size_t i = 0; i < baseline.size(); i += 2) {
            short x, y;
            memcpy(&x, baseline.data() + i, 2);
            memcpy(&y, quiet.audio.data() + i, 2);
            require(abs(int(y) * 2 - int(x)) <= 1, "Volume scaling");
        }
        original.voice = 3;
        original.inflection = 0;
        original.spacing = 2;
        original.readDigits = true;
        require(writeSettings(original), "Changed settings");
        require(say(engine, L"Hello. This is my voice.") != baseline, "Settings not applied");
        require(say(engine, L"100") == say(engine, L"one zero zero"), "Digit setting");
        original = Settings{};
        require(writeSettings(original), "Restore settings");
        require(say(engine, L"Hello. This is my voice.") == baseline, "Settings restoration");
        // The DOS driver retains transition context. Compare normalization with
        // identical preceding speech, rather than confusing that with spelling.
        say(engine, L"Hello. This is my voice.");
        auto number = say(engine, L"100");
        say(engine, L"Hello. This is my voice.");
        auto words = say(engine, L"one hundred");
        if (number != words) {
            printf("Cardinal samples: %zu vs %zu; normalized: [%s] [%s]\n", number.size(),
                   words.size(), normalise(L"100", false).c_str(),
                   normalise(L"one hundred", false).c_str());
            std::ofstream(std::wstring(argv[2]) + L".number", std::ios::binary)
                .write(reinterpret_cast<const char*>(number.data()), number.size());
            std::ofstream(std::wstring(argv[2]) + L".words", std::ios::binary)
                .write(reinterpret_cast<const char*>(words.data()), words.size());
        }
        require(number == words, "Cardinal setting");
        auto e = say(engine, L"E");
        std::ofstream output(std::wstring(argv[2]) + L".pcm", std::ios::binary);
        output.write(reinterpret_cast<const char*>(e.data()), e.size());
        output.close();
        // Exercise Windows' actual SpVoice scheduler with an in-memory stream.
        // This class factory registration is process-local, not a registry change.
        okay(CoRegisterClassObject(clsid, factory, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE,
                                   &cookie),
             "Process-local class factory");
        okay(token->CreateInstance(nullptr, CLSCTX_INPROC_SERVER, IID_ISpTTSEngine,
                                   reinterpret_cast<void**>(&tokenEngine)),
             "Token CreateInstance");
        tokenEngine->Release();
        tokenEngine = nullptr;
        okay(CreateStreamOnHGlobal(nullptr, TRUE, &memory), "Memory stream");
        WAVEFORMATEX pcmFormat{WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
        okay(stream->SetBaseStream(memory, SPDFID_WaveFormatEx, &pcmFormat), "Memory PCM format");
        okay(voice->SetOutput(stream, FALSE), "Memory output");
        okay(voice->SetVoice(token), "Select actual SAPI voice");
        okay(voice->SetRate(0), "SpVoice rate");
        okay(voice->SetVolume(100), "SpVoice volume");
        okay(voice->Speak(L"E", SPF_ASYNC | SPF_IS_NOT_XML, nullptr), "SpVoice Speak");
        require(voice->WaitUntilDone(10000) == S_OK, "SpVoice completion");
        STATSTG stat{};
        okay(memory->Stat(&stat, STATFLAG_NONAME), "Memory PCM size");
        require(stat.cbSize.QuadPart >= e.size(), "SpVoice truncated E");
        LARGE_INTEGER zero{};
        okay(memory->Seek(zero, STREAM_SEEK_SET, nullptr), "Memory rewind");
        std::vector<BYTE> realAudio(size_t(stat.cbSize.QuadPart));
        ULONG actual = 0;
        okay(memory->Read(realAudio.data(), ULONG(realAudio.size()), &actual), "Memory PCM read");
        require(actual == realAudio.size() && std::equal(e.begin(), e.end(), realAudio.begin()),
                "SpVoice differs from engine PCM");
        voice->Release();
        voice = nullptr;
        stream->Release();
        stream = nullptr;
        memory->Release();
        memory = nullptr;
        okay(CoRevokeClassObject(cookie), "Revoke process-local class factory");
        cookie = 0;
        printf("checking invalid settings\n");
        {
            std::ofstream invalid(argv[2]);
            invalid << "Voice = 99\nInflection = bad\nVolume = 7 wrong\nRate = 75 # valid\n";
        }
        read = readSettings();
        require(read.voice == 5 && read.inflection == 100 && read.volume == 100 && read.rate == 75,
                "Invalid TOML fallback");
        printf("releasing engine\n");
        engine->Release();
        engine = nullptr;
        printf("releasing factory and token\n");
        factory->Release();
        factory = nullptr;
        token->Release();
        token = nullptr;
        require(canUnload() == S_OK, "COM object leak");
        okay(unreg(), "Unregister");
        require(GetFileAttributesW(sharedPath.c_str()) != INVALID_FILE_ATTRIBUTES,
                "Unregister removed shared preferences");
        HKEY removed = nullptr;
        require(RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              L"Software\\Microsoft\\Speech\\Voices\\Tokens\\AccentMessenger", 0,
                              KEY_READ, &removed) == ERROR_FILE_NOT_FOUND,
                "Token not removed");
        printf("PASS: %u lexical cases; COM registration, PCM, grouping, backpressure, "
               "cancellation, settings, events and volume.\n",
               lexical);
        result = 0;
    } catch (const std::exception& e) {
        printf("FAIL: %s\n", e.what());
    }
    if (tokenEngine)
        tokenEngine->Release();
    if (voice)
        voice->Release();
    if (stream)
        stream->Release();
    if (memory)
        memory->Release();
    if (cookie)
        CoRevokeClassObject(cookie);
    if (engine)
        engine->Release();
    if (factory)
        factory->Release();
    if (token)
        token->Release();
    if (dll)
        FreeLibrary(dll);
    if (overridden)
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    if (registry)
        RegCloseKey(registry);
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    CoUninitialize();
    return result;
}
