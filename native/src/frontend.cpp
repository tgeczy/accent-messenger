#include "frontend.h"
#include <cstring>
#include <stdexcept>

namespace {
void checked(uc_err e) {
    if (e != UC_ERR_OK)
        throw std::runtime_error(uc_strerror(e));
}
uint16_t get16(const uint8_t* p) {
    return p[0] | (uint16_t(p[1]) << 8);
}
} // namespace
uint16_t Frontend::reg(int id) {
    uint32_t v = 0;
    checked(uc_reg_read(uc, id, &v));
    return uint16_t(v);
}
void Frontend::reg(int id, uint16_t v) {
    uint32_t value = v;
    checked(uc_reg_write(uc, id, &value));
}
uint8_t Frontend::byte(uint32_t a) {
    uint8_t v = 0;
    checked(uc_mem_read(uc, a, &v, 1));
    return v;
}
uint16_t Frontend::word(uint32_t a) {
    uint8_t v[2];
    checked(uc_mem_read(uc, a, v, 2));
    return get16(v);
}
void Frontend::word(uint32_t a, uint16_t v) {
    uint8_t b[] = {uint8_t(v), uint8_t(v >> 8)};
    checked(uc_mem_write(uc, a, b, 2));
}
void Frontend::fail(const char* s) {
    error = s;
    uc_emu_stop(uc);
}

Frontend::Frontend(const uint8_t* data, size_t size, std::atomic<uint32_t>& g) : generation(g) {
    ticket = generation.load(std::memory_order_relaxed);
    if (size < 28 || get16(data) != 0x5a4d)
        throw std::runtime_error("Invalid DOS image");
    checked(uc_open(UC_ARCH_X86, UC_MODE_16, &uc));
    try {
        checked(uc_mem_map(uc, 0, 0x200000, UC_PROT_ALL));
        size_t header = get16(data + 8) * 16, reloc = get16(data + 24), count = get16(data + 6);
        if (header > size || reloc + count * 4 > size || size - header > 0xd0000)
            throw std::runtime_error("Invalid DOS bounds");
        std::vector<uint8_t> image(data + header, data + size);
        for (size_t i = 0; i < count; i++) {
            size_t at = get16(data + reloc + i * 4) + get16(data + reloc + i * 4 + 2) * 16;
            if (at + 2 > image.size())
                throw std::runtime_error("Invalid relocation");
            uint16_t v = get16(image.data() + at) + 0x1000;
            image[at] = uint8_t(v);
            image[at + 1] = uint8_t(v >> 8);
        }
        checked(uc_mem_write(uc, 0x10000, image.data(), image.size()));
        uint8_t mode = 2;
        checked(uc_mem_write(uc, 0x10057, &mode, 1));
        word(0xff00, 0x20cd);
        word(0xff02, 0x9fff);
        word(0xff80, 0x0d00);
        word(0x413, 640);
        uint8_t iret = 0xcf;
        checked(uc_mem_write(uc, 0xf0000, &iret, 1));
        for (int i = 0; i < 256; i++) {
            word(i * 4, 0);
            word(i * 4 + 2, 0xf000);
        }
        reg(UC_X86_REG_CS, 0x1000 + get16(data + 22));
        reg(UC_X86_REG_IP, get16(data + 20));
        reg(UC_X86_REG_SS, 0x1000 + get16(data + 14));
        reg(UC_X86_REG_SP, get16(data + 16));
        reg(UC_X86_REG_DS, 0xff0);
        reg(UC_X86_REG_ES, 0xff0);
        reg(UC_X86_REG_EFLAGS, 0x202);
        uc_hook hook;
        checked(uc_hook_add(uc, &hook, UC_HOOK_BLOCK, (void*)blockHook, this, 1, 0));
        checked(uc_hook_add(uc, &hook, UC_HOOK_INTR, (void*)interruptHook, this, 1, 0));
        checked(uc_hook_add(uc, &hook, UC_HOOK_INSN, (void*)inputHook, this, 1, 0, UC_X86_INS_IN));
        checked(
            uc_hook_add(uc, &hook, UC_HOOK_INSN, (void*)outputHook, this, 1, 0, UC_X86_INS_OUT));
        run(3000000);
        if (!resident)
            throw std::runtime_error("Driver did not become resident");
    } catch (...) {
        uc_close(uc);
        uc = nullptr;
        throw;
    }
}
Frontend::~Frontend() {
    if (uc)
        uc_close(uc);
}
void Frontend::dispatch(int number) {
    uint16_t off = word(number * 4), seg = word(number * 4 + 2), flags = reg(UC_X86_REG_EFLAGS);
    uint16_t sp = reg(UC_X86_REG_SP), ss = reg(UC_X86_REG_SS);
    for (uint16_t v : {flags, reg(UC_X86_REG_CS), reg(UC_X86_REG_IP)}) {
        sp -= 2;
        word(ss * 16 + sp, v);
    }
    reg(UC_X86_REG_SP, sp);
    reg(UC_X86_REG_EFLAGS, flags & ~0x300);
    reg(UC_X86_REG_CS, seg);
    reg(UC_X86_REG_IP, off);
}
void Frontend::blockHook(uc_engine*, uint64_t address, uint32_t, void* opaque) {
    auto& s = *static_cast<Frontend*>(opaque);
    try {
        ++s.blocks;
        // Finish a submitted DOS transaction to its documented idle boundary.
        // Audio stops immediately in NVDA; stale frames are thrown away before
        // rendering, without disturbing the resident driver.
        bool tick = s.poll && s.blocks % 256 == 0;
        if (address == 0x1015e || address == s.poll || tick) {
            if (s.byte(0x150e6) == 255 && s.byte(0x1a2ec) == 0 &&
                (s.reg(UC_X86_REG_EFLAGS) & 0x200))
                s.dispatch(s.byte(0x10052));
        }
    } catch (const std::exception& e) {
        s.fail(e.what());
    }
}
uint32_t Frontend::inputHook(uc_engine*, uint32_t port, int size, void* opaque) {
    auto& s = *static_cast<Frontend*>(opaque);
    if (size == 1) {
        switch (port) {
        case 0x21:
            return s.port21;
        case 0xa1:
            return s.portA1;
        case 0x3ed:
            return 0x80;
        case 0x3ee:
            return 0x20;
        }
    }
    s.fail("Unsupported port read");
    return 0;
}
void Frontend::outputHook(uc_engine*, uint32_t port, int size, uint32_t value, void* opaque) {
    auto& s = *static_cast<Frontend*>(opaque);
    try {
        if (size == 1) {
            switch (port) {
            case 0x20:
            case 0xa0:
            case 0x3ed:
            case 0x3ee:
            case 0x3ef:
                return;
            case 0x21:
                s.port21 = uint8_t(value);
                return;
            case 0xa1:
                s.portA1 = uint8_t(value);
                return;
            case 0x3ec:
                s.frames.push_back(uint8_t(value));
                return;
            }
        }
        s.fail("Unsupported port write");
    } catch (const std::exception& e) {
        s.fail(e.what());
    }
}
void Frontend::interruptHook(uc_engine*, uint32_t number, void* opaque) {
    auto& s = *static_cast<Frontend*>(opaque);
    try {
        uint16_t ax = s.reg(UC_X86_REG_AX);
        uint8_t ah = uint8_t(ax >> 8), al = uint8_t(ax);
        if (number == 0x21) {
            s.reg(UC_X86_REG_EFLAGS, s.reg(UC_X86_REG_EFLAGS) & ~1);
            switch (ah) {
            case 0x30:
                s.reg(UC_X86_REG_AX, 5);
                s.reg(UC_X86_REG_BX, 0);
                s.reg(UC_X86_REG_CX, 0);
                break;
            case 0x09:
            case 0x02:
            case 0x4a:
                break;
            case 0x35:
                s.reg(UC_X86_REG_ES, s.word(al * 4 + 2));
                s.reg(UC_X86_REG_BX, s.word(al * 4));
                break;
            case 0x25:
                s.word(al * 4, s.reg(UC_X86_REG_DX));
                s.word(al * 4 + 2, s.reg(UC_X86_REG_DS));
                break;
            case 0x48: {
                uint16_t n = s.reg(UC_X86_REG_BX);
                if (uint32_t(s.nextSegment) + n > 0x9fff) {
                    s.reg(UC_X86_REG_EFLAGS, s.reg(UC_X86_REG_EFLAGS) | 1);
                    s.reg(UC_X86_REG_AX, 8);
                    s.reg(UC_X86_REG_BX, 0x9fff - s.nextSegment);
                } else {
                    s.reg(UC_X86_REG_AX, s.nextSegment);
                    s.nextSegment += n + 1;
                }
                break;
            }
            case 0x31:
                s.resident = al == 0;
                uc_emu_stop(s.uc);
                break;
            case 0x4c:
                s.fail("Driver exited");
                break;
            case 0x62:
                s.reg(UC_X86_REG_BX, 0xff0);
                break;
            default:
                s.fail("Unsupported DOS service");
            }
        } else if (number == 0x17) {
            s.dispatch(number);
        } else if (number == 0xf1 && s.poll) {
            s.idle = true;
            uc_emu_stop(s.uc);
        } else {
            s.fail("Unsupported interrupt");
        }
    } catch (const std::exception& e) {
        s.fail(e.what());
    }
}
void Frontend::run(uint64_t budget) {
    error.clear();
    checked(uc_emu_start(uc, reg(UC_X86_REG_CS) * 16 + reg(UC_X86_REG_IP), 0x1fffff, 10000000,
                         size_t(budget)));
    if (!error.empty())
        throw std::runtime_error(error);
    if (poll && !idle)
        throw std::runtime_error("Driver instruction/time budget exhausted");
    if (generation.load(std::memory_order_relaxed) != ticket)
        throw Cancelled();
}
std::vector<uint8_t> Frontend::speak(const std::string& text, uint32_t current) {
    ticket = current;
    if (generation.load(std::memory_order_relaxed) != ticket)
        throw Cancelled();
    {
        std::string request = "\x1b=F" + text + "\r";
        std::vector<uint8_t> stub = {0xba, 2, 0};
        for (uint8_t c : request) {
            for (uint8_t b : {uint8_t(0xb8), c, uint8_t(0), uint8_t(0xcd), uint8_t(0x17)})
                stub.push_back(b);
        }
        poll = 0xe0000 + stub.size();
        const uint8_t tail[] = {0xb8, 0, 2, 0xcd, 0x17, 0x80, 0xfc, 0xd2, 0x75, 0xf6, 0xcd, 0xf1};
        stub.insert(stub.end(), std::begin(tail), std::end(tail));
        // Generated clients replace code at the same guest address. Reclaim the
        // translation buffer at the request boundary as well as invalidating
        // old blocks: range invalidation alone leaves generated code occupying
        // Unicorn's bounded x86 host buffer over a long navigation session.
        checked(uc_ctl_flush_tb(uc));
        checked(uc_mem_write(uc, 0xe0000, stub.data(), stub.size()));
        reg(UC_X86_REG_CS, 0xe000);
        reg(UC_X86_REG_IP, 0);
        reg(UC_X86_REG_SS, 0xe800);
        reg(UC_X86_REG_SP, 0xfffe);
        reg(UC_X86_REG_DS, 0xe000);
        reg(UC_X86_REG_ES, 0xe000);
        reg(UC_X86_REG_EFLAGS, 0x202);
        frames.clear();
        idle = false;
        run(10000000);
        if (!idle)
            throw std::runtime_error("Driver instruction/time budget exhausted");
    }
    return frames;
}
