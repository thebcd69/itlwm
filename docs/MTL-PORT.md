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

## Plan

1. Gap analysis (below) — **done**.
2. Implement in the order of the table, gated on `sc_use_mld_api` / MA MAC type / advertised command versions so other cards keep their old paths.
3. Logging: every new path prints `XYLog("MTL: ...")` (grep the boot log for `MTL:`).
4. Commit small; intermediate commits carry `[skip ci]`, milestone commits (end of bring-up / scan / association / data) do not, so they produce a test build.

## Gap analysis (OpenBSD if_iwx.c r1.230 → itlwm ItlIwx.cpp)

Line numbers: OpenBSD = `/home/claude/obsd/if_iwx.c`, itlwm = `itlwm/hal_iwx/ItlIwx.cpp` at 0134ec2 (they drift as code is added; search by function name).
Priority: P0 = card cannot get past this point without it, P1 = needed for a working link, P2 = robustness/perf.
Status: `ok` = itlwm already equivalent, `todo`, `done` = ported (compiled, untested on hardware).

### A. Bring-up

| # | Requirement | OpenBSD | itlwm | Prio | Status |
|---|---|---|---|---|---|
| A1 | MA attach config: integrated=1, ltr NONE, low_latency_xtal=0, xtal_latency=0, uhb=1, AX210 family | attach `PCI_PRODUCT_INTEL_WL_22500_14` ~12514 | `iwl_ma_trans_cfg` + `iwl_cfg_ma_b0_gf_a0` (dev-info table, last match wins) | P0 | ok (values already identical) |
| A2 | Firmware TLV parse: unknown TLVs (0x39,0x3a,0x3c,0x3d,0x44,0x45,0x100-0x102,0x100000b/c) must not abort | `iwx_read_firmware` 1199 (default → EINVAL, but lists all of them) | `iwx_read_firmware` 1371 (default just logs) | P0 | ok + MTL log + quiet cases |
| A3 | IML TLV (13944 B) stored and handed to context info | 1585, `iwx_ctxt_info_gen3_init` 991 | `sc_iml[20480]`, `iwx_ctxt_info_gen3_init` 1107 | P0 | ok |
| A4 | Paging / SEC_RT (60 sections) / FW_MEM_SEG / FSEQ | `iwx_init_fw_sec` 638 | `iwx_init_fw_sec` 761 | P0 | ok (same code) |
| A5 | ALIVE notif v6 (v5 + IMR info): status, error tables, **SKU id** | `iwx_rx_pkt` IWX_ALIVE 10995 (by notif version) | `iwx_rx_pkt` IWX_ALIVE (by size; v6 falls into v5 branch by luck) | P0 | done (untested) |
| A6 | PNVM: pick SKU block matching ALIVE sku_id, collect SEC_RT payloads | `iwx_pnvm_parse` 4310, `iwx_pnvm_handle_section` 4194 | `iwx_read_pnvm` 2003, `iwx_pnvm_handle_section` 1846 | P0 | ok for selection; see A7 |
| A7 | **Fragmented PNVM** (CAPA 32 `FRAGMENTED_PNVM_IMG` is set in -89): base addr must point to `iwx_pnvm_info_dram` (array of segment addrs) | `iwx_pnvm_setup_fragmented` 4130, `iwx_ctxt_info_gen3_set_pnvm` 4349 | concatenates segments into one blob | P0 | done (untested) |
| A8 | PNVM doorbell + wait for `PNVM_INIT_COMPLETE` flag | `iwx_load_pnvm` 4376 | `iwx_load_pnvm` 2105 (single tsleep, no flag) | P0 | done (untested) |
| A9 | NVM_GET_INFO rsp v4 (API REGULATORY_NVM_INFO) | `iwx_nvm_get` 3978 | `iwx_nvm_get` 4585 | P0 | ok (identical) |
| A10 | MLD API switch: CAPA 110 && (MAC type MA or BZ) → `sc_use_mld_api` | `iwx_run_init_mvm_ucode` 4576 | missing | P0 | done (untested) |
| A11 | init_hw: tx-ant, (phy cfg), BT, SOC v2, DQA, LTR, CT-kill, power, MCC, scan cfg, beacon filter off. **No aux STA, no PHY ctxt at init** | `iwx_init_hw` 10094 | `iwx_init_hw` 10049 (adds aux STA via ADD_STA and 3 PHY ctxts) | P0 | done (untested) |
| A12 | MCC_UPDATE rsp v6 → use `iwx_mcc_update_resp_v4` when CAPA 89 (MCC_UPDATE_11AX) is set; reject resp ver ≥ 8 | `iwx_send_update_mcc_cmd` 9972 | `iwx_send_update_mcc_cmd` 9961 (old struct only → EIO → init fails) | P0 | done (untested) |
| A13 | SCAN_CFG reduced: bcast_sta_id=0xff only if SCAN_CFG ver < 5 / unknown | `iwx_config_umac_scan_reduced` 7473 | `iwx_config_umac_scan` 7540 (keys off ADD_STA ver → AUX_STA_ID) | P1 | done (untested) |
| A14 | rate_n_flags version from TX_CMD notif ver (>6 → v2) | `iwx_post_alive` 3062 | n/a (tx_fill_cmd keys off TX cmd ver > 8, equivalent for mgmt) | P1 | partial, see D |
| A15 | RX notif handling for new groups (STATISTICS 0x10 oper notifs, SYSTEM 0xfc, DATA_PATH 0xf8, BT_COEX PROFILE, MLD cmd responses copied to resp buffer) | `iwx_rx_pkt` 11250-11400 | missing → "unhandled firmware response" + no resp copy for MLD cmds | P0 (resp copy) | done (untested) |

### B. Scan

| # | Requirement | OpenBSD | itlwm | Prio | Status |
|---|---|---|---|---|---|
| B1 | SCAN_REQ_UMAC v17 (`scan_req_umac_v17`, general params v11, channel params v7, channel cfg v5 with band in flags, psd_20=-128) | `iwx_umac_scan_v17` 7750 + helpers 7507-7690 | only v12/v14 + legacy | P0 | done (untested) |
| B2 | Dispatch by cmd version, size by version | `iwx_initiate_scan` 8257 | `iwx_umac_scan` 7744, `iwx_umac_scan_size` 7660 | P0 | done (untested) |
| B3 | Scan complete / iteration complete → endscan | rx_pkt 11176 | same | P0 | ok + MTL log |

### C. Association (MLD API, all gated on `sc_use_mld_api`)

| # | Requirement | OpenBSD | itlwm | Prio | Status |
|---|---|---|---|---|---|
| C1 | PHY ctxt v4 ADD in auth (not at init), REMOVE in deauth; RLC_CONFIG v2 after every PHY ctxt add/modify | `iwx_auth` 8970, `iwx_phy_ctxt_update` 8917, `iwx_phy_send_rlc` 8893 | PHY added at init, MODIFY in auth; no RLC | P0 | todo |
| C2 | MAC_CONFIG_CMD v2 (`iwx_mac_config_cmd`) instead of MAC_CONTEXT_CMD | `iwx_mld_mac_ctxt_cmd` 8108 | `iwx_mac_ctxt_cmd` 8395 | P0 | todo |
| C3 | No BINDING with MLD | `iwx_binding_cmd` 5586 | `iwx_binding_cmd` 6018 | P0 | todo |
| C4 | LINK_CONFIG v2 add + modify (rates, EDCA, protection, beacon timing) | `iwx_mld_modify_link_fill` 6954 | missing | P0 | todo |
| C5 | STA_CONFIG (not in CMD_VERSIONS → v1 size `iwx_mvm_sta_cfg_cmd`) / STA_REMOVE + link deactivate/remove | `iwx_mld_add_sta_cmd` 7039, `iwx_mld_rm_sta_cmd` 7254 | `iwx_add_sta_cmd` 7157 (ADD_STA) | P0 | todo |
| C6 | No aux STA (scan uses no STA with reduced scan cfg v5+) | — | `iwx_add_aux_sta` 7275 called from init_hw | P0 | todo (skip) |
| C7 | Session protection v2 with id_and_color | `iwx_schedule_session_protection` 3078 | `iwx_schedule_protect_session` 3617 | P1 | check/ok |
| C8 | SEC_KEY_CMD v1 (sta_mask) for CCMP pairwise/group, IGTK | `iwx_mld_set_sta_key_cmd` 9351, `iwx_add_sta_key` 9513, `iwx_delete_key` 9590 | `iwx_set_key` 9526 (ADD_STA_KEY) | P0 (WPA2) | todo |
| C9 | SCD_QUEUE_CONFIG v3 (ADD/REMOVE with sta_mask) for mgmt queue (qid 1) and agg queues; explicit removal before STA remove | `iwx_enable_txq` 2914, `iwx_disable_txq` 3003, `iwx_enable_mgmt_queue` 8475, `iwx_rm_sta` 7187 | `iwx_enable_txq` 3306, `iwx_tvqm_enable_txq` 3402 (legacy SCD_QUEUE_CFG) | P0 | todo |
| C10 | RX BAID alloc v2 (`RX_BAID_ALLOCATION_CONFIG_CMD`, sta_mask) instead of ADD_STA BA fields | `iwx_sta_rx_agg_baid_cfg_cmd` 3436, `iwx_sta_rx_agg` 3546 | `iwx_sta_rx_agg` 4144 (ADD_STA) | P1 | todo |
| C11 | TLC v4 | `iwx_rs_init_v4` 8692 | `iwx_rs_init` 8891 (v4 struct already) | P1 | check |
| C12 | run(): STA update before MAC modify(assoc=1); run_stop: no quotas / binding | `iwx_run` 9139, `iwx_run_stop` 9260 | `iwx_run` 9332 (quotas only if !DYNAMIC_QUOTA → capa set, skipped) | P1 | todo (order under MLD) |
| C13 | Flush: TXPATH_FLUSH v? / no drain with MLD | `iwx_drain_sta` 6540, `iwx_flush_sta` 6581 | `iwx_drain_sta` 6989 (ADD_STA) | P1 | todo |
| C14 | Beacon filter / power table | `iwx_set_pslevel` 6700, `iwx_disable_beacon_filter` 6808 | `iwx_power_update_device`, `iwx_power_mac_update_mode` | P2 | check |

### D. Data path

| # | Requirement | OpenBSD | itlwm | Prio | Status |
|---|---|---|---|---|---|
| D1 | TX_CMD v10 = gen3 struct, rate_n_flags v2 for mgmt/multicast | `iwx_tx_fill_cmd` 6123, `iwx_tx` 6233 | `iwx_tx_fill_cmd` 6574 (v2 rate when TX ver > 8) | P0 | ok (check sta_id/queue mapping) |
| D2 | TX resp notif v7 | `iwx_rx_tx_cmd` 5418 | `iwx_rx_tx_cmd` 5931 | P1 | check |
| D3 | RX MPDU desc for AX210 family (one pkt per buffer, rx_mpdu_desc v3) | `iwx_rx_mpdu_mq` 5201 | `iwx_rx_mpdu_mq` 5612 | P0 | ok (AX210 path exists) |
| D4 | TLC update notif with rate v2 | `iwx_rs_update` 8809 | `iwx_rs_update` 9061 | P2 | check |
| D5 | Compressed BA notif → txq advance by fw queue id | `iwx_rx_compressed_ba` 5488 | `iwx_rx_tx_ba_notif` 5849 | P1 | check |

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
