#pragma once
#include "settings.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sapiddk.h>
#include <thread>
#include <vector>
namespace messenger_sapi {
class Runtime {
    void* engine = nullptr;
    uint32_t generation = 0;
    std::thread worker;
    std::mutex lock;
    std::condition_variable wake;
    bool stopping = false, pending = false;
    std::atomic<bool> done{true};
    std::string jobText;
    Settings jobSettings;
    int jobRate = 5, jobPitch = 5, result = -1;
    uint32_t jobGeneration = 0;

  public:
    ~Runtime();
    void close();
    HRESULT open(HMODULE module);
    HRESULT synthesize(const std::string& text, const Settings& settings, int rate, int pitch,
                       ISpTTSEngineSite* site, std::vector<short>& pcm, bool& aborted);
};
} // namespace messenger_sapi
