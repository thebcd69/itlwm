// See Cs35l56.h. Function-by-function port; comments name the Linux
// function each block comes from (torvalds/linux master, 2026-11-02).
#include "Cs35l56.h"
#include "Cs35l56Regs.h"

static const Cs35l56FwReg kFwRegB0 = {   // cs35l56_fw_reg
    CS35L56_DSP1_FW_VER, CS35L56_DSP1_HALO_STATE, CS35L56_DSP1_PM_CUR_STATE,
    CS35L56_PROTECTION_STATUS, CS35L56_TRANSDUCER_ACTUAL_PS,
    CS35L56_MAIN_RENDER_USER_MUTE, CS35L56_MAIN_RENDER_USER_VOLUME, CS35L56_MAIN_POSTURE_NUMBER,
};
static const Cs35l56FwReg kFwRegB2 = {   // cs35l56_b2_fw_reg
    CS35L56_DSP1_FW_VER, CS35L56_B2_DSP1_HALO_STATE, CS35L56_B2_DSP1_PM_CUR_STATE,
    CS35L56_PROTECTION_STATUS, CS35L56_TRANSDUCER_ACTUAL_PS,
    CS35L56_MAIN_RENDER_USER_MUTE, CS35L56_MAIN_RENDER_USER_VOLUME, CS35L56_MAIN_POSTURE_NUMBER,
};

struct RegSeq { uint32_t reg, val; };

// cs35l56_patch[] + cs35l56_patch_fw[] (cs35l56-shared.c), applied by cs35l56_set_patch()
static const RegSeq kPatch[] = {
    { CS35L56_SWIRE_DP3_CH1_INPUT, 0x00000018 },
    { CS35L56_SWIRE_DP3_CH2_INPUT, 0x00000019 },
    { CS35L56_SWIRE_DP3_CH3_INPUT, 0x00000029 },
    { CS35L56_SWIRE_DP3_CH4_INPUT, 0x00000028 },
    { CS35L56_IRQ1_MASK_18,        0x1f7df0ff },
    { CS35L56_MAIN_RENDER_USER_MUTE,   0x00000000 },
    { CS35L56_MAIN_RENDER_USER_VOLUME, 0x00000000 },
    { CS35L56_MAIN_POSTURE_NUMBER,     0x00000000 },
};

// cs35l56_hda_dai_config[] (cs35l56_hda.c): I2S, 32-bit slots, 24-bit data,
// BCLK id 0x21, RX1..3/TX1..4 disabled, TX sources fixed.
static const RegSeq kHdaDaiConfig[] = {
    { CS35L56_ASP1_CONTROL1,       0x00000021 },
    { CS35L56_ASP1_CONTROL2,       0x20200200 },
    { CS35L56_ASP1_CONTROL3,       0x00000003 },
    { CS35L56_ASP1_FRAME_CONTROL1, 0x03020100 },
    { CS35L56_ASP1_FRAME_CONTROL5, 0x00020100 },
    { CS35L56_ASP1_DATA_CONTROL5,  0x00000018 },
    { CS35L56_ASP1_DATA_CONTROL1,  0x00000018 },
    { CS35L56_ASP1_ENABLES1,       0x00000000 },
    { CS35L56_ASP1TX1_INPUT,       0x00000018 },
    { CS35L56_ASP1TX2_INPUT,       0x00000019 },
    { CS35L56_ASP1TX3_INPUT,       0x00000020 },
    { CS35L56_ASP1TX4_INPUT,       0x00000028 },
};

Cs35l56::Cs35l56(CsRegBus *b, int idx, const char *n)
    : bus(b), index(idx), amp_name(n), fw_reg(kFwRegB0), revid(0), type(0),
      is_secured(false), probed(false), fw_patched(false), playing(false) {
    for (int i = 0; i < 8; i++) last_status[i] = 0xdeadbeef;
}

int Cs35l56::pollRead(uint32_t reg, uint32_t *val, bool (*cond)(uint32_t), uint32_t poll_us, uint32_t timeout_us) {
    // read_poll_timeout() semantics: read, test, sleep; always bounded.
    uint64_t start = cs_now_us();
    int ret = CS_OK;
    for (int iter = 0; iter < 100000; iter++) {
        ret = bus->read(reg, val);
        if (ret == CS_OK && cond(*val)) return CS_OK;
        if (cs_now_us() - start >= timeout_us) break;
        if (poll_us >= 1000) cs_sleep_ms(poll_us / 1000); else cs_delay_us(poll_us);
    }
    // One last read after the timeout, as read_poll_timeout does.
    ret = bus->read(reg, val);
    if (ret == CS_OK && cond(*val)) return CS_OK;
    return CS_ETIMEDOUT;
}

int Cs35l56::updateBits(uint32_t reg, uint32_t mask, uint32_t val) {
    uint32_t cur = 0;
    int ret = bus->read(reg, &cur);
    if (ret) return ret;
    uint32_t nv = (cur & ~mask) | (val & mask);
    if (nv == cur) return CS_OK;
    return bus->write(reg, nv);
}

static bool condZero(uint32_t v) { return v == 0; }
static bool condBooted(uint32_t v) { return (v < 0xFFFF) && (v >= CS35L56_HALO_STATE_BOOT_DONE); }
static bool condMbox2(uint32_t v) { return (v > 0) && (v < 0xffffffff); }
static bool condShutdown(uint32_t v) { return v == CS35L56_HALO_STATE_SHUTDOWN; }
static bool condPS0(uint32_t v) { return v == CS35L56_PS0; }

// cs35l56_mbox_send()
int Cs35l56::mboxSend(uint32_t cmd) {
    uint32_t val = 0;
    int ret = bus->write(CS35L56_DSP_VIRTUAL1_MBOX_1, cmd);
    if (ret) { CS_LOG("%s: MBOX write %#x failed: %d", amp_name, cmd, ret); return ret; }
    ret = pollRead(CS35L56_DSP_VIRTUAL1_MBOX_1, &val, condZero, CS35L56_MBOX_POLL_US, CS35L56_MBOX_TIMEOUT_US);
    if (ret) CS_LOG("%s: MBOX command %#x failed: %d (MBOX1=%#x)", amp_name, cmd, ret, val);
    return ret;
}

// cs35l56_wait_for_firmware_boot()
int Cs35l56::waitForFirmwareBoot() {
    uint32_t val = 0;
    int ret = pollRead(fw_reg.halo_state, &val, condBooted, CS35L56_HALO_STATE_POLL_US, CS35L56_HALO_STATE_TIMEOUT_US);
    if (ret) { CS_LOG("%s: firmware boot timed out: HALO_STATE=%#x", amp_name, val); return CS_EIO; }
    return CS_OK;
}

// cs35l56_issue_wake_event()
void Cs35l56::issueWakeEvent() {
    uint32_t val;
    bus->read(CS35L56_IRQ1_STATUS, &val);
    cs_delay_us(CS35L56_WAKE_HOLD_TIME_US);
    bus->read(CS35L56_IRQ1_STATUS, &val);
    cs_sleep_ms((CS35L56_CONTROL_PORT_READY_US + 999) / 1000 + 1);
}

// cs35l56_spi_system_reset() (the SPI variant of cs35l56_system_reset())
void Cs35l56::systemReset() {
    // cs35l56_spi_system_reset_stages[]: SYSTEM_RESET to VIRTUAL1_MBOX_1,
    // then PING to DSP_MBOX_1_RAW to delimit the soft reset; no other bus
    // traffic in between (caller guarantees this: single worker thread).
    bus->write(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_SYSTEM_RESET);
    cs_sleep_ms((CS35L56_SPI_RESET_TO_PORT_READY_US + 999) / 1000 + 1);
    bus->write(CS35L56_DSP_MBOX_1_RAW, CS35L56_MBOX_CMD_PING);
    cs_sleep_ms((CS35L56_SPI_RESET_TO_PORT_READY_US + 999) / 1000 + 1);

    uint32_t val = 0;
    if (pollRead(CS35L56_DSP_VIRTUAL1_MBOX_2, &val, condMbox2, CS35L56_HALO_STATE_POLL_US, CS35L56_HALO_STATE_TIMEOUT_US))
        CS_LOG("%s: SPI reboot timed out: MBOX2=%#x", amp_name, val);
}

int Cs35l56::readProtStatus(bool *missing, uint32_t *ver) {
    uint32_t ps = 0;
    int ret = bus->read(fw_reg.prot_sts, &ps);
    if (ret) { CS_LOG("%s: get PROTECTION_STATUS failed: %d", amp_name, ret); return ret; }
    *missing = (ps & CS35L56_FIRMWARE_MISSING) != 0;
    ret = bus->read(fw_reg.fw_ver, ver);
    if (ret) CS_LOG("%s: get FW VER failed: %d", amp_name, ret);
    return ret;
}

int Cs35l56::writeIrqMasks() {
    // From cs35l56_hw_init(): mask wake/blocked IRQs, unmask AMP_SHORT_ERR and TEMP_ERR.
    int ret = bus->write(CS35L56_IRQ1_MASK_20, 0xffffffff);
    if (!ret) ret = updateBits(CS35L56_IRQ1_MASK_1, CS35L56_AMP_SHORT_ERR_EINT1_MASK, 0);
    if (!ret) ret = updateBits(CS35L56_IRQ1_MASK_8, CS35L56_TEMP_ERR_EINT1_MASK, 0);
    return ret;
}

// cs35l56_hw_init()
int Cs35l56::hwInit(bool hardResetDone) {
    if (!hardResetDone)
        issueWakeEvent();
    else
        cs_sleep_ms((CS35L56_CONTROL_PORT_READY_US + 999) / 1000 + 1);   // cs35l56_wait_control_port_ready()

    uint32_t rv = 0, devid = 0, otpid = 0, sec = 0, fwver = 0;
    int ret = bus->read(CS35L56_REVID, &rv);
    if (ret) { CS_LOG("%s: get Revision ID failed: %d", amp_name, ret); return ret; }
    revid = (uint8_t)(rv & (CS35L56_AREVID_MASK | CS35L56_MTLREVID_MASK));
    fw_reg = (revid == 0xb0) ? kFwRegB0 : kFwRegB2;   // cs35l56_set_fw_reg_table()
    CS_LOG("%s: REVID raw %#x -> rev %02X", amp_name, rv, revid);

    ret = waitForFirmwareBoot();
    if (ret) return ret;

    ret = bus->read(CS35L56_DEVID, &devid);
    if (ret) { CS_LOG("%s: get Device ID failed", amp_name); return ret; }
    devid &= CS35L56_DEVID_MASK;
    if (devid != 0x35A56) {
        // The HDA driver accepts 35A54/35A56/35A57; this board is CSC3556.
        CS_LOG("%s: unexpected device %#x (want 0x35A56), refusing", amp_name, devid);
        return CS_ENODEV;
    }
    type = 0x56;

    ret = bus->read(CS35L56_DSP_RESTRICT_STS1, &sec);
    if (ret) { CS_LOG("%s: get Secure status failed", amp_name); return ret; }
    is_secured = (sec & CS35L56_RESTRICTED_MASK) != 0;

    ret = bus->read(CS35L56_OTPID, &otpid);
    if (ret) { CS_LOG("%s: get OTP ID failed", amp_name); return ret; }

    bool missing = true;
    ret = readProtStatus(&missing, &fwver);
    if (ret) return ret;

    CS_LOG("%s: Cirrus Logic CS35L%02X%s Rev %02X OTP%u fw:%u.%u.%u (patched=%u)", amp_name, type,
           is_secured ? "s" : "", revid, otpid, fwver >> 16, (fwver >> 8) & 0xff, fwver & 0xff, !missing);

    ret = writeIrqMasks();
    if (ret) return ret;

    // cs35l56_read_silicon_uid(): only needed for EFI calibration lookup.
    // struct cs35l56_pte: x, wafer_id, pte[2], lot[3], y, unused[3], dvs (12 bytes)
    uint8_t pte[12];
    if (bus->rawRead(CS35L56_OTP_MEM_53, pte, sizeof(pte)) == CS_OK) {
        uint64_t uid = (uint64_t)((uint32_t)pte[6] | ((uint32_t)pte[5] << 8) | ((uint32_t)pte[4] << 16)) << 32;
        uid |= (uint32_t)pte[0] | ((uint32_t)pte[7] << 8) | ((uint32_t)pte[1] << 16) | ((uint32_t)pte[11] << 24);
        CS_LOG("%s: SiliconID = %#llx", amp_name, (unsigned long long)uid);
    }
    return CS_OK;
}

int Cs35l56::setPatch() {
    for (size_t i = 0; i < sizeof(kPatch) / sizeof(kPatch[0]); i++) {
        int ret = bus->write(kPatch[i].reg, kPatch[i].val);
        if (ret) return ret;
    }
    return CS_OK;
}

int Cs35l56::writeHdaDaiConfig() {
    for (size_t i = 0; i < sizeof(kHdaDaiConfig) / sizeof(kHdaDaiConfig[0]); i++) {
        int ret = bus->write(kHdaDaiConfig[i].reg, kHdaDaiConfig[i].val);
        if (ret) return ret;
    }
    return CS_OK;
}

// cs35l56_hda_common_probe() from cs35l56_hw_init() onwards.
int Cs35l56::probe(bool hardResetDone) {
    probed = false;
    int ret = hwInit(hardResetDone);
    if (ret) return ret;

    systemReset();
    ret = waitForFirmwareBoot();
    if (ret) return ret;

    ret = setPatch();
    if (ret) return ret;
    // regcache_mark_dirty() + regcache_sync(): re-apply what hw_init wrote.
    ret = writeIrqMasks();
    if (ret) return ret;

    ret = mboxSend(CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE);
    if (ret) return ret;

    // cs35l56_get_calibration(): EFI calibration not implemented (optional;
    // firmware then runs with its default calibration).

    ret = writeHdaDaiConfig();
    if (ret) return ret;

    CS_LOG("%s: probe done (index %d, rev %02X, secured=%d)", amp_name, index, revid, is_secured);
    probed = true;
    return CS_OK;
}

// cs35l56_firmware_shutdown()
int Cs35l56::firmwareShutdown() {
    int ret = mboxSend(CS35L56_MBOX_CMD_SHUTDOWN);
    if (ret) return ret;
    uint32_t val = 0;
    ret = pollRead(fw_reg.pm_cur_stat, &val, condShutdown, CS35L56_HALO_STATE_POLL_US, CS35L56_HALO_STATE_TIMEOUT_US);
    if (ret) CS_LOG("%s: failed to poll PM_CUR_STATE to 1 is %u (ret %d)", amp_name, val, ret);
    return ret;
}

// cs35l56_hda_fw_load()
int Cs35l56::fwLoad(const uint8_t *wmfw, size_t wmfwLen, const uint8_t *bin, size_t binLen) {
    fw_patched = false;
    if (!probed) return CS_EIO;

    bool missing = true;
    uint32_t preloaded = 0;
    int ret = readProtStatus(&missing, &preloaded);
    if (ret) return ret;
    if (missing) preloaded = 0;

    if (!missing) {
        // BIOS already patched the DSP. Linux would look for version-specific
        // tuning files ("...-%06x-dsp1-misc-...") which linux-firmware does not
        // ship for 10431c63, so it only reinits. Same here.
        CS_LOG("%s: firmware already patched by BIOS (ver %#x), skipping download", amp_name, preloaded);
        wmfw = nullptr; bin = nullptr;
    } else {
        if (!wmfw || !wmfwLen) { CS_LOG("%s: .wmfw file required but not found", amp_name); return CS_ENODEV; }
        if (!bin || !binLen) { CS_LOG("%s: .bin file required but not found", amp_name); return CS_ENODEV; }
        if (is_secured) { CS_LOG("%s: secured part with FIRMWARE_MISSING: cannot download", amp_name); return CS_EACCES; }
        if (revid != 0xb0) {
            // linux-firmware only has cs35l56-b0-* files for this SSID.
            CS_LOG("%s: rev %02X but only B0 firmware is embedded, refusing", amp_name, revid);
            return CS_ENODEV;
        }
    }

    if (missing) {
        ret = firmwareShutdown();
        if (ret) return ret;

        CsDsp *dsp = new CsDsp(bus, amp_name);
        if (!dsp) return CS_ENOMEM;
        uint64_t t0 = cs_now_us();
        ret = dsp->loadWmfw(wmfw, wmfwLen);              // cs_dsp_power_up(): cs_dsp_load
        if (!ret) ret = dsp->setupAlgs();                 //   ops->setup_algs
        if (!ret) ret = dsp->loadCoeff(bin, binLen);      //   cs_dsp_load_coeff
        CS_LOG("%s: download %s, %u bytes in %llu ms", amp_name, ret ? "FAILED" : "ok",
               dsp->bytesWritten(), (unsigned long long)((cs_now_us() - t0) / 1000));
        delete dsp;
        if (ret) return ret;   // DSP stays in SHUTDOWN: silent.

        // "If we downloaded firmware, reset the device and wait for it to boot"
        systemReset();
        ret = waitForFirmwareBoot();
        if (ret) return ret;
    }

    ret = mboxSend(CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE);
    if (ret) return ret;

    // regcache_sync(): registers that differ from defaults / patches.
    ret = setPatch();
    if (!ret) ret = writeIrqMasks();
    if (!ret) ret = writeHdaDaiConfig();
    if (ret) return ret;

    ret = updateBits(fw_reg.prot_sts, CS35L56_FIRMWARE_MISSING, 0);
    if (ret) return ret;
    fw_patched = true;

    // cs_dsp_run(): no-op for cs_dsp_halo_ao_ops without controls.
    // cs35l56_hda_apply_calibration(): not implemented (see FEASIBILITY.md).

    ret = mboxSend(CS35L56_MBOX_CMD_AUDIO_REINIT);
    if (ret) { fw_patched = false; return ret; }

    uint32_t v = 0;
    bool m2 = true;
    if (readProtStatus(&m2, &v) == CS_OK)
        CS_LOG("%s: running fw %u.%u.%u, FIRMWARE_MISSING=%d", amp_name, v >> 16, (v >> 8) & 0xff, v & 0xff, m2);
    return CS_OK;
}

// cs35l56_hda_play()
int Cs35l56::play() {
    if (!fw_patched) return CS_EIO;
    int ret = mboxSend(CS35L56_MBOX_CMD_AUDIO_PLAY);
    if (ret == CS_OK) {
        uint32_t val = 0;
        ret = pollRead(fw_reg.transducer_actual_ps, &val, condPS0, CS35L56_PS0_POLL_US, CS35L56_PS0_TIMEOUT_US);
        if (ret) CS_LOG("%s: PS0 wait failed: %d (PS=%u)", amp_name, ret, val);
    }
    // Linux sets RX1|RX2|asp_tx_mask. We deliberately leave the ASP TX
    // (amp -> codec feedback) disabled: nothing on macOS consumes it.
    int r2 = updateBits(CS35L56_ASP1_ENABLES1,
                        (1u << CS35L56_ASP_RX1_EN_SHIFT) | (1u << CS35L56_ASP_RX2_EN_SHIFT),
                        (1u << CS35L56_ASP_RX1_EN_SHIFT) | (1u << CS35L56_ASP_RX2_EN_SHIFT));
    playing = (r2 == CS_OK);
    return ret ? ret : r2;
}

// cs35l56_hda_pause()
int Cs35l56::pause() {
    playing = false;
    int ret = mboxSend(CS35L56_MBOX_CMD_AUDIO_PAUSE);
    int r2 = updateBits(CS35L56_ASP1_ENABLES1, 0x0003000F, 0);
    return ret ? ret : r2;
}

int Cs35l56::setVolumeQdB(int qdb) {
    if (qdb > 0) qdb = 0;   // never above 0 dB (Linux allows +12 dB)
    if (qdb < CS35L56_MAIN_RENDER_USER_VOLUME_MIN) qdb = CS35L56_MAIN_RENDER_USER_VOLUME_MIN;
    uint32_t raw = ((uint32_t)qdb << CS35L56_MAIN_RENDER_USER_VOLUME_SHIFT) & CS35L56_MAIN_RENDER_USER_VOLUME_MASK;
    return updateBits(fw_reg.user_volume, CS35L56_MAIN_RENDER_USER_VOLUME_MASK, raw);
}

int Cs35l56::setMute(bool mute) {
    return updateBits(fw_reg.user_mute, CS35L56_MAIN_RENDER_USER_MUTE_MASK, mute ? 1 : 0);
}

void Cs35l56::failSafe() {
    // Order: digital mute first, then stop the ASP, then tell fw to pause.
    setMute(true);
    updateBits(CS35L56_ASP1_ENABLES1, 0x0003000F, 0);
    if (fw_patched) mboxSend(CS35L56_MBOX_CMD_AUDIO_PAUSE);
    playing = false;
}

bool Cs35l56::logStatus(bool onlyIfChanged) {
    static const uint32_t regs[8] = {
        CS35L56_TRANSDUCER_ACTUAL_PS, CS35L56_PROTECTION_STATUS, 0 /* halo */, CS35L56_IRQ1_EINT_1,
        CS35L56_IRQ1_EINT_2, CS35L56_IRQ1_EINT_4, CS35L56_IRQ1_EINT_8, CS35L56_IRQ1_EINT_18,
    };
    uint32_t v[8];
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        uint32_t r = regs[i] ? regs[i] : fw_reg.halo_state;
        if (bus->read(r, &v[i])) v[i] = 0xffffffff;
        if (v[i] != last_status[i]) changed = true;
        last_status[i] = v[i];
    }
    if (changed || !onlyIfChanged)
        CS_LOG("%s: status PS=%u PROT=%#x HALO=%#x EINT1=%#x EINT2=%#x EINT4=%#x EINT8=%#x EINT18=%#x",
               amp_name, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    return changed;
}
