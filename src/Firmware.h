// Embedded linux-firmware files (generated into build/FirmwareData.cpp by
// scripts/fw2c.py from files fetched by scripts/fetch_fw.sh).
// License: Cirrus Logic redistributable firmware, see LICENSE.cirrus in linux-firmware.
#pragma once
#include <stdint.h>

struct CsFwFile {
    const char *name;     // Linux request_firmware() path, e.g. "cirrus/cs35l56-...bin"
    const uint8_t *data;
    uint32_t size;
};

// Returns nullptr if not embedded.
const CsFwFile *csFindFirmware(const char *linuxName);
