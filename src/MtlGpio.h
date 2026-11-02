// Direct Meteor Lake-P (INTC1083) GPIO pad access for the few pads the amps
// need. Register layout from Linux drivers/pinctrl/intel/pinctrl-meteorlake.c
// (mtlp_communities) + pinctrl-intel.c, cross-checked with the BIOS GPCM
// table in this machine's DSDT (see docs/HARDWARE.md).
#pragma once
#include "Platform.h"

class IOService;
class IOMemoryMap;

struct MtlPad {
    const char *name;
    uint8_t  acpiPin;    // pin number as used in the DSDT GpioIo() descriptors
    uint8_t  memIdx;     // index into GPI0 _CRS Memory32Fixed list (0=com0 .. 4=com5)
    uint16_t padcfg;     // PADCFG DW0 offset
    uint16_t padown;     // PAD_OWN register
    uint8_t  padownShift;
    uint16_t hostsw;     // HOSTSW_OWN register
    uint16_t lock;       // PADCFGLOCK register (LOCKTX = lock + 4)
    uint8_t  bit;        // pad index within its group (HOSTSW/LOCK bit)
};

extern const MtlPad kPadCs1;     // GPP_E_2  (SPI0 CS1, cs-gpios)
extern const MtlPad kPadReset;   // GPP_D_1  (reset-gpios, shared)
extern const MtlPad kPadSpkId;   // GPP_B_8  (spk-id-gpios)
extern const MtlPad kPadIrq;     // GPP_D_2  (amp IRQ, read-only diagnostics)

class MtlGpio {
public:
    MtlGpio();
    // Finds the INTC1083 ACPI device and maps communities 1 and 5.
    bool init();
    void release();

    bool readDw0(const MtlPad &p, uint32_t *dw0);
    void logPad(const MtlPad &p);
    // Configure as GPIO output with the given level (single DW0 write).
    int setOutput(const MtlPad &p, bool high);
    // Fast path for an already-configured output (CS toggling).
    void setLevel(const MtlPad &p, bool high);
    // Configure as GPIO input if needed and read the level (0/1).
    int getInput(const MtlPad &p, int *level);

private:
    volatile uint8_t *base(uint8_t memIdx);
    bool ownedByHost(const MtlPad &p);
    void locks(const MtlPad &p, bool *locked, bool *lockedTx);

    IOService *dev;
    IOMemoryMap *map1;
    IOMemoryMap *map5;
    volatile uint8_t *com1;
    volatile uint8_t *com5;
};
