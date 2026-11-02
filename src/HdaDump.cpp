#include "HdaDump.h"
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>

#define HDA_ICOI 0x60
#define HDA_IRII 0x64
#define HDA_ICS  0x68
#define ICS_ICB  (1u << 0)
#define ICS_IRV  (1u << 1)

static inline uint16_t rd16(volatile uint8_t *b, uint32_t o) { return *(volatile uint16_t *)(b + o); }
static inline void wr16(volatile uint8_t *b, uint32_t o, uint16_t v) { *(volatile uint16_t *)(b + o) = v; }

bool HdaDump::init() {
    OSDictionary *m = IOService::nameMatching("HDEF");
    if (!m) return false;
    IOService *s = IOService::waitForMatchingService(m, 2ULL * 1000 * 1000 * 1000);
    dev = OSDynamicCast(IOPCIDevice, s);
    if (!dev) { if (s) s->release(); CS_LOG("HDA: HDEF not found"); return false; }
    uint16_t vid = dev->configRead16(kIOPCIConfigVendorID), did = dev->configRead16(kIOPCIConfigDeviceID);
    if (vid != 0x8086 || did != 0x7e28) { CS_LOG("HDA: unexpected HDEF id %04x:%04x", vid, did); release(); return false; }
    map = dev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0, kIOMapInhibitCache);
    if (!map) { CS_LOG("HDA: BAR0 map failed"); release(); return false; }
    regs = (volatile uint8_t *)map->getVirtualAddress();
    uint32_t vendor = param(0, 0x00);
    uint32_t sub = param(0, 0x04);
    afgStart = (sub >> 16) & 0xff;
    uint32_t r = 0;
    if (!verb(0x01, 0xF20, 0, &r)) r = 0;   // subsystem id
    CS_LOG("HDA: codec0 vendor %#x, root sub %#x, AFG ssid %#x", vendor, sub, r);
    uint32_t afgSub = param(0x01, 0x04);
    afgStart = (afgSub >> 16) & 0xff;
    afgCount = afgSub & 0xff;
    CS_LOG("HDA: AFG widgets %#x..%#x", afgStart, afgStart + afgCount - 1);
    return vendor == 0x10ec0285;
}

void HdaDump::release() {
    if (map) { map->release(); map = nullptr; }
    regs = nullptr;
    if (dev) { dev->release(); dev = nullptr; }
}

bool HdaDump::verb4(uint32_t nid, uint32_t v4, uint32_t p16, uint32_t *resp) {
    return verb(nid, (v4 << 8) | ((p16 >> 8) & 0xff), p16 & 0xff, resp);
}

bool HdaDump::verb(uint32_t nid, uint32_t verbId, uint32_t payload, uint32_t *resp) {
    if (!regs || failures > 8) return false;
    // Only GET verbs (0xFxx, 0xB for amp gain, 0xA for converter format) are allowed.
    uint32_t top = verbId >> 8;
    if (!(top == 0xF || (verbId >> 8) == 0xB || (verbId >> 8) == 0xA)) return false;
    int n = 0;
    while (rd16(regs, HDA_ICS) & ICS_ICB) { if (++n > 1000) { failures++; return false; } IODelay(1); }
    wr16(regs, HDA_ICS, ICS_IRV);   // clear stale IRV (RW1C)
    uint32_t cmd = (0u << 28) | ((nid & 0x7f) << 20) | ((verbId & 0xfff) << 8) | (payload & 0xff);
    *(volatile uint32_t *)(regs + HDA_ICOI) = cmd;
    wr16(regs, HDA_ICS, ICS_ICB);
    n = 0;
    while (1) {
        uint16_t ics = rd16(regs, HDA_ICS);
        if (!(ics & ICS_ICB) && (ics & ICS_IRV)) break;
        if (++n > 2000) { failures++; CS_LOG("HDA: immediate command timeout (ICS=%#x)", ics); return false; }
        IODelay(1);
    }
    *resp = *(volatile uint32_t *)(regs + HDA_IRII);
    wr16(regs, HDA_ICS, ICS_IRV);
    return true;
}

uint32_t HdaDump::param(uint32_t nid, uint32_t p) {
    uint32_t r = 0;
    return verb(nid, 0xF00, p, &r) ? r : 0xffffffff;
}

void HdaDump::dumpAll() {
    if (!regs) return;
    for (uint32_t nid = afgStart; nid < afgStart + afgCount && nid < 0x80; nid++) {
        uint32_t caps = param(nid, 0x09);
        uint32_t type = (caps >> 20) & 0xf;
        uint32_t cfg = 0, ctl = 0, sel = 0, eapd = 0, pincap = 0, clen = 0, conn[2] = { 0, 0 }, pwr = 0;
        clen = (caps & (1u << 8)) ? param(nid, 0x0E) : 0;
        if (clen & 0x7f) { verb(nid, 0xF02, 0, &conn[0]); if ((clen & 0x7f) > 4) verb(nid, 0xF02, 4, &conn[1]); }
        verb(nid, 0xF01, 0, &sel);
        verb(nid, 0xF05, 0, &pwr);
        if (type == 4) {   // pin complex
            verb(nid, 0xF1C, 0, &cfg);
            verb(nid, 0xF07, 0, &ctl);
            verb(nid, 0xF0C, 0, &eapd);
            pincap = param(nid, 0x0C);
        }
        CS_LOG("HDA: nid %#04x type %u caps %#010x cfg %#010x pinctl %#x eapd %#x pincap %#x conn(len %u) %08x %08x sel %u pwr %#x",
               nid, type, caps, cfg, ctl, eapd, pincap, clen & 0x7f, conn[0], conn[1], sel, pwr);
    }
}

void HdaDump::dumpLive() {
    if (!regs) return;
    static const uint8_t dacs[] = { 0x02, 0x03, 0x06 };
    static const uint8_t pins[] = { 0x14, 0x17, 0x21 };
    for (unsigned i = 0; i < sizeof(dacs); i++) {
        uint32_t sc = 0, fmt = 0, gl = 0, gr = 0;
        verb(dacs[i], 0xF06, 0, &sc);          // stream/channel
        verb4(dacs[i], 0xA, 0, &fmt);          // converter format
        verb4(dacs[i], 0xB, 0xA000, &gl);      // out amp left
        verb4(dacs[i], 0xB, 0x8000, &gr);      // out amp right
        CS_LOG("HDA: live DAC %#04x stream %u ch %u fmt %#06x amp L %#x R %#x", dacs[i], (sc >> 4) & 0xf, sc & 0xf, fmt, gl, gr);
    }
    for (unsigned i = 0; i < sizeof(pins); i++) {
        uint32_t sel = 0, ctl = 0, eapd = 0, sense = 0, pw = 0;
        verb(pins[i], 0xF01, 0, &sel);
        verb(pins[i], 0xF07, 0, &ctl);
        verb(pins[i], 0xF0C, 0, &eapd);
        verb(pins[i], 0xF09, 0, &sense);
        verb(pins[i], 0xF05, 0, &pw);
        CS_LOG("HDA: live pin %#04x sel %u pinctl %#x eapd %#x sense %#x pwr %#x", pins[i], sel, ctl, eapd, sense, pw);
    }
}
