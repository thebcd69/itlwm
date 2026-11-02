// Host-side test of the CS35L56 core against the real linux-firmware files.
// Build: make -C tests FW=<dir with wmfw/bin>
// A small amp simulator models:
//  - status/mailbox registers so that probe/fw_load/play follow the happy path
//  - XM/YM as 24-bit word arrays visible through both the packed
//    (3 bytes/word) and unpacked24 (1 word/reg) windows, so the halo
//    algorithm header written by the wmfw (packed) is read back through the
//    unpacked window exactly like on hardware. A correct port must then
//    find the 0xbedead terminator and every bin block's algorithm.
#include "../src/Cs35l56.h"
#include "../src/Cs35l56Regs.h"
#include <map>
#include <vector>
#include <string>
#include <time.h>

static uint64_t fake_us = 0;
void cs_sleep_ms(uint32_t ms) { fake_us += (uint64_t)ms * 1000; }
void cs_delay_us(uint32_t us) { fake_us += us; }
uint64_t cs_now_us() { return fake_us += 10; }

struct Mem24 {
    // HALO packed window: 4 x 24-bit words little-endian in 96 bits held by
    // 3 consecutive 32-bit registers (r0 | r1<<32 | r2<<64). Derived from the
    // GU605 wmfw itself: decoding its XM header this way yields
    // vendor=0x2, id=0x1a00d6, ver=0x030b10 (fw id also used by the .bin).
    uint32_t packed_base, unpacked_base;
    std::vector<uint32_t> w;  // 24-bit words
    Mem24(uint32_t pb, uint32_t ub, size_t n) : packed_base(pb), unpacked_base(ub), w(n, 0) {}
    bool packedHit(uint32_t reg) const { return reg >= packed_base && reg < packed_base + w.size() * 3; }
    bool unpackedHit(uint32_t reg) const { return reg >= unpacked_base && reg < unpacked_base + w.size() * 4; }
    void packedWriteReg(uint32_t reg, uint32_t val) {
        uint32_t off = reg - packed_base, g = off / 12, r = (off % 12) / 4;
        for (int bit = 0; bit < 32; bit++) {
            uint32_t pos = r * 32 + bit, word = g * 4 + pos / 24, wb = pos % 24;
            if (word >= w.size()) continue;
            w[word] = (w[word] & ~(1u << wb)) | (((val >> bit) & 1u) << wb);
        }
    }
    uint32_t packedReadReg(uint32_t reg) const {
        uint32_t off = reg - packed_base, g = off / 12, r = (off % 12) / 4, v = 0;
        for (int bit = 0; bit < 32; bit++) {
            uint32_t pos = r * 32 + bit, word = g * 4 + pos / 24, wb = pos % 24;
            if (word < w.size()) v |= ((w[word] >> wb) & 1u) << bit;
        }
        return v;
    }
};

class SimAmp : public CsRegBus {
public:
    std::map<uint32_t, uint32_t> regs;
    Mem24 xm{CS35L56_DSP1_XMEM_PACKED_0, CS35L56_DSP1_XMEM_UNPACKED24_0, 0x8000};
    Mem24 ym{CS35L56_DSP1_YMEM_PACKED_0, CS35L56_DSP1_YMEM_UNPACKED24_0, 0x8000};
    std::vector<uint8_t> pm = std::vector<uint8_t>(0x20000, 0);
    std::map<uint32_t, int> writesPerReg;
    uint32_t rawBytes = 0, nWrites = 0, nReads = 0;
    bool shutdown = false, fwMissing = true;
    std::vector<std::string> mboxLog;

    SimAmp() {
        regs[CS35L56_DEVID] = 0x35A56; regs[CS35L56_REVID] = 0xB0; regs[CS35L56_OTPID] = 0x3;
        regs[CS35L56_DSP_RESTRICT_STS1] = 0;
        regs[CS35L56_DSP1_HALO_STATE] = 2;
        regs[CS35L56_DSP_VIRTUAL1_MBOX_2] = 0x1;
        regs[CS35L56_IRQ1_MASK_1] = 0x83ffffff; regs[CS35L56_IRQ1_MASK_8] = 0xfc000fff;
    }
    uint32_t get(uint32_t reg) {
        // status registers live inside the XM/YM windows: special-case first
        if (reg == CS35L56_DSP1_FW_VER) return 0x030b10;
        if (reg == CS35L56_DSP1_PM_CUR_STATE) return shutdown ? 1 : 3;
        if (reg == CS35L56_DSP1_HALO_STATE) return 2;
        if (reg == CS35L56_PROTECTION_STATUS) return fwMissing ? 1 : 0;
        if (reg == CS35L56_TRANSDUCER_ACTUAL_PS) return 0;
        if (xm.unpackedHit(reg)) return xm.w[(reg - xm.unpacked_base) / 4];
        if (ym.unpackedHit(reg)) return ym.w[(reg - ym.unpacked_base) / 4];
        auto it = regs.find(reg);
        return it == regs.end() ? 0 : it->second;
    }
    int write(uint32_t reg, uint32_t val) override {
        nWrites++; writesPerReg[reg]++;
        if (reg & 3) { printf("UNALIGNED write %#x\n", reg); exit(2); }
        if (reg == CS35L56_DSP_VIRTUAL1_MBOX_1) {
            char b[32]; snprintf(b, sizeof b, "%#x", val); mboxLog.push_back(b);
            if (val == CS35L56_MBOX_CMD_SHUTDOWN) shutdown = true;
            if (val == CS35L56_MBOX_CMD_SYSTEM_RESET) shutdown = false;
            regs[reg] = 0;  // fw acks immediately
            return CS_OK;
        }
        if (reg == CS35L56_PROTECTION_STATUS) { fwMissing = val & 1; return CS_OK; }
        if (xm.unpackedHit(reg)) { xm.w[(reg - xm.unpacked_base) / 4] = val & 0xffffff; return CS_OK; }
        if (ym.unpackedHit(reg)) { ym.w[(reg - ym.unpacked_base) / 4] = val & 0xffffff; return CS_OK; }
        regs[reg] = val;
        return CS_OK;
    }
    int read(uint32_t reg, uint32_t *val) override { nReads++; *val = get(reg); return CS_OK; }
    int rawWrite(uint32_t reg, const uint8_t *b, size_t len) override {
        if ((reg & 3) || (len & 3)) { printf("BAD raw write %#x len %zu\n", reg, len); exit(2); }
        if (reg < CS35L56_DSP1_XMEM_PACKED_0) {
            for (size_t i = 0; i < len; i += 4) write(reg + i, cs_be32(b + i));
            return CS_OK;
        }
        if (!shutdown) { printf("raw DSP write %#x while DSP not shut down\n", reg); exit(3); }
        rawBytes += len;
        for (size_t i = 0; i < len; i += 4) {
            uint32_t a = reg + i, v = cs_be32(b + i);
            if (xm.packedHit(a)) xm.packedWriteReg(a, v);
            else if (ym.packedHit(a)) ym.packedWriteReg(a, v);
            else if (a >= CS35L56_DSP1_PMEM_0 && a + 4 <= CS35L56_DSP1_PMEM_0 + pm.size()) memcpy(&pm[a - CS35L56_DSP1_PMEM_0], b + i, 4);
            else if (xm.unpackedHit(a)) xm.w[(a - xm.unpacked_base) / 4] = v & 0xffffff;
            else if (ym.unpackedHit(a)) ym.w[(a - ym.unpacked_base) / 4] = v & 0xffffff;
            else { printf("raw write outside DSP memory %#x\n", a); exit(2); }
        }
        return CS_OK;
    }
    int rawRead(uint32_t reg, uint8_t *b, size_t len) override {
        for (size_t i = 0; i < len; i += 4) {
            uint32_t v;
            if (xm.packedHit(reg + i)) v = xm.packedReadReg(reg + i);
            else if (ym.packedHit(reg + i)) v = ym.packedReadReg(reg + i);
            else v = get(reg + i);
            b[i] = v >> 24; b[i + 1] = v >> 16; b[i + 2] = v >> 8; b[i + 3] = v;
        }
        return CS_OK;
    }
};

static std::vector<uint8_t> slurp(const std::string &p) {
    std::vector<uint8_t> v;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) { printf("cannot open %s\n", p.c_str()); exit(1); }
    uint8_t buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
    fclose(f);
    return v;
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: %s file.wmfw file.bin [...]\n", argv[0]); return 1; }
    auto wmfw = slurp(argv[1]);
    int fails = 0;
    for (int i = 2; i < argc; i++) {
        auto bin = slurp(argv[i]);
        SimAmp sim;
        Cs35l56 amp(&sim, 0, "AMP1");
        int r = amp.probe(true);
        printf("probe -> %d\n", r);
        if (r) { fails++; continue; }
        r = amp.fwLoad(wmfw.data(), wmfw.size(), bin.data(), bin.size());
        printf("fwLoad(%s) -> %d, raw bytes %u, writes %u\n", argv[i], r, sim.rawBytes, sim.nWrites);
        if (r) { fails++; continue; }
        r = amp.setVolumeQdB(-24);
        r |= amp.play();
        printf("play -> %d, ASP1_ENABLES1=%#x VOL=%#x MUTE=%u\n", r, sim.get(CS35L56_ASP1_ENABLES1),
               sim.get(CS35L56_MAIN_RENDER_USER_VOLUME), sim.get(CS35L56_MAIN_RENDER_USER_MUTE));
        if (r) fails++;
        if (sim.get(CS35L56_MAIN_RENDER_USER_VOLUME) != (((uint32_t)-24 << 6) & 0xFFC0)) { printf("volume mismatch\n"); fails++; }
        printf("mbox:"); for (auto &s : sim.mboxLog) printf(" %s", s.c_str()); printf("\n");
        amp.failSafe();
        if (sim.get(CS35L56_MAIN_RENDER_USER_MUTE) != 1 || sim.get(CS35L56_ASP1_ENABLES1) != 0) { printf("failSafe did not mute\n"); fails++; }
    }
    printf("RESULT: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
