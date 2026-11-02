// Opt-in (-cs35l56codecdump) READ-ONLY codec dump through the Intel HDA
// Immediate Command interface (ICOI/IRII/ICS, HDA spec 3.4.3), the same
// mechanism CodecCommander uses next to AppleHDA. Only GET verbs are sent.
#pragma once
#include "Platform.h"

class IOPCIDevice;
class IOMemoryMap;

class HdaDump {
public:
    HdaDump() : dev(nullptr), map(nullptr), regs(nullptr), afgStart(0), afgCount(0), failures(0) {}
    bool init();
    void release();
    void dumpAll();     // once: every widget of the AFG
    void dumpLive();    // short: DAC streams/formats + speaker/HP pin routing
private:
    bool verb(uint32_t nid, uint32_t verbId, uint32_t payload, uint32_t *resp);   // 12-bit verb
    bool verb4(uint32_t nid, uint32_t verbId4, uint32_t payload16, uint32_t *resp);  // 4-bit verb
    uint32_t param(uint32_t nid, uint32_t p);

    IOPCIDevice *dev;
    IOMemoryMap *map;
    volatile uint8_t *regs;
    uint32_t afgStart, afgCount;
    int failures;
};
