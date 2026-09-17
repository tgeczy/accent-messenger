#include "frontend.h"
#include "messenger.h"
#include "renderer.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>

struct Engine {
    std::atomic<uint32_t> generation{0};
    std::vector<uint8_t> driver;
    std::unique_ptr<Frontend> frontend;
    std::array<double, 10> parameters;
    std::vector<uint8_t> frames;
    std::vector<int16_t> pcm;
    std::string error;
    int lastIntonation = 0, lastSpacing = 0;
    Engine(const uint8_t* data, uint32_t size, const double* p)
        : driver(data, data + size), frontend(std::make_unique<Frontend>(data, size, generation)) {
        for (int i = 0; i < 10; i++) {
            if (!std::isfinite(p[i]) || (i >= 7 && p[i] <= 0))
                throw std::runtime_error("Invalid calibration");
            parameters[i] = p[i];
        }
    }
};
void* msg_create(const uint8_t* data, uint32_t size, const double* p) {
    if (!data || !p)
        return nullptr;
    try {
        return new Engine(data, size, p);
    } catch (...) {
        return nullptr;
    }
}
void msg_destroy(void* instance) {
    delete static_cast<Engine*>(instance);
}
void msg_cancel(void* instance, uint32_t generation) {
    if (instance)
        static_cast<Engine*>(instance)->generation.store(generation, std::memory_order_relaxed);
}
int msg_synthesize(void* instance, uint32_t generation, const char* text, int rate, int pitch,
                   int volume) {
    return msg_synthesize_options(instance, generation, text, rate, pitch, volume, 5, 0, 0, 100);
}
int msg_synthesize_options(void* instance, uint32_t generation, const char* text, int rate,
                           int pitch, int volume, int voice, int intonation, int spacing,
                           int outputGain) {
    if (!instance)
        return -1;
    auto& e = *static_cast<Engine*>(instance);
    e.pcm.clear();
    e.frames.clear();
    e.error.clear();
    bool optionsSubmitted = false;
    try {
        if (!text || std::strlen(text) > 1200 || rate < 0 || rate > 17 || pitch < 0 || pitch > 9 ||
            volume < 0 || volume > 100 || voice < 0 || voice > 9 || intonation < 0 ||
            intonation > 4 || spacing < 0 || spacing > 9 || outputGain < 100 || outputGain > 200)
            throw std::runtime_error("Invalid synthesis request");
        if (e.generation.load(std::memory_order_relaxed) != generation)
            throw Cancelled();
        if (!*text || !volume)
            return 0;
        if (!e.frontend) {
            e.frontend = std::make_unique<Frontend>(e.driver.data(), e.driver.size(), e.generation);
            e.lastIntonation = e.lastSpacing = 0;
        }
        std::string command = "\x1b"
                              "A9\x1b"
                              "V" +
                              std::to_string(voice) +
                              "\x1b"
                              "P" +
                              std::to_string(pitch) +
                              "\x1b"
                              "R" +
                              "0123456789ABCDEFGH"[rate];
        // Reissuing options emits preparation records. Keep the approved default
        // command stream intact, and send an option only when its value changes.
        if (intonation != e.lastIntonation) {
            command += "\x1b"
                       "M" +
                       std::to_string(intonation);
            optionsSubmitted = true;
        }
        if (spacing != e.lastSpacing) {
            command += "\x1b"
                       "S" +
                       std::to_string(spacing);
            optionsSubmitted = true;
        }
        e.frames = e.frontend->speak(command + text, generation);
        e.lastIntonation = intonation;
        e.lastSpacing = spacing;
        // The original isolated E begins with half master level. Tomi approved
        // removing that onset dip for the letter alone, without altering words.
        auto renderedFrames = e.frames;
        if (std::strcmp(text, "E") == 0 || std::strcmp(text, "e") == 0 ||
            std::strcmp(text, "E.") == 0 || std::strcmp(text, "e.") == 0) {
            for (size_t i = 0; i < renderedFrames.size(); i += 48) {
                if (renderedFrames[i + 2] && renderedFrames[i + 42]) {
                    renderedFrames[i + 42] = 255;
                    break;
                }
            }
        }
        e.pcm = render(renderedFrames, e.parameters.data(), volume, e.generation, generation,
                       outputGain / 100.0);
        return 0;
    } catch (const Cancelled&) {
        // Cancellation can occur before submission or after the DOS transaction.
        // Reassert changed settings on the next request if completion was unseen.
        if (optionsSubmitted)
            e.lastIntonation = e.lastSpacing = -1;
        e.pcm.clear();
        e.frames.clear();
        return 1;
    } catch (const std::exception& x) {
        e.pcm.clear();
        e.frontend.reset();
        e.error = x.what();
        return -1;
    } catch (...) {
        e.pcm.clear();
        e.frontend.reset();
        e.error = "Unknown native engine error";
        return -1;
    }
}
const int16_t* msg_pcm(void* instance, uint32_t* count) {
    if (!instance || !count)
        return nullptr;
    auto& p = static_cast<Engine*>(instance)->pcm;
    *count = uint32_t(p.size());
    return p.data();
}
const uint8_t* msg_frames(void* instance, uint32_t* count) {
    if (!instance || !count)
        return nullptr;
    auto& p = static_cast<Engine*>(instance)->frames;
    *count = uint32_t(p.size());
    return p.data();
}
const char* msg_error(void* instance) {
    return instance ? static_cast<Engine*>(instance)->error.c_str()
                    : "Engine initialization failed";
}
uint32_t msg_api_version() {
    return 2;
}
