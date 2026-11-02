#include "LpssSpi.h"
#include "MtlGpio.h"
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>

// SSP registers (include/linux/pxa2xx_ssp.h)
#define SSCR0   0x00
#define SSCR1   0x04
#define SSSR    0x08
#define SSDR    0x10
#define SSTO    0x28
#define SSPSP   0x2C
#define SSITF   0x44
#define SSIRF   0x48
#define SSCR0_DataSize(x)   ((x) - 1)
#define SSCR0_Motorola      (0x0 << 4)
#define SSCR0_SSE           (1u << 7)
#define SSCR0_SCR(x)        ((uint32_t)(x) << 8)
#define SSCR1_SPO           (1u << 3)
#define SSCR1_SPH           (1u << 4)
#define SSCR1_TxTresh(x)    (((x) - 1) << 6)
#define SSCR1_RxTresh(x)    (((x) - 1) << 10)
#define SSSR_TNF            (1u << 2)
#define SSSR_RNE            (1u << 3)
#define SSSR_BSY            (1u << 4)
#define SSSR_ROR            (1u << 7)
#define RX_THRESH_DFLT      8
#define TX_THRESH_DFLT      8
#define SSITF_TxHiThresh(x) (((x) - 1) << 0)
#define SSITF_TxLoThresh(x) (((x) - 1) << 8)
#define SSIRF_RxThresh(x)   ((x) - 1)

// LPSS private space (intel-lpss.c LPSS_PRIV_OFFSET 0x200; spi-pxa2xx lpss_platforms[LPSS_CNL_SSP])
#define LPSS_PRIV               0x200
#define LPSS_PRIV_CLOCK_PARAMS  (LPSS_PRIV + 0x00)   // bit0 enable, M[15:1], N[30:16], bit31 update
#define LPSS_PRIV_RESETS        (LPSS_PRIV + 0x04)
#define LPSS_PRIV_RESETS_IDMA   (1u << 2)
#define LPSS_PRIV_RESETS_FUNC   0x3
#define LPSS_PRIV_SSP_REG       (LPSS_PRIV + 0x20)
#define LPSS_PRIV_SSP_REG_DIS_DMA_FIN (1u << 0)
#define LPSS_PRIV_CS_CTRL       (LPSS_PRIV + 0x24)
#define LPSS_CS_CONTROL_SW_MODE (1u << 0)
#define LPSS_CS_CONTROL_CS_HIGH (1u << 1)
#define LPSS_CS_SEL_SHIFT       8
#define LPSS_CS_SEL_MASK        (3u << 8)
#define LPSS_PRIV_CLOCK_GATE    (LPSS_PRIV + 0x38)
#define LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK      0x3
#define LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_ON  0x3
#define LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_OFF 0x0
#define LPSS_PRIV_REMAP_ADDR    (LPSS_PRIV + 0x40)
#define LPSS_PRIV_CAPS          (LPSS_PRIV + 0xfc)
#define LPSS_PRIV_CAPS_NO_IDMA  (1u << 8)
#define LPSS_CAPS_CS_EN_SHIFT   9
#define LPSS_CAPS_CS_EN_MASK    (0xfu << 9)

#define LPSS_INPUT_CLK_HZ       100000000u   // tgl_spi_info.clk_rate (used for 0x7e27)

// Bounded busy-waits: each poll step ~1 us.
#define SPI_POLL_LIMIT          20000        // 20 ms per byte, far above 2 us/byte at 4 MHz

LpssSpi::LpssSpi()
    : pci(nullptr), map(nullptr), regs(nullptr), barPhys(0), gpio(nullptr), speed(0), clkRate(0),
      cr0(0), cr1(0), clkFix(false), ready(false) {}

uint32_t LpssSpi::rd(uint32_t off) { return *(volatile uint32_t *)(regs + off); }
void LpssSpi::wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(regs + off) = v; }

bool LpssSpi::init(IOPCIDevice *p, uint32_t speedHz, MtlGpio *g, bool clockUnityFix) {
    pci = p; speed = speedHz; gpio = g; clkFix = clockUnityFix;

    uint16_t vid = pci->configRead16(kIOPCIConfigVendorID), did = pci->configRead16(kIOPCIConfigDeviceID);
    if (vid != 0x8086 || did != 0x7e27) { CS_LOG("SPI: unexpected PCI id %04x:%04x", vid, did); return false; }

    // PCI PM: make sure the function is in D0 (intel-lpss relies on PCI core for this).
    uint8_t pmOff = 0;
    if (pci->findPCICapability(kIOPCIPowerManagementCapability, &pmOff) && pmOff) {
        uint16_t pmcsr = pci->configRead16(pmOff + 4);
        CS_LOG("SPI: PMCSR=%#x (D%u)", pmcsr, pmcsr & 3);
        if (pmcsr & 3) {
            pci->configWrite16(pmOff + 4, (uint16_t)(pmcsr & ~3u));
            IOSleep(10);   // D3hot -> D0 recovery time
        }
    }
    pci->setMemoryEnable(true);

    IODeviceMemory *mem = pci->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!mem) { CS_LOG("SPI: no BAR0"); return false; }
    barPhys = mem->getPhysicalAddress();
    map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0, kIOMapInhibitCache);
    if (!map) { CS_LOG("SPI: BAR0 map failed"); return false; }
    regs = (volatile uint8_t *)map->getVirtualAddress();
    CS_LOG("SPI: BAR0 phys %#llx len %#llx", (unsigned long long)barPhys, (unsigned long long)map->getLength());
    if (map->getLength() < 0x300) { CS_LOG("SPI: BAR0 too small"); release(); return false; }

    return setupController();
}

bool LpssSpi::reinit() {
    if (!regs) return false;
    return setupController();
}

bool LpssSpi::setupController() {
    ready = false;
    uint32_t caps = rd(LPSS_PRIV_CAPS);
    if (caps == 0xffffffff) { CS_LOG("SPI: controller reads all-ones (powered off?)"); return false; }

    // intel_lpss_init_dev(): reset, de-assert reset, remap address, DMA fin.
    wr(LPSS_PRIV_RESETS, 0);
    wr(LPSS_PRIV_RESETS, LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA);
    wr(LPSS_PRIV_REMAP_ADDR, (uint32_t)barPhys);           // lo_hi_writeq()
    wr(LPSS_PRIV_REMAP_ADDR + 4, (uint32_t)(barPhys >> 32));
    if (!(caps & LPSS_PRIV_CAPS_NO_IDMA))
        wr(LPSS_PRIV_SSP_REG, LPSS_PRIV_SSP_REG_DIS_DMA_FIN);

    // Clock tree: root 100 MHz -> gate(bit0) -> fractional divider M/N -> update(bit31).
    // Linux keeps the preset M/N (no QUIRK_CLOCK_DIVIDER_UNITY for tgl_spi_info).
    uint32_t cp = rd(LPSS_PRIV_CLOCK_PARAMS);
    uint32_t m = (cp >> 1) & 0x7fff, n = (cp >> 16) & 0x7fff;
    if (clkFix && (!m || !n)) { m = 1; n = 1; cp = (cp & 0x80000001u) | (m << 1) | (n << 16); CS_LOG("SPI: clock divider forced to 1/1"); }
    clkRate = (m && n) ? (uint32_t)(((uint64_t)LPSS_INPUT_CLK_HZ * m) / n) : LPSS_INPUT_CLK_HZ;
    wr(LPSS_PRIV_CLOCK_PARAMS, cp | 1u | (1u << 31));
    CS_LOG("SPI: CAPS=%#x CLOCK_PARAMS %#x -> %#x (M=%u N=%u, ssp_clk=%u Hz) CS_CTRL=%#x",
           caps, cp, rd(LPSS_PRIV_CLOCK_PARAMS), m, n, clkRate, rd(LPSS_PRIV_CS_CTRL));

    // pxa2xx_spi_probe(): default SSP configuration.
    wr(SSCR0, rd(SSCR0) & ~SSCR0_SSE);
    wr(SSCR1, SSCR1_RxTresh(RX_THRESH_DFLT) | SSCR1_TxTresh(TX_THRESH_DFLT));
    wr(SSCR0, SSCR0_Motorola | SSCR0_DataSize(8) | SSCR0_SCR(2));
    wr(SSTO, 0);
    wr(SSPSP, 0);

    // lpss_ssp_setup(): software chip-select control, CS high (inactive).
    uint32_t cs = rd(LPSS_PRIV_CS_CTRL);
    cs |= LPSS_CS_CONTROL_SW_MODE | LPSS_CS_CONTROL_CS_HIGH;
    wr(LPSS_PRIV_CS_CTRL, cs);
    uint32_t csen = (caps & LPSS_CAPS_CS_EN_MASK) >> LPSS_CAPS_CS_EN_SHIFT;
    int ncs = 0;
    while (ncs < 4 && (csen & (1u << ncs))) ncs++;   // ffz()
    CS_LOG("SPI: native chip selects: %d", ncs);

    // Per-transfer configuration (pxa2xx_spi_transfer_one): mode 0, 8 bits.
    uint32_t div = (clkRate + speed - 1) / speed;   // DIV_ROUND_UP(ssp_clk, rate)
    if (div < 1) div = 1;
    div = (div - 1) & 0xfff;
    cr0 = SSCR0_SCR(div) | SSCR0_Motorola | SSCR0_DataSize(8);
    cr1 = SSCR1_RxTresh(RX_THRESH_DFLT) | SSCR1_TxTresh(TX_THRESH_DFLT);   // SPO=0, SPH=0
    wr(SSIRF, SSIRF_RxThresh(1));                               // lpss_platforms[CNL].rx_threshold
    wr(SSITF, SSITF_TxLoThresh(32) | SSITF_TxHiThresh(56));     // tx_threshold_lo/hi
    wr(SSTO, 1000);   // TIMOUT_DFLT
    wr(SSCR1, cr1);
    wr(SSCR0, cr0);
    wr(SSCR0, cr0 | SSCR0_SSE);
    CS_LOG("SPI: SCLK %u Hz (SCR=%u) SSCR0=%#x SSCR1=%#x SSSR=%#x", clkRate / (div + 1), div, rd(SSCR0), rd(SSCR1), rd(SSSR));

    // Make sure CS1 (GPIO) idles high.
    if (gpio && gpio->setOutput(kPadCs1, true) != CS_OK) {
        CS_LOG("SPI: cannot drive CS1 GPIO, refusing");
        return false;
    }
    ready = flush();
    if (!ready) CS_LOG("SPI: flush failed");
    return ready;
}

void LpssSpi::release() {
    if (regs) {
        // Leave the SSP disabled and chip selects inactive.
        wr(SSCR0, rd(SSCR0) & ~SSCR0_SSE);
        wr(LPSS_PRIV_CS_CTRL, rd(LPSS_PRIV_CS_CTRL) | LPSS_CS_CONTROL_CS_HIGH);
        if (gpio) gpio->setLevel(kPadCs1, true);
    }
    if (map) { map->release(); map = nullptr; }
    regs = nullptr;
    ready = false;
}

bool LpssSpi::flush() {
    // pxa2xx_spi_flush(), bounded.
    for (int i = 0; i < SPI_POLL_LIMIT; i++) {
        while (rd(SSSR) & SSSR_RNE) (void)rd(SSDR);
        if (!(rd(SSSR) & SSSR_BSY)) { wr(SSSR, SSSR_ROR); return true; }
        IODelay(1);
    }
    return false;
}

void LpssSpi::csAssert(int cs) {
    if (cs == 0) {
        // lpss_ssp_cs_control(enable): select CS0 then drive it low.
        uint32_t v = rd(LPSS_PRIV_CS_CTRL);
        uint32_t nv = (v & ~LPSS_CS_SEL_MASK) | (0u << LPSS_CS_SEL_SHIFT);
        if (nv != v) { wr(LPSS_PRIV_CS_CTRL, nv); IODelay(1); }   // >= 2 ssp_clk cycles
        wr(LPSS_PRIV_CS_CTRL, nv & ~LPSS_CS_CONTROL_CS_HIGH);
        // cs_clk_stays_gated: poke the clock gate so CS actually moves.
        uint32_t g = rd(LPSS_PRIV_CLOCK_GATE);
        if ((g & LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) != LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_ON) {
            wr(LPSS_PRIV_CLOCK_GATE, (g & ~LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) | LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_ON);
            wr(LPSS_PRIV_CLOCK_GATE, (g & ~LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) | LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_OFF);
        }
    } else {
        // CS1 is a GPIO (cs-gpios); ACPI => active low, controller set_cs not called.
        gpio->setLevel(kPadCs1, false);
    }
}

void LpssSpi::csDeassert(int cs) {
    // Wait until SSP becomes idle before deasserting (cs_deassert, 10 ms bound).
    for (int i = 0; i < 10000 && (rd(SSSR) & SSSR_BSY); i++) IODelay(1);
    if (cs == 0) {
        uint32_t v = rd(LPSS_PRIV_CS_CTRL);
        wr(LPSS_PRIV_CS_CTRL, v | LPSS_CS_CONTROL_CS_HIGH);
        uint32_t g = rd(LPSS_PRIV_CLOCK_GATE);
        if ((g & LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) != LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_ON) {
            wr(LPSS_PRIV_CLOCK_GATE, (g & ~LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) | LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_ON);
            wr(LPSS_PRIV_CLOCK_GATE, (g & ~LPSS_PRIV_CLOCK_GATE_CLK_CTL_MASK) | LPSS_PRIV_CLOCK_GATE_CLK_CTL_FORCE_OFF);
        }
    } else {
        gpio->setLevel(kPadCs1, true);
    }
}

int LpssSpi::transfer(int cs, const SpiSeg *segs, int nsegs) {
    if (!ready || !regs) return CS_ENODEV;
    if (cs != 0 && cs != 1) return CS_EINVAL;
    if (cs == 1 && !gpio) return CS_ENODEV;
    if (!flush()) { CS_LOG("SPI: flush failed"); return CS_EIO; }

    csAssert(cs);
    int ret = CS_OK;
    // Lock-step PIO: one byte out, one byte in. Simple and never overruns the RX FIFO.
    for (int s = 0; s < nsegs && ret == CS_OK; s++) {
        for (size_t i = 0; i < segs[s].len; i++) {
            int n = 0;
            while (!(rd(SSSR) & SSSR_TNF)) { if (++n > SPI_POLL_LIMIT) { ret = CS_ETIMEDOUT; break; } IODelay(1); }
            if (ret) break;
            wr(SSDR, segs[s].tx ? segs[s].tx[i] : 0);
            n = 0;
            while (!(rd(SSSR) & SSSR_RNE)) { if (++n > SPI_POLL_LIMIT) { ret = CS_ETIMEDOUT; break; } IODelay(1); }
            if (ret) break;
            uint8_t b = (uint8_t)rd(SSDR);
            if (segs[s].rx) segs[s].rx[i] = b;
        }
    }
    uint32_t sssr = rd(SSSR);
    csDeassert(cs);
    if (sssr & SSSR_ROR) { wr(SSSR, SSSR_ROR); if (!ret) ret = CS_EIO; }
    if (ret) CS_LOG("SPI: transfer on CS%d failed: %d (SSSR=%#x)", cs, ret, sssr);
    return ret;
}
