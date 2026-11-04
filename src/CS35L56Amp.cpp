#include "CS35L56Amp.hpp"
#include "Cs35l56.h"
#include "Cs35l56Regs.h"
#include "Firmware.h"
#include <pexpert/pexpert.h>
#include <IOKit/IOLib.h>

OSDefineMetaClassAndStructors(CS35L56Amp, IOService)

#ifndef CS35L56_VERSION
#define CS35L56_VERSION "dev"
#endif

// ---------------------------------------------------------------- SPI bus
// cs35l56_regmap_spi: [addr32 BE (|0x80 in byte0 for reads)][pad16][val32 BE]
static void putBe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static bool regOk(uint32_t reg) { return (reg & 3) == 0 && reg <= CS35L56_DSP1_PMEM_5114; }

int SpiAmpBus::write(uint32_t reg, uint32_t val) {
    if (!regOk(reg)) return CS_EINVAL;
    uint8_t b[10];
    putBe32(b, reg); b[4] = 0; b[5] = 0; putBe32(b + 6, val);
    SpiSeg s = { b, nullptr, sizeof(b) };
    int r = spi->transfer(cs, &s, 1);
    if (r) errors++;
    return r;
}

int SpiAmpBus::read(uint32_t reg, uint32_t *val) {
    if (!regOk(reg)) return CS_EINVAL;
    uint8_t h[6], d[4] = { 0, 0, 0, 0 };
    putBe32(h, reg | 0x80000000u); h[4] = 0; h[5] = 0;
    SpiSeg s[2] = { { h, nullptr, sizeof(h) }, { nullptr, d, sizeof(d) } };
    int r = spi->transfer(cs, s, 2);
    if (r) { errors++; *val = 0xffffffff; return r; }
    *val = cs_be32(d);
    return CS_OK;
}

int SpiAmpBus::rawWrite(uint32_t reg, const uint8_t *buf, size_t len) {
    if (!regOk(reg) || (len & 3)) return CS_EINVAL;
    const size_t kChunk = 1024;   // address auto-increments; split for bounded transfers
    for (size_t off = 0; off < len; off += kChunk) {
        size_t n = len - off < kChunk ? len - off : kChunk;
        uint8_t h[6];
        putBe32(h, reg + (uint32_t)off); h[4] = 0; h[5] = 0;
        SpiSeg s[2] = { { h, nullptr, sizeof(h) }, { buf + off, nullptr, n } };
        int r = spi->transfer(cs, s, 2);
        if (r) { errors++; return r; }
    }
    return CS_OK;
}

int SpiAmpBus::rawRead(uint32_t reg, uint8_t *buf, size_t len) {
    if (!regOk(reg) || (len & 3)) return CS_EINVAL;
    const size_t kChunk = 1024;
    for (size_t off = 0; off < len; off += kChunk) {
        size_t n = len - off < kChunk ? len - off : kChunk;
        uint8_t h[6];
        putBe32(h, (reg + (uint32_t)off) | 0x80000000u); h[4] = 0; h[5] = 0;
        SpiSeg s[2] = { { h, nullptr, sizeof(h) }, { nullptr, buf + off, n } };
        int r = spi->transfer(cs, s, 2);
        if (r) { errors++; return r; }
    }
    return CS_OK;
}

// ---------------------------------------------------------------- driver
void CS35L56Amp::parseBootArgs() {
    int v = 0;
    char dummy[8];
    if (PE_parse_boot_argn("cs35l56_vol", &v, sizeof(v))) {
        if (v < 0) v = 0;
        if (v > 40) v = 40;
        volAttenDb = v;
    }
    if (PE_parse_boot_argn("cs35l56_spkid", &v, sizeof(v)) && (v == 0 || v == 1)) spkIdOverride = v;
    dbg = PE_parse_boot_argn("-cs35l56dbg", dummy, sizeof(dummy));
    noPlay = PE_parse_boot_argn("-cs35l56noplay", dummy, sizeof(dummy));
    noReset = PE_parse_boot_argn("-cs35l56noreset", dummy, sizeof(dummy));
    clkFix = PE_parse_boot_argn("-cs35l56clkfix", dummy, sizeof(dummy));
    codecDump = PE_parse_boot_argn("-cs35l56codecdump", dummy, sizeof(dummy));
    follow = PE_parse_boot_argn("-cs35l56follow", dummy, sizeof(dummy));
}

// ---- CS_LOG ring buffer (see Platform.h), one instance for the whole kext ----
extern "C" int vsnprintf(char *, size_t, const char *, __builtin_va_list);
static const uint32_t kCsRingSize = 32768;
static char gCsRing[kCsRingSize];
static volatile SInt32 gCsRingPos;
static uint32_t gCsRingPublished;

void cs_ringlog(const char *fmt, ...) {
    char line[256];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    __builtin_va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    uint32_t start = (uint32_t)OSAddAtomic(n, &gCsRingPos) & 0x7FFFFFFFu;
    for (int i = 0; i < n; i++) gCsRing[(start + (uint32_t)i) % kCsRingSize] = line[i];
    IOLog("%s", line);
}

void CS35L56Amp::publishLog() {
    uint32_t total = (uint32_t)gCsRingPos & 0x7FFFFFFFu;
    if (total == gCsRingPublished) return;
    gCsRingPublished = total;
    uint32_t len = total < kCsRingSize ? total : kCsRingSize;
    char *tmp = (char *)IOMalloc(len);
    if (!tmp) return;
    for (uint32_t i = 0; i < len; i++) tmp[i] = gCsRing[(total - len + i) % kCsRingSize];
    OSData *d = OSData::withBytes(tmp, len);
    IOFree(tmp, len);
    if (d) { setProperty("CS35L56-Log", d); d->release(); }
}

void CS35L56Amp::publish(const char *key, const char *val) { setProperty(key, val); }
void CS35L56Amp::publishNum(const char *key, uint64_t val) { setProperty(key, val, 64); }

bool CS35L56Amp::start(IOService *provider) {
    char dummy[8];
    if (PE_parse_boot_argn("-cs35l56off", dummy, sizeof(dummy))) {
        IOLog("CS35L56: disabled by -cs35l56off\n");
        return false;
    }
    if (!IOService::start(provider)) return false;
    pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci) return false;

    uint32_t ssid = pci->configRead32(kIOPCIConfigSubSystemVendorID);
    CS_LOG("CS35L56Amp %s starting on %s, subsystem %08x", CS35L56_VERSION, provider->getName(), ssid);
    if (ssid != 0x1c631043) {
        // Firmware tuning is laptop-specific (Cirrus: "not interchangeable between laptops").
        CS_LOG("subsystem is not 1043:1C63 (GU605M), refusing to touch the amps");
        IOService::stop(provider);
        return false;
    }
    parseBootArgs();
    CS_LOG("boot-args: vol=-%d dB spkid=%d dbg=%d noplay=%d noreset=%d clkfix=%d codecdump=%d follow=%d",
           volAttenDb, spkIdOverride, dbg, noPlay, noReset, clkFix, codecDump, follow);
    publish("CS35L56-Version", CS35L56_VERSION);
    publish("CS35L56-State", "starting");

    lock = IOLockAlloc();
    call = thread_call_allocate(workerEntry, this);
    if (!lock || !call) {
        if (call) { thread_call_free(call); call = nullptr; }
        if (lock) { IOLockFree(lock); lock = nullptr; }
        IOService::stop(provider);
        return false;
    }

    static IOPMPowerState states[2] = {
        { kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { kIOPMPowerStateVersion1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0 },
    };
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, states, 2);

    registerService();
    // Do the slow work off the start() path (VoodooGPIO/ACPI may still be coming up).
    scheduleWorker(3000);
    return true;
}

void CS35L56Amp::scheduleWorker(uint32_t delayMs) {
    if (stopping || !call) return;
    if (!delayMs) { thread_call_enter(call); return; }
    uint64_t deadline;
    clock_interval_to_deadline(delayMs, kMillisecondScale, &deadline);
    thread_call_enter_delayed(call, deadline);
}

void CS35L56Amp::workerEntry(thread_call_param_t p0, thread_call_param_t) {
    static_cast<CS35L56Amp *>(p0)->worker();
}

void CS35L56Amp::holdReset() {
    if (gpioOk && resetControlled) {
        gpio.setOutput(kPadReset, false);
        CS_LOG("amps held in reset (%s low)", kPadReset.name);
    }
}

void CS35L56Amp::failAll(const char *why) {
    CS_LOG("FAIL-SAFE: %s -> muting amps", why);
    for (int i = 0; i < 2; i++)
        if (amp[i] && amp[i]->isProbed()) amp[i]->failSafe();
    holdReset();
    running = false;
    publish("CS35L56-State", "failed");
    publish("CS35L56-Error", why);
}

bool CS35L56Amp::bringUp() {
    running = false;
    if (!gpioOk) {
        gpioOk = gpio.init();
        if (!gpioOk) { publish("CS35L56-State", "no-gpio"); return false; }
    }
    gpio.logPad(kPadReset);
    gpio.logPad(kPadCs1);
    gpio.logPad(kPadSpkId);
    gpio.logPad(kPadIrq);

    if (!spiOk) {
        spiOk = spi.init(pci, 4000000, &gpio, clkFix);   // DSDT: 0x3D0900 Hz, mode 0
        if (!spiOk) { publish("CS35L56-State", "no-spi"); return false; }
        bus[0].setup(&spi, 0);
        bus[1].setup(&spi, 1);
    } else if (!spi.reinit()) {
        publish("CS35L56-State", "no-spi");
        return false;
    }

    // Speaker id (cirrus_scodec_get_speaker_id: one spk-id GPIO per amp, same pad)
    if (spkIdOverride >= 0) {
        spkId = spkIdOverride;
        CS_LOG("spk-id forced to %d", spkId);
    } else {
        int lvl = 0;
        if (gpio.getInput(kPadSpkId, &lvl) != CS_OK) { failAll("cannot read spk-id GPIO"); return false; }
        spkId = lvl;
        CS_LOG("spk-id (%s) = %d", kPadSpkId.name, spkId);
    }
    publishNum("CS35L56-SpkId", (uint64_t)spkId);

    // Shared hard reset (cs35l56_hda_common_probe: 0, >=1.1 ms, 1)
    bool hardReset = false;
    resetControlled = false;
    if (!noReset) {
        if (gpio.setOutput(kPadReset, false) == CS_OK) {
            resetControlled = true;
            IOSleep(3);
            if (gpio.setOutput(kPadReset, true) == CS_OK) hardReset = true;
        }
        if (!hardReset) CS_LOG("hard reset not possible, relying on soft reset");
    }
    gpio.logPad(kPadReset);

    static const char *names[2] = { "AMP1", "AMP2" };
    for (int i = 0; i < 2; i++) {
        if (!amp[i]) amp[i] = new Cs35l56(&bus[i], i, names[i]);
        if (!amp[i]) { failAll("out of memory"); return false; }
        // Linux: the amp that owns the shared reset waits tIRS; the other one
        // gets "Reset GPIO busy, assume shared reset" and issues a wake event.
        int r = amp[i]->probe(hardReset && i == 0);
        if (r) { char why[64]; snprintf(why, sizeof(why), "%s probe failed (%d)", names[i], r); failAll(why); return false; }
    }

    const CsFwFile *wmfw = csFindFirmware(spkId ? "cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid1.wmfw"
                                                : "cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid0.wmfw");
    for (int i = 0; i < 2; i++) {
        char binName[96];
        snprintf(binName, sizeof(binName), "cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid%d-amp%d.bin", spkId, i + 1);
        const CsFwFile *bin = csFindFirmware(binName);
        CS_LOG("%s: using %s + %s", names[i], wmfw ? wmfw->name : "(none)", bin ? bin->name : "(none)");
        int r = amp[i]->fwLoad(wmfw ? wmfw->data : nullptr, wmfw ? wmfw->size : 0,
                               bin ? bin->data : nullptr, bin ? bin->size : 0);
        if (r) { char why[64]; snprintf(why, sizeof(why), "%s firmware load failed (%d)", names[i], r); failAll(why); return false; }
    }

    // Safe level first, muted while switching to PS0.
    for (int i = 0; i < 2; i++) {
        amp[i]->setMute(true);
        if (amp[i]->setVolumeQdB(-4 * volAttenDb)) { failAll("volume write failed"); return false; }
    }
    if (noPlay) {
        CS_LOG("-cs35l56noplay: amps patched but left paused+muted");
        for (int i = 0; i < 2; i++) amp[i]->logStatus(false);
        publish("CS35L56-State", "ready-noplay");
        return true;
    }
    if (follow) {
        // Linux-like: PLAY on stream start, PAUSE after 3 s idle (followTick()).
        for (int i = 0; i < 2; i++) amp[i]->logStatus(false);
        running = true;
        idleTicks = 0;
        publish("CS35L56-State", "ready-follow");
        publishNum("CS35L56-AttenuationDb", (uint64_t)volAttenDb);
        CS_LOG("both amps patched, follow mode (PLAY with AppleHDA output), amp volume -%d dB", volAttenDb);
        return true;
    }
    for (int i = 0; i < 2; i++) {
        int r = amp[i]->play();
        if (r) CS_LOG("%s: play returned %d", names[i], r);
    }
    for (int i = 0; i < 2; i++) amp[i]->setMute(false);
    for (int i = 0; i < 2; i++) amp[i]->logStatus(false);
    running = true;
    replayAttempts = 0;
    publish("CS35L56-State", "playing");
    publishNum("CS35L56-AttenuationDb", (uint64_t)volAttenDb);
    CS_LOG("both amps patched and in PLAY, amp volume -%d dB", volAttenDb);
    return true;
}

bool CS35L56Amp::engineRunning() {
    // IOAudioEngine publishes kIOAudioEngineStateKey ("IOAudioEngineState"): 1 = running.
    if (!engine) {
        OSDictionary *m = IOService::serviceMatching("AppleHDAEngineOutput");
        if (!m) return false;
        OSIterator *it = IOService::getMatchingServices(m);
        m->release();
        if (!it) return false;
        IOService *s = OSDynamicCast(IOService, it->getNextObject());
        if (s) { s->retain(); engine = s; CS_LOG("follow: watching %s", s->getName()); }
        it->release();
        if (!engine) return false;
    }
    OSNumber *st = OSDynamicCast(OSNumber, engine->getProperty("IOAudioEngineState"));
    return st && st->unsigned32BitValue() == 1;
}

void CS35L56Amp::followTick() {
    bool on = engineRunning();
    bool playingNow = amp[0] && amp[0]->isPlaying();
    if (on) {
        idleTicks = 0;
        if (!playingNow) {
            for (int i = 0; i < 2; i++) amp[i]->play();
            for (int i = 0; i < 2; i++) amp[i]->setMute(false);
            if (dbg) CS_LOG("follow: output engine running -> PLAY");
        }
    } else if (playingNow && ++idleTicks >= 12) {   // 12 x 250 ms = 3 s (Linux autosuspend delay)
        for (int i = 0; i < 2; i++) { amp[i]->setMute(true); amp[i]->pause(); }
        if (dbg) CS_LOG("follow: output idle 3 s -> PAUSE");
    }
}

void CS35L56Amp::worker() {
    if (stopping) return;
    IOLockLock(lock);
    if (!asleep && needBringUp) {
        needBringUp = false;
        if (bringUpAttempts++ < 2) {
            uint64_t t0 = cs_now_us();
            bool ok = bringUp();
            CS_LOG("bring-up %s in %llu ms (attempt %d)", ok ? "OK" : "FAILED",
                   (unsigned long long)((cs_now_us() - t0) / 1000), bringUpAttempts);
        } else {
            CS_LOG("bring-up attempts exhausted, staying off");
        }
        if (codecDump && !hdaOk) {
            hdaOk = hda.init();
            if (hdaOk) { hda.dumpAll(); hda.dumpLive(); }
        }
    } else if (!asleep && running && follow) {
        followTick();
        if ((++ticks % (dbg ? 8 : 40)) == 0)
            for (int i = 0; i < 2; i++) if (amp[i]) amp[i]->logStatus(!dbg);
        if (bus[0].errorCount() + bus[1].errorCount() > 50) failAll("too many SPI errors");
    } else if (!asleep && running) {
        ticks++;
        bool verbose = dbg || ticks <= 6;
        for (int i = 0; i < 2; i++) {
            if (!amp[i]) continue;
            amp[i]->logStatus(!verbose);
            // Keep the amps in PS0: if firmware fell back to standby, re-issue PLAY (bounded).
            uint32_t ps = 0;
            if (bus[i].read(CS35L56_TRANSDUCER_ACTUAL_PS, &ps) == CS_OK && ps != CS35L56_PS0 && replayAttempts < 5) {
                replayAttempts++;
                CS_LOG("%s: PS=%u, re-issuing PLAY (%d/5)", amp[i]->name(), ps, replayAttempts);
                amp[i]->play();
            }
        }
        if (bus[0].errorCount() + bus[1].errorCount() > 50) failAll("too many SPI errors");
    }
    static int liveDumps = 0;
    if (!asleep && hdaOk && !needBringUp && (!follow || (ticks % 8) == 0) && (dbg || liveDumps < 30)) { liveDumps++; hda.dumpLive(); }
    publishLog();
    IOLockUnlock(lock);
    if (!stopping) scheduleWorker(follow && running ? 250 : (dbg ? 2000 : 10000));
}

IOReturn CS35L56Amp::setPowerState(unsigned long state, IOService *) {
    if (!lock) return kIOPMAckImplied;
    if (state == 0) {
        IOLockLock(lock);
        CS_LOG("sleep: muting and holding amps in reset");
        for (int i = 0; i < 2; i++)
            if (amp[i] && amp[i]->isProbed()) amp[i]->failSafe();
        holdReset();   // cs35l56_hda_system_suspend_late(): reset low
        running = false;
        asleep = true;
        IOLockUnlock(lock);
    } else if (asleep) {
        IOLockLock(lock);
        CS_LOG("wake: scheduling re-init");
        asleep = false;
        needBringUp = true;
        bringUpAttempts = 0;
        IOLockUnlock(lock);
        scheduleWorker(1500);
    }
    return kIOPMAckImplied;
}

void CS35L56Amp::stop(IOService *provider) {
    stopping = true;
    if (call) {
        thread_call_cancel_wait(call);
        thread_call_free(call);
        call = nullptr;
    }
    if (lock) {
        IOLockLock(lock);
        for (int i = 0; i < 2; i++)
            if (amp[i] && amp[i]->isProbed()) amp[i]->failSafe();
        holdReset();
        IOLockUnlock(lock);
    }
    for (int i = 0; i < 2; i++) { delete amp[i]; amp[i] = nullptr; }
    if (engine) { engine->release(); engine = nullptr; }
    hda.release();
    spi.release();
    gpio.release();
    PMstop();
    if (lock) { IOLockFree(lock); lock = nullptr; }
    IOService::stop(provider);
}
