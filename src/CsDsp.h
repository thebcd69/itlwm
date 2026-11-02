// Minimal HALO-core firmware loader for CS35L56: a faithful subset of
// Linux drivers/firmware/cirrus/cs_dsp.c for the "no_core_startstop"
// (cs_dsp_halo_ao_ops) configuration used by cs35l56 (cs35l56_init_cs_dsp()).
//
// Implemented: cs_dsp_load() (wmfw), cs_dsp_halo_setup_algs(),
// cs_dsp_load_coeff() (bin). Not implemented (not needed for the ao ops
// variant without user controls): coefficient controls, MPU lock,
// core start/stop, watchdog.
#pragma once
#include "CsBus.h"

struct CsDspMemRegion {
    int type;
    uint32_t base;
};

struct CsDspAlgRegion {
    int type;
    uint32_t alg;
    uint32_t ver;
    uint32_t base;   // in DSP words
};

class CsDsp {
public:
    explicit CsDsp(CsRegBus *bus, const char *tag);
    ~CsDsp();

    int loadWmfw(const uint8_t *data, size_t size);
    int setupAlgs();
    int loadCoeff(const uint8_t *data, size_t size);

    uint32_t fwId() const { return fw_id; }
    uint32_t fwVer() const { return fw_id_ver; }
    int numAlgRegions() const { return n_alg_regions; }
    const CsDspAlgRegion *algRegion(int i) const { return (i >= 0 && i < n_alg_regions) ? &alg_regions[i] : nullptr; }
    uint32_t bytesWritten() const { return bytes_written; }

    // Public for host tests.
    static uint32_t regionToReg(const CsDspMemRegion *mem, uint32_t offset);
    static const CsDspMemRegion *findRegion(int type);

private:
    const CsDspAlgRegion *findAlgRegion(int type, uint32_t id) const;
    int addAlgRegions(uint32_t id, uint32_t ver, uint32_t xm_base, uint32_t ym_base);

    CsRegBus *bus;
    const char *tag;
    uint32_t fw_id;
    uint32_t fw_id_ver;
    uint32_t bytes_written;
    enum { kMaxAlgRegions = 4 * 80 };
    CsDspAlgRegion alg_regions[kMaxAlgRegions];
    int n_alg_regions;
};
