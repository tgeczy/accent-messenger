#include "runtime.h"
#include "messenger.h"
#include "sapi_calibration.h"
#include <algorithm>
#include <atomic>
#include <bcrypt.h>
#include <fstream>
#include <iterator>
#include <thread>

namespace messenger_sapi {
Runtime::~Runtime() {
    close();
}
void Runtime::close() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    wake.notify_one();
    if (worker.joinable())
        worker.join();
}
HRESULT Runtime::open(HMODULE module) {
    if (engine)
        return S_OK;
    if (worker.joinable())
        worker.join();
    stopping = false;
    auto path = modulePath(module);
    path.resize(path.find_last_of(L"\\/"));
    std::ifstream file(path + L"\\..\\data\\SPKMIC.TSR", std::ios::binary);
    if (!file)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    if (data.size() != 163936)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return E_FAIL;
    NTSTATUS status = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0)
        status = BCryptHashData(hash, data.data(), ULONG(data.size()), 0);
    if (status >= 0)
        status = BCryptFinishHash(hash, digest, sizeof digest, 0);
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status < 0 || !std::equal(std::begin(digest), std::end(digest), driverDigest))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    // A single persistent owner creates, runs and destroys this instance,
    // matching NVDA's worker ownership without per-request thread startup.
    done.store(false, std::memory_order_release);
    worker = std::thread([this, data = std::move(data)] {
        engine = msg_create(data.data(), uint32_t(data.size()), calibration);
        done.store(true, std::memory_order_release);
        if (!engine)
            return;
        for (;;) {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [this] { return stopping || pending; });
            if (stopping)
                break;
            pending = false;
            guard.unlock();
            const int modes[] = {1, 2, 3, 4, 0};
            result = msg_synthesize_options(
                engine, jobGeneration, jobText.c_str(), jobRate, jobPitch, 100, jobSettings.voice,
                modes[jobSettings.inflection / 25], jobSettings.spacing, 200);
            done.store(true, std::memory_order_release);
        }
        msg_destroy(engine);
        engine = nullptr;
    });
    while (!done.load(std::memory_order_acquire))
        Sleep(1);
    return engine ? S_OK : E_FAIL;
}
HRESULT Runtime::synthesize(const std::string& text, const Settings& s, int rate, int pitch,
                            ISpTTSEngineSite* site, std::vector<short>& pcm, bool& aborted) {
    {
        std::lock_guard<std::mutex> guard(lock);
        jobText = text;
        jobSettings = s;
        jobRate = rate;
        jobPitch = pitch;
        jobGeneration = generation;
        result = -1;
        done.store(false, std::memory_order_release);
        pending = true;
    }
    wake.notify_one();
    while (!done.load(std::memory_order_acquire)) {
        if (!aborted && (site->GetActions() & SPVES_ABORT)) {
            aborted = true;
            msg_cancel(engine, ++generation);
        }
        Sleep(2);
    }
    if (aborted || (site->GetActions() & SPVES_ABORT)) {
        aborted = true;
        return S_OK;
    }
    if (result != 0)
        return E_FAIL;
    uint32_t count = 0;
    auto data = msg_pcm(engine, &count);
    if (count)
        pcm.insert(pcm.end(), data, data + count);
    return S_OK;
}
} // namespace messenger_sapi
