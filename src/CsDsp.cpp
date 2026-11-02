// See CsDsp.h. Line references are to torvalds/linux master
// drivers/firmware/cirrus/cs_dsp.c as fetched on 2026-11-02.
#include "CsDsp.h"
#include "Cs35l56Regs.h"

// cs35l56_dsp1_regions[] (sound/soc/codecs/cs35l56-shared.c)
static const CsDspMemRegion kRegions[] = {
    { WMFW_HALO_PM_PACKED, CS35L56_DSP1_PMEM_0 },
    { WMFW_HALO_XM_PACKED, CS35L56_DSP1_XMEM_PACKED_0 },
    { WMFW_HALO_YM_PACKED, CS35L56_DSP1_YMEM_PACKED_0 },
    { WMFW_ADSP2_XM,       CS35L56_DSP1_XMEM_UNPACKED24_0 },
    { WMFW_ADSP2_YM,       CS35L56_DSP1_YMEM_UNPACKED24_0 },
};

CsDsp::CsDsp(CsRegBus *b, const char *t)
    : bus(b), tag(t), fw_id(0), fw_id_ver(0), bytes_written(0), n_alg_regions(0) {}

CsDsp::~CsDsp() {}

const CsDspMemRegion *CsDsp::findRegion(int type) {
    for (size_t i = 0; i < sizeof(kRegions) / sizeof(kRegions[0]); i++)
        if (kRegions[i].type == type)
            return &kRegions[i];
    return nullptr;
}

// cs_dsp_halo_region_to_reg()
uint32_t CsDsp::regionToReg(const CsDspMemRegion *mem, uint32_t offset) {
    switch (mem->type) {
    case WMFW_ADSP2_XM:
    case WMFW_ADSP2_YM:
        return mem->base + (offset * 4);
    case WMFW_HALO_XM_PACKED:
    case WMFW_HALO_YM_PACKED:
        return (mem->base + (offset * 3)) & ~0x3u;
    case WMFW_HALO_PM_PACKED:
        return mem->base + (offset * 5);
    default:
        return 0;
    }
}

static bool regInDspRange(uint32_t reg, size_t len) {
    // Every DSP write must land inside the cs35l56 regmap
    // (max_register = CS35L56_DSP1_PMEM_5114). Defensive: refuse anything else.
    if (reg < CS35L56_DSP1_XMEM_PACKED_0) return false;
    uint64_t end = (uint64_t)reg + len;
    return end <= (uint64_t)CS35L56_DSP1_PMEM_5114 + 4;
}

// cs_dsp_load(): wmfw
int CsDsp::loadWmfw(const uint8_t *d, size_t size) {
    const size_t kHdr = 12, kSizes = 16, kFooter = 12, kRegHdr = 8;
    if (!d || size <= kHdr) return CS_EOVERFLOW;
    if (d[0] != 'W' || d[1] != 'M' || d[2] != 'F' || d[3] != 'W') {
        CS_LOG("%s: wmfw: invalid magic", tag);
        return CS_EINVAL;
    }
    uint32_t hdr_len = cs_le32(d + 4);
    uint8_t core = d[10], ver = d[11];
    // cs_dsp_halo_validate_version(): only version 3
    if (ver != 3) { CS_LOG("%s: wmfw: unknown file format %u", tag, ver); return CS_EINVAL; }
    if (core != WMFW_HALO) { CS_LOG("%s: wmfw: invalid core %u != %u", tag, core, WMFW_HALO); return CS_EINVAL; }

    size_t pos = kHdr;
    if (kSizes > size - pos) return CS_EOVERFLOW;   // cs_dsp_adsp2_parse_sizes()
    CS_LOG("%s: wmfw: %u XM, %u YM, %u PM, %u ZM", tag, cs_le32(d + pos), cs_le32(d + pos + 4),
           cs_le32(d + pos + 8), cs_le32(d + pos + 12));
    pos += kSizes;
    if (kFooter > size - pos) return CS_EOVERFLOW;
    pos += kFooter;
    if (hdr_len != pos) return CS_EOVERFLOW;

    int regions = 0;
    while (pos < size) {
        if (kRegHdr > size - pos) return CS_EOVERFLOW;
        const uint8_t *r = d + pos;
        uint32_t rlen = cs_le32(r + 4);
        if (rlen > size - pos - kRegHdr) return CS_EOVERFLOW;
        uint32_t offset = cs_le32(r) & 0xffffff;
        int type = cs_be32(r) & 0xff;
        uint32_t reg = 0;

        switch (type) {
        case WMFW_INFO_TEXT:
        case WMFW_NAME_TEXT: {
            char txt[101];
            uint32_t n = rlen < 100 ? rlen : 100;
            for (uint32_t i = 0; i < n; i++) { char c = (char)r[kRegHdr + i]; txt[i] = (c >= 0x20 && c < 0x7f) ? c : ' '; }
            txt[n] = 0;
            CS_LOG("%s: wmfw: %s", tag, txt);
            break;
        }
        case WMFW_ALGORITHM_DATA:
            // Coefficient descriptors only (cs_dsp_parse_coeff); they create
            // ALSA controls in Linux and write nothing to the device.
            break;
        case WMFW_ABSOLUTE:
            reg = offset;
            break;
        case WMFW_ADSP2_XM:
        case WMFW_ADSP2_YM:
        case WMFW_HALO_PM_PACKED:
        case WMFW_HALO_XM_PACKED:
        case WMFW_HALO_YM_PACKED: {
            const CsDspMemRegion *mem = findRegion(type);
            if (!mem) { CS_LOG("%s: wmfw: no region of type %x", tag, type); return CS_EINVAL; }
            reg = regionToReg(mem, offset);
            break;
        }
        default:
            CS_LOG("%s: wmfw.%d: unknown region type %x at %u", tag, regions, type, (unsigned)pos);
            break;
        }

        if (reg) {
            if ((rlen & 3) || !regInDspRange(reg, rlen)) {
                CS_LOG("%s: wmfw.%d: refusing write of %u bytes at %#x", tag, regions, rlen, reg);
                return CS_EINVAL;
            }
            int ret = bus->rawWrite(reg, r + kRegHdr, rlen);
            if (ret) {
                CS_LOG("%s: wmfw.%d: failed to write %u bytes at %#x: %d", tag, regions, rlen, reg, ret);
                return ret;
            }
            bytes_written += rlen;
        }
        pos += rlen + kRegHdr;
        regions++;
    }
    CS_LOG("%s: wmfw: %d regions, %u bytes downloaded", tag, regions, bytes_written);
    return CS_OK;
}

int CsDsp::addAlgRegions(uint32_t id, uint32_t ver, uint32_t xm_base, uint32_t ym_base) {
    // cs_dsp_halo_create_regions()
    static const int types[] = { WMFW_ADSP2_XM, WMFW_HALO_XM_PACKED, WMFW_ADSP2_YM, WMFW_HALO_YM_PACKED };
    const uint32_t bases[] = { xm_base, xm_base, ym_base, ym_base };
    for (int i = 0; i < 4; i++) {
        if (n_alg_regions >= kMaxAlgRegions) return CS_ENOMEM;
        CsDspAlgRegion &a = alg_regions[n_alg_regions++];
        a.type = types[i]; a.alg = id; a.ver = ver; a.base = bases[i];
    }
    return CS_OK;
}

const CsDspAlgRegion *CsDsp::findAlgRegion(int type, uint32_t id) const {
    for (int i = 0; i < n_alg_regions; i++)
        if (alg_regions[i].alg == id && alg_regions[i].type == type)
            return &alg_regions[i];
    return nullptr;
}

// cs_dsp_halo_setup_algs() + cs_dsp_read_algs()
int CsDsp::setupAlgs() {
    const CsDspMemRegion *mem = findRegion(WMFW_ADSP2_XM);
    uint8_t hdr[40];   // struct wmfw_halo_id_hdr
    int ret = bus->rawRead(mem->base, hdr, sizeof(hdr));
    if (ret) { CS_LOG("%s: failed to read algorithm info: %d", tag, ret); return ret; }

    fw_id = cs_be32(hdr + 12);
    fw_id_ver = cs_be32(hdr + 16);
    uint32_t xm_base = cs_be32(hdr + 20), ym_base = cs_be32(hdr + 28);
    uint32_t n_algs = cs_be32(hdr + 36);
    CS_LOG("%s: firmware: %#x vendor: %#x v%u.%u.%u, %u algorithms", tag, fw_id, cs_be32(hdr + 8),
           (fw_id_ver & 0xff0000) >> 16, (fw_id_ver & 0xff00) >> 8, fw_id_ver & 0xff, n_algs);

    n_alg_regions = 0;
    ret = addAlgRegions(fw_id, fw_id_ver, xm_base, ym_base);
    if (ret) return ret;

    if (n_algs == 0) { CS_LOG("%s: no algorithms", tag); return CS_EINVAL; }
    if (n_algs > 64) { CS_LOG("%s: algorithm count %u excessive", tag, n_algs); return CS_EINVAL; }

    uint32_t pos = sizeof(hdr) / 4;
    uint32_t len = (24 * n_algs) / 4;
    uint8_t term[4];
    ret = bus->rawRead(regionToReg(mem, pos + len), term, 4);
    if (ret) { CS_LOG("%s: failed to read algorithm list end: %d", tag, ret); return ret; }
    if (cs_be32(term) != 0xbedead)
        CS_LOG("%s: algorithm list end %#x != 0xbedead", tag, cs_be32(term));

    size_t blen = (size_t)len * 4;
    uint8_t *algs = (uint8_t *)cs_alloc(blen);
    if (!algs) return CS_ENOMEM;
    ret = bus->rawRead(regionToReg(mem, pos), algs, blen);
    if (ret) { CS_LOG("%s: failed to read algorithm list: %d", tag, ret); cs_free(algs, blen); return ret; }

    for (uint32_t i = 0; i < n_algs && !ret; i++) {
        const uint8_t *a = algs + 24 * i;   // struct wmfw_halo_alg_hdr
        uint32_t id = cs_be32(a), ver = cs_be32(a + 4);
        uint32_t axm = cs_be32(a + 8), aym = cs_be32(a + 16);
        CS_LOG("%s: alg %u: ID %#x v%u.%u.%u XM@%#x YM@%#x", tag, i, id,
               (ver & 0xff0000) >> 16, (ver & 0xff00) >> 8, ver & 0xff, axm, aym);
        ret = addAlgRegions(id, ver, axm, aym);
    }
    cs_free(algs, blen);
    return ret;
}

// cs_dsp_load_coeff(): bin / WMDR
int CsDsp::loadCoeff(const uint8_t *d, size_t size) {
    const size_t kHdr = 16, kBlk = 20;
    if (!d || size <= kHdr) { CS_LOG("%s: coefficient file too short", tag); return CS_EINVAL; }
    if (d[0] != 'W' || d[1] != 'M' || d[2] != 'D' || d[3] != 'R') {
        CS_LOG("%s: invalid coefficient magic", tag);
        return CS_EINVAL;
    }
    uint32_t rev = cs_be32(d + 8) & 0xff;
    if (rev < 1 || rev > 3) { CS_LOG("%s: unsupported coefficient file format %u", tag, rev); return CS_EINVAL; }

    size_t pos = cs_le32(d + 4);
    int blocks = 0, errors = 0;
    uint32_t written = 0;
    while (pos < size) {
        if (kBlk > size - pos) return CS_EOVERFLOW;
        const uint8_t *b = d + pos;
        uint32_t blen = cs_le32(b + 16);
        if (blen > size - pos - kBlk) return CS_EOVERFLOW;
        int type = cs_le16(b + 2);
        uint32_t offset = cs_le16(b);
        uint32_t id = cs_le32(b + 4);
        uint32_t version = cs_le32(b + 8) >> 8;
        uint32_t reg = 0;

        switch (type) {
        case (WMFW_NAME_TEXT << 8): {
            char txt[101];
            uint32_t n = blen < 100 ? blen : 100;
            for (uint32_t i = 0; i < n; i++) { char c = (char)b[kBlk + i]; txt[i] = (c >= 0x20 && c < 0x7f) ? c : ' '; }
            txt[n] = 0;
            CS_LOG("%s: bin: %s", tag, txt);
            break;
        }
        case (WMFW_INFO_TEXT << 8):
        case (WMFW_METADATA << 8):
            break;
        case (WMFW_ABSOLUTE << 8):
            if (id == fw_id && offset == 0) {
                // "global coefficients" live in ZM, which HALO does not have.
                CS_LOG("%s: bin.%d: no ZM", tag, blocks);
            } else {
                reg = offset;
            }
            break;
        case WMFW_ADSP2_XM_LONG:
        case WMFW_ADSP2_YM_LONG:
        case WMFW_HALO_XM_PACKED_LONG:
        case WMFW_HALO_YM_PACKED_LONG:
            offset = cs_le32(b + 12);
            type &= 0xff;
            /* fallthrough */
        case WMFW_ADSP2_XM:
        case WMFW_ADSP2_YM:
        case WMFW_HALO_XM_PACKED:
        case WMFW_HALO_YM_PACKED:
        case WMFW_HALO_PM_PACKED: {
            const CsDspMemRegion *mem = findRegion(type);
            if (!mem) { CS_LOG("%s: bin.%d: no base for region %x", tag, blocks, type); errors++; break; }
            const CsDspAlgRegion *ar = findAlgRegion(type, id);
            if (ar) {
                if (version != ar->ver)
                    CS_LOG("%s: bin.%d: alg %#x coeff version %u.%u.%u but expected %u.%u.%u", tag, blocks, id,
                           (version >> 16) & 0xff, (version >> 8) & 0xff, version & 0xff,
                           (ar->ver >> 16) & 0xff, (ar->ver >> 8) & 0xff, ar->ver & 0xff);
                reg = regionToReg(mem, ar->base) + offset;
            } else {
                CS_LOG("%s: bin.%d: no region %x for algorithm %#x", tag, blocks, type, id);
                errors++;
            }
            break;
        }
        default:
            CS_LOG("%s: bin.%d: unknown region type %x at %u", tag, blocks, type, (unsigned)pos);
            errors++;
            break;
        }

        if (reg) {
            if ((blen & 3) || !regInDspRange(reg, blen)) {
                // Linux would hand this to regmap which rejects it; we refuse
                // explicitly and count it as an error.
                CS_LOG("%s: bin.%d: refusing write of %u bytes at %#x", tag, blocks, blen, reg);
                errors++;
            } else {
                int ret = bus->rawWrite(reg, b + kBlk, blen);
                if (ret) {
                    // Linux logs and continues here (cs_dsp_load_coeff); so do we.
                    CS_LOG("%s: bin.%d: failed to write to %#x: %d", tag, blocks, reg, ret);
                    errors++;
                } else {
                    written += blen;
                }
            }
        }
        pos += (blen + kBlk + 3) & ~3u;
        blocks++;
    }
    bytes_written += written;
    CS_LOG("%s: bin: %d blocks, %u bytes, %d errors", tag, blocks, written, errors);
    return errors ? CS_EIO : CS_OK;
}
