#include "MtlGpio.h"
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>

// PADCFG0 bits (pinctrl-intel.c)
#define PADCFG0_GPIROUTIOXAPIC   (1u << 20)
#define PADCFG0_GPIROUTSCI       (1u << 19)
#define PADCFG0_GPIROUTSMI       (1u << 18)
#define PADCFG0_GPIROUTNMI       (1u << 17)
#define PADCFG0_PMODE_MASK       (0xfu << 10)
#define PADCFG0_GPIORXDIS        (1u << 9)
#define PADCFG0_GPIOTXDIS        (1u << 8)
#define PADCFG0_GPIORXSTATE      (1u << 1)
#define PADCFG0_GPIOTXSTATE      (1u << 0)
#define PADBAR                   0x00c

// Group register bases per community from mtlp_community*_gpps / MTL_P_* and
// the DSDT GPCM table (columns: count, padcfg, hostsw, padown, ?, lock, locktx, base, idx):
//   com1 GPP_E: {0x19, 0x0790, 0x0144, 0xC0, 0x0234, 0x0118, 0x011C, 0x80, 4}
//   com5 GPP_B: {0x19, 0x0600, 0x0140, 0xB0, 0x0230, 0x0110, 0x0114, 0x160, 11}
//   com5 GPP_D: {0x19, 0x0790, 0x0144, 0xC0, 0x0234, 0x0118, 0x011C, 0x180, 12}
const MtlPad kPadCs1   = { "GPP_E_2", 0x82, 1, 0x790 + 2 * 0x10, 0xC0, 8, 0x144, 0x118, 2 };
const MtlPad kPadReset = { "GPP_D_1", 0x81, 4, 0x790 + 1 * 0x10, 0xC0, 4, 0x144, 0x118, 1 };
const MtlPad kPadSpkId = { "GPP_B_8", 0x68, 4, 0x600 + 8 * 0x10, 0xB4, 0, 0x140, 0x110, 8 };
const MtlPad kPadIrq   = { "GPP_D_2", 0x82, 4, 0x790 + 2 * 0x10, 0xC0, 8, 0x144, 0x118, 2 };
// (acpiPin keeps only the low byte for logging: 0x082, 0x181, 0x168, 0x182)

static inline uint32_t rd(volatile uint8_t *b, uint32_t off) { return *(volatile uint32_t *)(b + off); }
static inline void wr(volatile uint8_t *b, uint32_t off, uint32_t v) { *(volatile uint32_t *)(b + off) = v; }

MtlGpio::MtlGpio() : dev(nullptr), map1(nullptr), map5(nullptr), com1(nullptr), com5(nullptr) {}

bool MtlGpio::init() {
    // GPI0 _HID is INTC1083 on this board (DSDT: PCHS != 4, ARLM != 1).
    const char *hids[] = { "INTC1083", "INTC105E" };
    for (int i = 0; i < 2 && !dev; i++) {
        OSDictionary *m = IOService::nameMatching(hids[i]);
        if (!m) continue;
        IOService *s = IOService::waitForMatchingService(m, 5ULL * 1000 * 1000 * 1000);  // does not consume m; returns +1
        m->release();
        if (s) { dev = s; CS_LOG("GPIO: found %s (%s)", hids[i], s->getName()); }
    }
    if (!dev) { CS_LOG("GPIO: INTC1083 ACPI device not found"); return false; }

    IOItemCount n = dev->getDeviceMemoryCount();
    CS_LOG("GPIO: %u memory ranges", (unsigned)n);
    for (IOItemCount i = 0; i < n; i++) {
        IODeviceMemory *m = dev->getDeviceMemoryWithIndex((unsigned)i);
        if (m) CS_LOG("GPIO:  mem[%u] %#llx len %#llx", (unsigned)i, (unsigned long long)m->getPhysicalAddress(),
                      (unsigned long long)m->getLength());
    }
    if (n < 5) { CS_LOG("GPIO: expected 5 communities"); release(); return false; }

    // Sanity: community 1 at SBREG+0xD20000, community 5 at SBREG+0xD50000.
    IODeviceMemory *m1 = dev->getDeviceMemoryWithIndex(1), *m5 = dev->getDeviceMemoryWithIndex(4);
    if (!m1 || !m5 || (m1->getPhysicalAddress() & 0xFF0000) != 0xD20000 || (m5->getPhysicalAddress() & 0xFF0000) != 0xD50000 ||
        m1->getLength() < 0x1000 || m5->getLength() < 0x1000) {
        CS_LOG("GPIO: community ranges do not look like MTL-P com1/com5, refusing");
        release();
        return false;
    }
    map1 = dev->mapDeviceMemoryWithIndex(1, kIOMapInhibitCache);
    map5 = dev->mapDeviceMemoryWithIndex(4, kIOMapInhibitCache);
    if (!map1 || !map5) { CS_LOG("GPIO: mapping failed"); release(); return false; }
    com1 = (volatile uint8_t *)map1->getVirtualAddress();
    com5 = (volatile uint8_t *)map5->getVirtualAddress();

    uint32_t pb1 = rd(com1, PADBAR), pb5 = rd(com5, PADBAR);
    CS_LOG("GPIO: com1 PADBAR=%#x com5 PADBAR=%#x", pb1, pb5);
    if (pb1 != 0x600 || pb5 != 0x600) { CS_LOG("GPIO: unexpected PADBAR, refusing"); release(); return false; }
    return true;
}

void MtlGpio::release() {
    if (map1) { map1->release(); map1 = nullptr; }
    if (map5) { map5->release(); map5 = nullptr; }
    com1 = com5 = nullptr;
    if (dev) { dev->release(); dev = nullptr; }   // waitForMatchingService returned it retained
}

volatile uint8_t *MtlGpio::base(uint8_t memIdx) {
    return memIdx == 1 ? com1 : (memIdx == 4 ? com5 : nullptr);
}

bool MtlGpio::readDw0(const MtlPad &p, uint32_t *dw0) {
    volatile uint8_t *b = base(p.memIdx);
    if (!b) return false;
    *dw0 = rd(b, p.padcfg);
    return true;
}

bool MtlGpio::ownedByHost(const MtlPad &p) {
    volatile uint8_t *b = base(p.memIdx);
    return b && ((rd(b, p.padown) >> p.padownShift) & 0xf) == 0;   // intel_pad_owned_by_host()
}

void MtlGpio::locks(const MtlPad &p, bool *locked, bool *lockedTx) {
    volatile uint8_t *b = base(p.memIdx);
    *locked = (rd(b, p.lock) >> p.bit) & 1;
    *lockedTx = (rd(b, p.lock + 4) >> p.bit) & 1;
}

void MtlGpio::logPad(const MtlPad &p) {
    volatile uint8_t *b = base(p.memIdx);
    if (!b) return;
    bool l, lt;
    locks(p, &l, &lt);
    uint32_t dw0 = rd(b, p.padcfg), dw1 = rd(b, p.padcfg + 4);
    CS_LOG("GPIO: %s DW0=%#010x DW1=%#010x PMODE=%u TXDIS=%u RXDIS=%u TX=%u RX=%u own=%s hostsw=%u lock=%u locktx=%u",
           p.name, dw0, dw1, (dw0 & PADCFG0_PMODE_MASK) >> 10, !!(dw0 & PADCFG0_GPIOTXDIS), !!(dw0 & PADCFG0_GPIORXDIS),
           dw0 & 1, !!(dw0 & PADCFG0_GPIORXSTATE), ownedByHost(p) ? "host" : "OTHER",
           (rd(b, p.hostsw) >> p.bit) & 1, l, lt);
}

int MtlGpio::setOutput(const MtlPad &p, bool high) {
    volatile uint8_t *b = base(p.memIdx);
    if (!b) return CS_ENODEV;
    if (!ownedByHost(p)) { CS_LOG("GPIO: %s not owned by host", p.name); return CS_EACCES; }
    bool l, lt;
    locks(p, &l, &lt);
    uint32_t v = rd(b, p.padcfg);
    uint32_t nv = v;
    // intel_gpio_request_enable(): switch to GPIO mode only if not already.
    if (nv & PADCFG0_PMODE_MASK) {
        nv &= ~PADCFG0_PMODE_MASK;
        nv &= ~(PADCFG0_GPIROUTIOXAPIC | PADCFG0_GPIROUTSCI | PADCFG0_GPIROUTSMI | PADCFG0_GPIROUTNMI);
    }
    // __intel_gpio_set_direction(value, input=false, output=true)
    nv |= PADCFG0_GPIORXDIS;
    nv &= ~PADCFG0_GPIOTXDIS;
    nv = high ? (nv | PADCFG0_GPIOTXSTATE) : (nv & ~PADCFG0_GPIOTXSTATE);
    if (nv == v) return CS_OK;
    if (l && ((nv ^ v) & ~PADCFG0_GPIOTXSTATE)) { CS_LOG("GPIO: %s config locked (DW0=%#x)", p.name, v); return CS_EACCES; }
    if (lt && ((nv ^ v) & PADCFG0_GPIOTXSTATE)) { CS_LOG("GPIO: %s TX locked (DW0=%#x)", p.name, v); return CS_EACCES; }
    wr(b, p.padcfg, nv);
    (void)rd(b, p.padcfg);   // flush posted write
    return CS_OK;
}

void MtlGpio::setLevel(const MtlPad &p, bool high) {
    volatile uint8_t *b = base(p.memIdx);
    if (!b) return;
    uint32_t v = rd(b, p.padcfg);
    uint32_t nv = high ? (v | PADCFG0_GPIOTXSTATE) : (v & ~PADCFG0_GPIOTXSTATE);
    if (nv != v) { wr(b, p.padcfg, nv); (void)rd(b, p.padcfg); }
}

int MtlGpio::getInput(const MtlPad &p, int *level) {
    volatile uint8_t *b = base(p.memIdx);
    if (!b) return CS_ENODEV;
    if (!ownedByHost(p)) { CS_LOG("GPIO: %s not owned by host", p.name); return CS_EACCES; }
    uint32_t v = rd(b, p.padcfg);
    // intel_gpio_get(): if TX buffer enabled, report TXSTATE; else RXSTATE.
    if ((v & PADCFG0_PMODE_MASK) || (v & PADCFG0_GPIORXDIS)) {
        bool l, lt;
        locks(p, &l, &lt);
        if (l) { CS_LOG("GPIO: %s locked, cannot make it an input", p.name); return CS_EACCES; }
        uint32_t nv = v & ~PADCFG0_PMODE_MASK;
        nv &= ~(PADCFG0_GPIROUTIOXAPIC | PADCFG0_GPIROUTSCI | PADCFG0_GPIROUTSMI | PADCFG0_GPIROUTNMI);
        nv &= ~PADCFG0_GPIORXDIS;   // input: enable RX
        nv |= PADCFG0_GPIOTXDIS;    // disable TX
        wr(b, p.padcfg, nv);
        (void)rd(b, p.padcfg);
        IODelay(10);
        v = rd(b, p.padcfg);
    }
    *level = (v & PADCFG0_GPIOTXDIS) ? !!(v & PADCFG0_GPIORXSTATE) : (int)(v & PADCFG0_GPIOTXSTATE);
    return CS_OK;
}
