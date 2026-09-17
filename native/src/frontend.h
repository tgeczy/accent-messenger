#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <unicorn/unicorn.h>
#include <vector>

struct Cancelled {};
class Frontend {
  public:
    Frontend(const uint8_t* image, size_t size, std::atomic<uint32_t>& generation);
    ~Frontend();
    std::vector<uint8_t> speak(const std::string& text, uint32_t ticket);

  private:
    uc_engine* uc = nullptr;
    std::atomic<uint32_t>& generation;
    uint32_t ticket = 0;
    uint64_t blocks = 0;
    uint64_t poll = 0;
    bool resident = false;
    bool idle = false;
    uint16_t nextSegment = 0x5000;
    uint8_t port21 = 255, portA1 = 255;
    std::string error;
    std::vector<uint8_t> frames;
    uint16_t reg(int id);
    void reg(int id, uint16_t value);
    uint8_t byte(uint32_t address);
    uint16_t word(uint32_t address);
    void word(uint32_t address, uint16_t value);
    void dispatch(int number);
    void fail(const char* message);
    void run(uint64_t budget);
    static void blockHook(uc_engine*, uint64_t, uint32_t, void*);
    static void interruptHook(uc_engine*, uint32_t, void*);
    static uint32_t inputHook(uc_engine*, uint32_t, int, void*);
    static void outputHook(uc_engine*, uint32_t, int, uint32_t, void*);
};
