#!/usr/bin/env python3
# Dump the TLV contents of an Intel iwlwifi .ucode / .pnvm file:
# API/capability bits, command versions, IML/section sizes, PNVM SKUs.
# Used by the MTL CI job so the build log documents what the firmware advertises.
import struct
import sys
import zlib

MAGIC = 0x0a4c5749

TLV_NAMES = {
    1: "INST", 2: "DATA", 6: "PROBE_MAX_LEN", 7: "PAN", 18: "FLAGS",
    19: "SEC_RT", 20: "SEC_INIT", 21: "SEC_WOWLAN", 22: "DEF_CALIB",
    23: "PHY_SKU", 27: "NUM_OF_CPU", 28: "CSCHEME", 29: "API_CHANGES_SET",
    30: "ENABLED_CAPABILITIES", 31: "N_SCAN_CHANNELS", 32: "PAGING",
    34: "SEC_RT_USNIFFER", 35: "SDIO_ADMA_ADDR", 36: "FW_VERSION",
    38: "FW_DBG_DEST", 39: "FW_DBG_CONF", 40: "FW_DBG_TRIGGER",
    48: "CMD_VERSIONS", 50: "FW_GSCAN_CAPA", 51: "FW_MEM_SEG", 52: "IML",
    54: "UMAC_DEBUG_ADDRS", 55: "LMAC_DEBUG_ADDRS", 57: "FW_RECOVERY_INFO",
    58: "HW_TYPE", 59: "FMAC_RECOVERY_INFO", 60: "FW_FSEQ_VERSION",
    61: "PHY_INTEGRATION_VERSION", 62: "PNVM_VERSION", 64: "PNVM_SKU",
    66: "SEC_TABLE_ADDR", 67: "D3_KEK_KCK_ADDR", 68: "CURRENT_PC",
    74: "PNVM_DATA", 0x100: "FW_NUM_STATIONS", 0x102: "FW_NUM_BEACONS",
    0x1000005: "DBG_INFO", 0x1000006: "DBG_BUF_ALLOC", 0x1000007: "DBG_HCMD",
    0x1000008: "DBG_REGIONS", 0x1000009: "DBG_TRIGGERS", 0x100000a: "DBG_CONF_SET",
}

# Types the itlwm iwx parser knows about (anything else is logged + skipped).
GROUPS = {0: "LEGACY", 1: "LONG", 2: "SYSTEM", 3: "MAC_CONF", 4: "PHY_OPS",
          5: "DATA_PATH", 0xb: "LOCATION", 0xc: "REGULATORY_NVM",
          0x10: "DEBUG", 0x1f: "STATISTICS"}


def tlvs(buf, off):
    while off + 8 <= len(buf):
        t, l = struct.unpack_from("<II", buf, off)
        off += 8
        if off + l > len(buf):
            print("  !! truncated TLV type 0x%x len %d" % (t, l))
            return
        yield t, buf[off:off + l]
        off += (l + 3) & ~3


def ucode(path):
    buf = open(path, "rb").read()
    print("== %s: %d bytes, zlib %d bytes (ratio %.2f)" % (
        path, len(buf), len(zlib.compress(buf)), len(buf) / len(zlib.compress(buf))))
    zero, magic = struct.unpack_from("<II", buf, 0)
    if zero != 0 or magic != MAGIC:
        print("  not a TLV ucode file")
        return
    human = buf[8:72].split(b"\0")[0].decode(errors="replace")
    ver, build = struct.unpack_from("<II", buf, 72)
    print("  human: %s  ver 0x%08x (major %d minor %d api %d) build %d" % (
        human, ver, ver >> 24, (ver >> 16) & 0xff, (ver >> 8) & 0xff, build))
    counts = {}
    api = set()
    capa = set()
    cmdv = []
    for t, d in tlvs(buf, 88):
        counts.setdefault(t, [0, 0])
        counts[t][0] += 1
        counts[t][1] += len(d)
        if t == 29:
            idx, flags = struct.unpack_from("<II", d)
            api |= {idx * 32 + i for i in range(32) if flags & (1 << i)}
        elif t == 30:
            idx, flags = struct.unpack_from("<II", d)
            print("  capa index %d = 0x%08x" % (idx, flags))
            capa |= {idx * 32 + i for i in range(32) if flags & (1 << i)}
        elif t == 48:
            for i in range(len(d) // 4):
                cmdv.append(struct.unpack_from("<BBBB", d, i * 4))
        elif t == 36 and len(d) == 12:
            print("  FW_VERSION %d.%d.%d" % struct.unpack("<III", d))
        elif t in (6, 18, 23, 27, 31, 32, 0x100, 0x102) and len(d) >= 4:
            print("  %s = %u (0x%x)" % (TLV_NAMES.get(t), struct.unpack_from("<I", d)[0],
                                       struct.unpack_from("<I", d)[0]))
        elif t == 60 and len(d) >= 32:
            print("  FSEQ %s" % d[:32].split(b"\0")[0].decode(errors="replace"))
    print("  TLV types (type name count bytes):")
    for t in sorted(counts):
        print("    0x%07x %-22s %4d %8d" % (t, TLV_NAMES.get(t, "?"), counts[t][0], counts[t][1]))
    print("  API bits: %s" % sorted(api))
    print("  CAPA bits: %s" % sorted(capa))
    print("  CMD_VERSIONS (%d entries) group/cmd: cmd_ver notif_ver" % len(cmdv))
    for cmd, grp, cv, nv in sorted(cmdv, key=lambda x: (x[1], x[0])):
        print("    %-14s 0x%02x/0x%02x: cmd %3d notif %3d" % (GROUPS.get(grp, "0x%x" % grp), grp, cmd, cv, nv))


def pnvm(path):
    buf = open(path, "rb").read()
    print("== %s: %d bytes, zlib %d bytes (ratio %.2f)" % (
        path, len(buf), len(zlib.compress(buf)), len(buf) / len(zlib.compress(buf))))
    cur = None
    for t, d in tlvs(buf, 0):
        if t == 64:
            cur = struct.unpack_from("<III", d) if len(d) >= 12 else d
            print("  SKU %s" % (" ".join("0x%08x" % x for x in cur) if isinstance(cur, tuple) else cur))
        elif t == 58 and len(d) >= 4:
            print("    HW_TYPE mac 0x%x rf 0x%x" % struct.unpack_from("<HH", d))
        elif t == 62 and len(d) >= 4:
            print("    PNVM_VERSION 0x%08x" % struct.unpack_from("<I", d))
        elif t == 19:
            off = struct.unpack_from("<I", d)[0]
            print("    SEC_RT offset 0x%08x len %d" % (off, len(d) - 4))
        else:
            print("    TLV 0x%x %s len %d" % (t, TLV_NAMES.get(t, "?"), len(d)))


for p in sys.argv[1:]:
    if p.endswith(".pnvm"):
        pnvm(p)
    else:
        ucode(p)
