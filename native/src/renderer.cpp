// Native implementation of the approved experimental_synth.py clear profile.
// This is an acoustic reconstruction, not recovered Aicom DSP firmware.
#include "renderer.h"
#include "filter_coefficients.h"
#include "frontend.h"
#include "noise.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace {
constexpr double pi = 3.14159265358979323846, fs = 16000;
using Controls = std::array<double, 10>;
using Frequencies = std::array<double, 5>;
double clip(double v, double low, double high) {
    return std::max(low, std::min(high, v));
}
struct Smooth {
    double a, z;
    Smooth(double ms, double initial) : a(1 - std::exp(-1000 / (fs * ms))), z(initial * (1 - a)) {}
    double step(double v) {
        double result = a * v + z;
        z = (1 - a) * result;
        return result;
    }
    void setTime(double ms) {
        double next = 1 - std::exp(-1000 / (fs * ms));
        if (next != a) {
            z = (z / (1 - a)) * (1 - next);
            a = next;
        }
    }
};
struct Band {
    const double (*sos)[6];
    double z[2][2] = {};
    explicit Band(int i) : sos(bands[i]) {}
    double step(double x) {
        for (int i = 0; i < 2; i++) {
            auto b = sos[i];
            double y = b[0] * x + z[i][0];
            z[i][0] = b[1] * x - b[4] * y + z[i][1];
            z[i][1] = b[2] * x - b[5] * y;
            x = y;
        }
        return x;
    }
};
int duration(const uint8_t* f) {
    return 64 * ((f[0] & 3) + 1) * ((f[1] & 15) + 1);
}
bool excitation(const uint8_t* f) {
    return f[2] || f[3] || f[44] || f[45] || f[46];
}
void check(const std::atomic<uint32_t>& gen, uint32_t ticket) {
    if (gen.load(std::memory_order_relaxed) != ticket)
        throw Cancelled();
}
} // namespace
std::vector<int16_t> render(const std::vector<uint8_t>& frames, const double* p, int volume,
                            const std::atomic<uint32_t>& gen, uint32_t ticket, double outputGain) {
    if (frames.size() % 48)
        throw std::runtime_error("Incomplete driver frames");
    int count = int(frames.size() / 48), first = -1, last = -1;
    for (int i = 0; i < count; i++) {
        auto f = &frames[i * 48];
        if (f[42] && excitation(f)) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0 || !volume)
        return {};
    for (int i = 0; i < first; i++)
        if (excitation(&frames[i * 48])) {
            first = i;
            break;
        }
    const int originalFirst = first;
    for (int i = 0; i < first; i++)
        if (frames[i * 48 + 42]) {
            // Leading W records can prepare formants with no excitation.
            first = i;
            break;
        }
    size_t preparationSamples = 0;
    for (int i = first; i < originalFirst; i++)
        preparationSamples += duration(&frames[i * 48]);
    std::vector<Controls> control;
    const int fields[] = {2, 3, 12, 20, 28, 41, 42, 44, 45, 46};
    for (int i = first; i <= last; i++) {
        Controls c;
        for (int k = 0; k < 10; k++)
            c[k] = frames[i * 48 + fields[k]];
        // The driver scales byte 3's LOW nibble, preserving its upper field
        // (image 789A..78F8). Do not turn e.g. C3 into 195 units of breath.
        c[1] = frames[i * 48 + 3] & 0x0f;
        control.insert(control.end(), duration(&frames[i * 48]), c);
        if (control.size() > 16000 * 180)
            throw std::runtime_error("Speech exceeds 180 second render limit");
    }
    Controls release = control.back();
    for (int k : {0, 1, 6, 7, 8, 9})
        release[k] = 0;
    control.insert(control.end(), 960, release);
    size_t n = control.size();
    std::vector<Frequencies> feedback(n);
    std::vector<double> pitch(n), tonal, noisy;
    const double bandwidth[] = {60, 140, 150, 220, 270};
    double radii[5];
    for (int k = 0; k < 5; k++)
        radii[k] = std::exp(-pi * bandwidth[k] / fs);
    Smooth f1(p[7], control[0][2]), f2(p[8], control[0][3]), f3(p[9], control[0][4]);
    double previousF3 = control[0][4];
    // The stored P record has the extreme 255 target. Its long F3 glide
    // produced an unwanted extra vowel. Retain the fitted timing elsewhere.
    if (previousF3 == 255)
        f3.setTime(10);
    Smooth pitchFilter(8, clip(1.3604379448 * control[0][5] + 53.520929659, 55, 220));
    for (size_t i = 0; i < n; i++) {
        if (i % 1024 == 0)
            check(gen, ticket);
        if (control[i][4] != previousF3) {
            f3.setTime(control[i][4] == 255 || previousF3 == 255 ? 10 : p[9]);
            previousF3 = control[i][4];
        }
        double effective[] = {f1.step(control[i][2]), f2.step(control[i][3]),
                              f3.step(control[i][4]), 0, 0};
        for (int k = 0; k < 5; k++) {
            double frequency =
                k < 3 ? clip(p[k + 3] + p[k] * std::sqrt(std::max(0.0, 256 - effective[k])), 120,
                             4500)
                      : (k == 3 ? 3300 : 3700);
            feedback[i][k] = 2 * radii[k] * std::cos(2 * pi * frequency / fs);
        }
        pitch[i] = pitchFilter.step(clip(1.3604379448 * control[i][5] + 53.520929659, 55, 220));
    }
    // The reference samples the steady-state gain every 80 samples and linearly
    // interpolates. Preserve that grid rather than altering the voice's envelope.
    for (size_t i = 0; i < n; i += 80) {
        check(gen, ticket);
        double t = 0, w = 0;
        for (int harmonic = 1; harmonic <= 128; harmonic++) {
            double hz = pitch[i] * harmonic;
            std::complex<double> z = std::exp(std::complex<double>(0, -2 * pi * hz / fs)), h = 1;
            for (int k = 0; k < 5; k++)
                h *= (1 - radii[k]) / (1.0 - feedback[i][k] * z + radii[k] * radii[k] * z * z);
            double a = hz < .45 * fs ? 2 / (pi * harmonic) : 0;
            t += std::norm(h * a);
        }
        for (int j = 0; j <= 256; j++) {
            std::complex<double> z = std::exp(std::complex<double>(0, -pi * j / 256)), h = 1;
            for (int k = 0; k < 5; k++)
                h *= (1 - radii[k]) / (1.0 - feedback[i][k] * z + radii[k] * radii[k] * z * z);
            w += std::norm(h);
        }
        tonal.push_back(std::max(1e-8, std::sqrt(t / 2)));
        noisy.push_back(std::max(1e-8, std::sqrt(w / 257)));
    }
    // Tomi selected the -3 dB noise audition. Keep periodic excitation unchanged.
    constexpr double noiseScale = 0.7079457843841379;
    Smooth voice(4, 0), breath(4, 0), gain(3, 0), mix(2, 0), slevel(3, 0);
    Smooth levels[] = {Smooth(3, 0), Smooth(3, 0), Smooth(3, 0)};
    Band filters[] = {Band(0), Band(1), Band(2), Band(3)};
    double norms[] = {std::sqrt(2600 / fs), std::sqrt(4000 / fs), std::sqrt(6000 / fs),
                      std::sqrt(2200 / fs)};
    Noise noise;
    double phaseClock = 0, previous[5] = {}, older[5] = {};
    // Run the extra silent preparation through the model, but do not send
    // it to playback. In particular, one must not gain 48 ms of leading silence.
    std::vector<int16_t> pcm(n - preparationSamples);
    size_t fade = std::min(size_t(128), n);
    for (size_t i = 0; i < n; i++) {
        if (i % 1024 == 0)
            check(gen, ticket);
        const auto& c = control[i];
        size_t at = i / 80, next = std::min(at + 1, tonal.size() - 1);
        double fraction = (i % 80) / 80.0;
        double tg = tonal[at] + fraction * (tonal[next] - tonal[at]),
               ng = noisy[at] + fraction * (noisy[next] - noisy[at]);
        double dt = pitch[i] / fs;
        phaseClock += dt;
        double phase = std::fmod(phaseClock, 1.0), exc = 2 * phase - 1;
        if (phase < dt) {
            double t = phase / dt;
            exc -= 2 * t - t * t - 1;
        }
        if (phase > 1 - dt) {
            double t = (phase - 1) / dt;
            exc -= t * t + 2 * t + 1;
        }
        double white = noise.normal(),
               // Reference-guided control law, listening-approved on sonorants.
               // Preserve zero excitation and anchor E (control 89) to its
               // previous level. This does not measure the audio envelope.
            sample = exc * voice.step(.09 * std::sqrt(89.0 / 95) * std::pow(c[0] / 89, .2)) / tg +
                     white * (breath.step(.012 * std::sqrt(c[1])) * noiseScale) / ng;
        for (int k = 0; k < 5; k++) {
            double value = (1 - radii[k]) * sample + feedback[i][k] * previous[k] -
                           radii[k] * radii[k] * older[k];
            older[k] = previous[k];
            previous[k] = value;
            sample = value;
        }
        double frication = 0;
        for (int k = 0; k < 3; k++)
            frication +=
                filters[k].step(white) / norms[k] * levels[k].step(.035 * std::sqrt(c[k + 7] / 64));
        double blend = mix.step(c[9] > 0 && c[9] >= 4 * c[7] && std::abs(c[8] - c[9]) <= 1 ? 1 : 0);
        double sibilant = filters[3].step(white) / norms[3];
        frication = frication * (1 - blend) +
                    sibilant * slevel.step(.035 * std::sqrt((c[7] + c[8] + c[9]) / 64)) * blend;
        double output = (sample + frication * noiseScale) * gain.step(c[6] / 255);
        // Fixed final gain preserves the approved envelope and filter behavior.
        output = .8 * std::tanh(output / .8) * (volume / 100.0) * outputGain;
        if (i >= n - fade)
            output *= double(n - 1 - i) / double(fade - 1);
        if (!std::isfinite(output))
            throw std::runtime_error("Non-finite audio");
        if (i >= preparationSamples)
            pcm[i - preparationSamples] = int16_t(std::nearbyint(clip(output, -1, 1) * 32767));
    }
    return pcm;
}
