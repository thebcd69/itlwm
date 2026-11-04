// CS35L56Amp: standalone (Lilu-free) IOKit driver that brings up the two
// Cirrus Logic CS35L56 speaker amplifiers of the ASUS GU605M (SSID 1043:1C63)
// over the Meteor Lake LPSS SPI0 controller.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>
#include "LpssSpi.h"
#include "MtlGpio.h"
#include "CsBus.h"
#include "HdaDump.h"

class Cs35l56;

// CsRegBus over one SPI chip select (Linux cs35l56_regmap_spi wire format).
class SpiAmpBus : public CsRegBus {
public:
    SpiAmpBus() : spi(nullptr), cs(0), errors(0) {}
    void setup(LpssSpi *s, int c) { spi = s; cs = c; }
    int write(uint32_t reg, uint32_t val) override;
    int read(uint32_t reg, uint32_t *val) override;
    int rawWrite(uint32_t reg, const uint8_t *buf, size_t len) override;
    int rawRead(uint32_t reg, uint8_t *buf, size_t len) override;
    uint32_t errorCount() const { return errors; }
private:
    LpssSpi *spi;
    int cs;
    uint32_t errors;
};

class CS35L56Amp : public IOService {
    OSDeclareDefaultStructors(CS35L56Amp)
public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn setPowerState(unsigned long state, IOService *whatDevice) override;

private:
    static void workerEntry(thread_call_param_t p0, thread_call_param_t p1);
    void worker();
    bool bringUp();
    void failAll(const char *why);
    void holdReset();
    void scheduleWorker(uint32_t delayMs);
    void parseBootArgs();
    void publish(const char *key, const char *val);
    void publishLog();
    void publishNum(const char *key, uint64_t val);

    IOPCIDevice *pci = nullptr;
    IOLock *lock = nullptr;
    thread_call_t call = nullptr;
    volatile bool stopping = false;
    volatile bool needBringUp = true;
    volatile bool asleep = false;
    bool gpioOk = false;
    bool spiOk = false;
    bool resetControlled = false;
    bool running = false;
    int bringUpAttempts = 0;
    int replayAttempts = 0;
    uint32_t ticks = 0;

    // boot-args
    int volAttenDb = 6;        // cs35l56_vol=<dB attenuation>, 0..40, default 6 => -6 dB
    int spkIdOverride = -1;    // cs35l56_spkid=0|1
    bool dbg = false;          // -cs35l56dbg
    bool noPlay = false;       // -cs35l56noplay
    bool noReset = false;      // -cs35l56noreset
    bool clkFix = false;       // -cs35l56clkfix
    bool codecDump = false;    // -cs35l56codecdump (read-only HDA verbs)
    bool follow = false;       // -cs35l56follow: PLAY/PAUSE with the AppleHDA output engine
    bool volTrack = true;      // amp gain follows the macOS output volume (-cs35l56novoltrack disables)
    int lastVolQdb = 1;        // last gain written by volTick (1 = none yet)
    int lastMute = -1;
    IOService *volCtl = nullptr, *muteCtl = nullptr;
    void volTick();
    IOService *findOutputEngine();
    int idleTicks = 0;
    IOService *engine = nullptr;
    bool engineRunning();
    void followTick();
    bool hdaOk = false;
    HdaDump hda;

    MtlGpio gpio;
    LpssSpi spi;
    SpiAmpBus bus[2];
    Cs35l56 *amp[2] = { nullptr, nullptr };
    int spkId = 0;
};
