// Abstract 32-bit register bus used by the CS35L56 core.
// Wire format (Linux regmap, sound/soc/codecs/cs35l56-shared.c cs35l56_regmap_spi):
//   reg_bits=32 BE, pad_bits=16, val_bits=32 BE, reg_stride=4,
//   read flag = 0x80 in the first (MSB) address byte (regmap-spi read_flag_mask).
#pragma once
#include "Platform.h"

class CsRegBus {
public:
    virtual ~CsRegBus() {}
    virtual int write(uint32_t reg, uint32_t val) = 0;
    virtual int read(uint32_t reg, uint32_t *val) = 0;
    // len must be a multiple of 4. Data is sent as-is (already big-endian
    // DSP words, exactly what cs_dsp hands to regmap_raw_write).
    virtual int rawWrite(uint32_t reg, const uint8_t *buf, size_t len) = 0;
    virtual int rawRead(uint32_t reg, uint8_t *buf, size_t len) = 0;
};
