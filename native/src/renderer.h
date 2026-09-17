#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
std::vector<int16_t> render(const std::vector<uint8_t>& frames, const double* parameters,
                            int volume, const std::atomic<uint32_t>& generation, uint32_t ticket,
                            double outputGain = 1.0);
