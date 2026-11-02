// CS35L56 amp bring-up, ported from Linux:
//   sound/soc/codecs/cs35l56-shared.c      (cs35l56_hw_init, system reset, mbox, patch)
//   sound/hda/codecs/side-codecs/cs35l56_hda.c (common_probe, fw_load, play/pause)
// Only the SPI + HDA path is implemented.
#pragma once
#include "CsBus.h"
#include "CsDsp.h"

struct Cs35l56FwReg {        // struct cs35l56_fw_reg
    uint32_t fw_ver, halo_state, pm_cur_stat, prot_sts, transducer_actual_ps,
             user_mute, user_volume, posture_number;
};

class Cs35l56 {
public:
    Cs35l56(CsRegBus *bus, int index, const char *name);

    // cs35l56_hda_common_probe() minus GPIO handling. The caller performs
    // the (shared) hardware reset before calling this on the first amp.
    // hardResetDone: true for an amp whose reset line was just released.
    int probe(bool hardResetDone);

    // cs35l56_hda_fw_load(). wmfw/bin may be null only if the device is
    // already patched (FIRMWARE_MISSING clear); otherwise both are required.
    int fwLoad(const uint8_t *wmfw, size_t wmfwLen, const uint8_t *bin, size_t binLen);

    // cs35l56_hda_play() / cs35l56_hda_pause()
    int play();
    int pause();

    // volume in 0.25 dB steps (Linux control raw units), clamped to [-400, 0].
    int setVolumeQdB(int qdb);
    int setMute(bool mute);

    // Best-effort: mute, disable ASP RX, pause. Never fails loudly.
    void failSafe();

    // Logs PS / HALO / PROT / EINT state; returns true if state changed.
    bool logStatus(bool onlyIfChanged);

    bool isProbed() const { return probed; }
    bool isPatched() const { return fw_patched; }
    bool isPlaying() const { return playing; }
    uint8_t rev() const { return revid; }
    bool secured() const { return is_secured; }
    const char *name() const { return amp_name; }

    // Exposed for the host test.
    int mboxSend(uint32_t cmd);

private:
    int pollRead(uint32_t reg, uint32_t *val, bool (*cond)(uint32_t), uint32_t poll_us, uint32_t timeout_us);
    int waitForFirmwareBoot();
    void issueWakeEvent();
    int hwInit(bool hardResetDone);
    void systemReset();
    int setPatch();
    int writeHdaDaiConfig();
    int writeIrqMasks();
    int firmwareShutdown();
    int readProtStatus(bool *missing, uint32_t *ver);
    int updateBits(uint32_t reg, uint32_t mask, uint32_t val);

    CsRegBus *bus;
    int index;
    const char *amp_name;
    Cs35l56FwReg fw_reg;
    uint8_t revid;
    uint8_t type;
    bool is_secured;
    bool probed;
    bool fw_patched;
    bool playing;
    uint32_t last_status[8];
};
