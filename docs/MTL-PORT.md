# itlwm: Meteor Lake AX211 (8086:7E40) port — handoff

> **Sahip için:** Bu dosyayı diğer sohbetteki Claude'a ver. GU605MY'deki Wi-Fi kartını macOS'ta çalıştırmak için itlwm sürücüsünü güncelleme işinin kaldığı yeri, elimizdeki verileri ve sıradaki adımları anlatıyor. Ajan durduruldu; yaptığı her şey `thebcd69/itlwm` reposunun `mtl` dalında.

## Context

- Machine: ASUS ROG Zephyrus G16 **GU605MY**, Intel Core Ultra 9 185H (Meteor Lake), RTX 4090 Laptop.
- Hackintosh state: macOS **Sequoia 15.8.1** installed on the KIOXIA SSD ("Macintosh HD", APFS, 300 GB). It boots from a USB stick (SanDisk, FAT32 partition `OPENCORE`) with an OpenCore EFI built by the 1401 app. Working: CPU (custom MADT fix), NVIDIA via NullMoth driver (240 Hz), headphone output (done in another chat). **Not working: Wi-Fi.** The owner stops the project unless Wi-Fi works.
- Wi-Fi card: Meteor Lake CNVi + AX211. PCI `8086:7E40`, subsystem `8086:0094`, PCI REV `0x20`. MAC type MA (`0x44`), RF GF (HW_TYPE rf `0x10d` in the PNVM). linux-firmware only ships `iwlwifi-ma-b0-gf-a0-{83,86,88,89}.ucode` + `iwlwifi-ma-b0-gf-a0.pnvm` for this combo, so treat it as **MA B0**.
- Why stock itlwm fails: itlwm 2.4.0 matches 7E40 but loads a non-existent `iwlwifi-ma-a0-gf-a0-63.ucode`, and its iwx code (OpenBSD if_iwx.c v1.45, 2020, plus later Linux-derived additions) predates the firmware generation this card needs.

## Reference implementation

OpenBSD's current iwx (`if_iwx.c` rev **1.230**, 2026-09-19) supports exactly this combo:
- `IWX_MA_B_GF_A_FW "iwx-ma-b0-gf-a0-89"` + `IWX_MA_B_GF_A_PNVM`.
- Attach config for MA (PCI_PRODUCT_INTEL_WL_22500_14): `integrated=1`, `ltr_delay=NONE`, `low_latency_xtal=0`, `xtal_latency=0`, `uhb_supported=1`.
- On MA it enables the **MLD API** when the firmware has `IWX_UCODE_TLV_CAPA_MLD_API_SUPPORT` (110): `sc->sc_use_mld_api = 1` (around line 4574). Association then uses `iwx_mld_mac_ctxt_cmd`, `iwx_mld_modify_link_fill`, `iwx_mld_add_sta_cmd`, `iwx_mld_rm_sta_cmd`, etc. instead of the legacy MAC context / binding / ADD_STA commands.

Get it from the openbsd/src GitHub mirror: `sys/dev/pci/if_iwx.c`, `if_iwxreg.h`, `if_iwxvar.h` (and `sys/net80211/` if needed).

## Repository and CI

- Repo: **`thebcd69/itlwm`** (fork of OpenIntelWireless/itlwm). Work only on branch **`mtl`**. Do not touch `master`, do not force-push, do not open PRs/issues upstream.
- iwx code: `itlwm/hal_iwx/ItlIwx.cpp` (+ `ItlIwx.hpp`, `if_iwxreg.h`, `if_iwxvar.h`). It is OpenBSD C converted to C++ class methods (`ItlIwx::iwx_*`). net80211 is in `itl80211/openbsd/net80211/`. Log macro: `XYLog`.
- CI: `.github/workflows/mtl.yml`. Every push to `mtl` builds on a GitHub macOS runner (macos-14 + acidanthera/MacKernelSDK). It downloads the -89 ucode + pnvm from kernel.org (`intel/iwlwifi/` path) into `itlwm/firmware/`, dumps the firmware TLVs with `scripts/iwx_fw_info.py`, builds itlwm **Debug**, and publishes a pre-release `mtl-<shortsha>` with `itlwm-mtl-<shortsha>.zip` (~16 MB). A build takes about 6 minutes. There is no local Xcode, so CI is the compiler.

### Commits on `mtl` so far

| Commit | What |
|---|---|
| `95ae312` | New cfg `iwl_cfg_ma_b0_gf_a0` → `iwlwifi-ma-b0-gf-a0-89.ucode`; MA+GF dev-info entries (incl. Killer 1671/1672) switched to it; off-by-one fix in `iwx_read_pnvm` (it built `...-gf-a.pnvm` instead of `...-gf-a0.pnvm`); CI workflow with pre-releases. CI green, release `mtl-95ae312`. |
| `487859d` | `scripts/iwx_fw_info.py` + CI step that prints the firmware's TLVs, command versions and PNVM SKUs. CI green. |
| (this commit) | Correct command-group names in the dump script; this handoff file; `docs/fw89-tlv-dump.txt`. |

**Nothing in the driver logic has been ported yet.** The agent stopped during the gap analysis, while reading the scan code.

## What the -89 firmware advertises

Full dump: `docs/fw89-tlv-dump.txt`. In that file, group `0x0b` is labelled LOCATION and `0x10` DEBUG; the correct iwlwifi names are **PROT_OFFLOAD (0x0b)** and **STATISTICS (0x10)**. The script is fixed now; the next CI log prints the right names.

- Version string `release/core86:signed_remote:b20dcd1f`, FW_VERSION 89. FSEQ 0.0.3.94. `NUM_OF_CPU=2`, `N_SCAN_CHANNELS=67`, `FW_NUM_STATIONS=15`, PAGING present.
- Has an **IML** TLV (0x34). itlwm already parses `IWX_UCODE_TLV_IML`.
- TLVs itlwm probably does not know: 0x39 FW_RECOVERY_INFO, 0x3a HW_TYPE, 0x3c FSEQ, 0x3d PHY_INTEGRATION_VERSION, 0x44 CURRENT_PC, 0x45, 0x100–0x102, 0x100000b/0x100000c debug. itlwm logs unknown TLVs ("Found TLV ...") — check that none of them abort the load.
- CAPA bit **110 (MLD_API_SUPPORT) is set**, so the OpenBSD MA path (MLD API) applies.
- PNVM has 4 SKU blocks for mac 0x44 / rf 0x10d (SKU ids `0x000610d1/0`, `0x000610d1/3`, `0x000a10d1/0`, `0x000510d1/0`). The driver must pick the block matching the SKU id the device/firmware reports (see OpenBSD `iwx_pnvm_parse` and `iwx_load_pnvm`).

Key command versions (group/cmd → version), names checked against OpenBSD `if_iwxreg.h`:

| Command | Group/ID | Version |
|---|---|---|
| ALIVE notification | LEGACY 0x01 | notif **6** |
| PHY_CONTEXT_CMD | LONG 0x08 | **4** |
| SCAN_REQ_UMAC | LONG 0x0d | **17** |
| TX_CMD | LONG 0x1c | **10** (TX response notif 7) |
| MCC_UPDATE_CMD | LONG 0xc8 | cmd 1, response **6** |
| SESSION_PROTECTION_CMD | MAC_CONF 0x05 | 2 |
| MAC_CONFIG_CMD (MLD) | MAC_CONF 0x08 | 2 |
| LINK_CONFIG_CMD (MLD) | MAC_CONF 0x09 | 2 |
| ROC_CMD | MAC_CONF 0x0e | 3 |
| RLC_CONFIG_CMD | DATA_PATH 0x08 | 2 |
| TLC_MNG_CONFIG_CMD | DATA_PATH 0x0f | 4 |
| RX_BAID_ALLOCATION_CONFIG_CMD | DATA_PATH 0x16 | 2 |
| SCD_QUEUE_CONFIG_CMD | DATA_PATH 0x17 | 3 |
| SEC_KEY_CMD | DATA_PATH 0x18 | 1 |
| NVM_GET_INFO | REGULATORY_NVM 0x02 | cmd 1, response **4** |

STA_CONFIG_CMD (0x0a) and STA_REMOVE_CMD (0x0c) are not listed in CMD_VERSIONS; check how OpenBSD treats a missing entry.

## Plan (not started)

1. **Gap analysis** → write a table here: each requirement, OpenBSD function/line → itlwm function, priority, status. Order: bring-up → scan → associate → data. Areas: TLV parsing, PNVM SKU selection, context-info/IML loading for integrated AX210-family, ALIVE v6, init commands (NVM_GET_INFO v4 response, PHY/SOC/LARI/MCC, RLC), UMAC scan v17, PHY context v4, the MLD path (MAC/link/STA config, aux/mgmt stations, session protection, keys via SEC_KEY_CMD, BAID with station masks, SCD queue config, TLC v4, beacon filter, power), TX v10 / RX descriptors.
2. **Implement in that order.** Gate new code on `sc_use_mld_api`, MA MAC type or the firmware's advertised command versions, the way OpenBSD does, so other cards keep their current paths. Port OpenBSD logic faithfully; adapt only the plumbing (C++ methods, IOKit DMA, `mbuf_t`, timeouts, itl80211).
3. **Logging:** add `XYLog("MTL: ...")` lines for firmware name + API, PNVM SKU loaded or not, ALIVE version/status, MLD API on/off, each init command + result, scan request version + scan-complete count, and each MLD MAC/link/STA command + return code.
4. Keep CI green; every meaningful step ends in a published pre-release. Keep this file current: done / compiled-but-untested / risks / test checklist.

## Hardware test procedure (only when the owner is at the PC)

- The owner is away until **Wednesday morning (Europe/Istanbul)**. No USB/macOS testing before that.
- On Windows: the first test build (`mtl-95ae312`) is already downloaded and SHA-256-verified at `C:\Users\Baha\Tools\hackintosh-usb\mtl\95ae312\itlwm.kext`. Use the newest pre-release instead if the code has moved on.
- Install: on the USB `OPENCORE` partition (drive letter varies; it was `E:`), rename `EFI\OC\Kexts\itlwm.kext` to a backup name (OpenCore only loads kexts listed in config.plist), then copy the new `itlwm.kext` in. `config.plist` already has itlwm enabled (`Kernel > Add`). Do not change other config entries; the headphone/audio settings were changed in another chat.
- Boot: Esc at the ASUS logo → USB → OpenCore → "Macintosh HD". The custom MADT (`ACPI/APIC-BSP0.aml`) shows macOS only the 12 P-core threads. Try booting with G-Helper E-cores at 8; if it panics while starting CPUs, set E-cores to 0 in G-Helper (needs a reboot).
- Collect logs in macOS Terminal:
  ```
  sudo dmesg > /Volumes/OPENCORE/t1-dmesg.txt; log show --last boot --predicate 'eventMessage CONTAINS[c] "itlwm"' > /Volumes/OPENCORE/t1-log.txt; ifconfig -a > /Volumes/OPENCORE/t1-ifconfig.txt; sync
  ```
  Then reboot to Windows and read the files from the USB. Wi-Fi is joined with the HeliPort app (not installed yet); internet on the Mac is possible via iPhone USB tethering.

## Owner's rules (keep these)

- Reply in Turkish; be direct and technically precise.
- No permanent deletion: rename or use the Recycle Bin/Trash.
- Ask before any download on his PC (file name, source, size). He already allowed the itlwm test builds from `github.com/thebcd69/itlwm` releases (~16 MB each).
- Never touch the Windows SSD (`HFS002…`, disk 0) or the firmware boot order. Never add NVRAM boot entries.
- Don't raise CPU/GPU power limits.
- Speakers (Cirrus CS35L56 amps on SPI) come last, only after Wi-Fi.
