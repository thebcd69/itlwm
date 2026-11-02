// Polled (PIO) driver for the Intel LPSS SPI (PXA2xx SSP core, "LPSS_CNL_SSP"
// type) found on Meteor Lake-P at 00:1e.2 (8086:7E27). Ported from Linux
// drivers/spi/spi-pxa2xx.c and drivers/mfd/intel-lpss.c (tgl_spi_info:
// LPSS_CNL_SSP, 100 MHz input clock).
#pragma once
#include "Platform.h"

class IOPCIDevice;
class IOMemoryMap;
class MtlGpio;

struct SpiSeg {
    const uint8_t *tx;   // may be null (sends 0x00)
    uint8_t *rx;         // may be null (discard)
    size_t len;
};

class LpssSpi {
public:
    LpssSpi();
    // speedHz: wanted SCLK (DSDT: 4 MHz). gpio: used for CS1 (cs-gpios).
    bool init(IOPCIDevice *pci, uint32_t speedHz, MtlGpio *gpio, bool clockUnityFix);
    void release();
    // Re-run controller init after a power transition.
    bool reinit();
    // One CS-asserted message made of several segments (like spi_message).
    int transfer(int cs, const SpiSeg *segs, int nsegs);

private:
    uint32_t rd(uint32_t off);
    void wr(uint32_t off, uint32_t v);
    void csAssert(int cs);
    void csDeassert(int cs);
    bool flush();
    bool setupController();

    IOPCIDevice *pci;
    IOMemoryMap *map;
    volatile uint8_t *regs;
    uint64_t barPhys;
    MtlGpio *gpio;
    uint32_t speed;
    uint32_t clkRate;
    uint32_t cr0, cr1;
    bool clkFix;
    bool ready;
};
