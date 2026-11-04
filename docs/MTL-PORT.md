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
| `95ae312` | New cfg `iwl_cfg_ma_b0_gf_a0` → `iwlwifi-ma-b0-gf-a0-89.ucode`; MA+GF dev-info entries switched to it; PNVM file-name fix; CI workflow. |
| `487859d` | `scripts/iwx_fw_info.py` + CI TLV dump. |
| `0134ec2` | Handoff notes, `docs/fw89-tlv-dump.txt`. |
| `31f7622` | Gap analysis table (this file). |
| `50254a2` | Bring-up: MLD/MA register defs, fragmented PNVM, ALIVE v6, MCC resp v4, MLD switch. |
| `6b0d9cf` | **Milestone build**: UMAC scan v17, MLD-gated init_hw, MLD cmd responses in rx_pkt. |
| `413a70d` | MLD command builders (PHY v4, RLC, MAC/LINK/STA, SCD v3, BAID, SEC_KEY). |
| `fc062b9` | **Milestone build**: association via MLD API (auth/deauth/run, queues, BA, keys, stats). |
| `1751ebb` | **Milestone build**: data path review (EAPOL high-prio), MLD-only gating of stats/bmiss/channel-switch, status + risks + test checklist in this file. |

| `614f5e4` | ALIVE wait checks the flag first (MA only). |
| `d2953ae` | **Milestone build** (CI green): TLV parser capa-index fix (A0) — without it no build before this one could load the -89 firmware. |
| `695815c` | MAC_CONFIG `he_support` for HE nodes; HE gap documented. |
| (latest, see `git log`) | **Milestone build**: test build containing everything above. **Use the newest `mtl-<sha>` pre-release for the first hardware test.** |

**Driver logic for this card is now ported but completely untested on hardware.** Everything below "done" means "compiles locally (clang -fsyntax-only) and follows OpenBSD r1.230"; CI is the real compiler.

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
| A14 | rate_n_flags version from TX_CMD notif ver (>6 → v2) | `iwx_post_alive` 3062 | n/a (tx_fill_cmd keys off TX cmd ver > 8, equivalent for mgmt) | P1 | done (sc_rate_n_flags_version set; TX path keys off TX_CMD ver > 8, equivalent) |
| A0 | **TLV parse must not stop early**: the -89 image sends ENABLED_CAPABILITIES index 4 (bit 129) right after CMD_VERSIONS; itlwm's `IWX_NUM_UCODE_TLV_CAPA` was 128, so the parser hit `goto parse_out` with err 0 and silently dropped every later TLV (all 60 SEC_RT sections, IML, N_SCAN_CHANNELS, ...). Also 60 SEC_RT sections = itlwm's old `IWX_UCODE_SECT_MAX` (no margin) | `IWX_NUM_UCODE_TLV_CAPA` 160, `IWX_UCODE_SECT_MAX` 69 | `if_iwxreg.h`, `if_iwxvar.h` | P0 | done (verified against the TLV order of a linux-firmware -89 image) |
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
| C1 | PHY ctxt v4 ADD in auth (not at init), REMOVE in deauth; RLC_CONFIG v2 after every PHY ctxt add/modify | `iwx_auth` 8970, `iwx_phy_ctxt_update` 8917, `iwx_phy_send_rlc` 8893 | PHY added at init, MODIFY in auth; no RLC | P0 | done (untested) |
| C2 | MAC_CONFIG_CMD v2 (`iwx_mac_config_cmd`) instead of MAC_CONTEXT_CMD | `iwx_mld_mac_ctxt_cmd` 8108 | `iwx_mac_ctxt_cmd` 8395 | P0 | done (untested) |
| C3 | No BINDING with MLD | `iwx_binding_cmd` 5586 | `iwx_binding_cmd` 6018 | P0 | done (untested) |
| C4 | LINK_CONFIG v2 add + modify (rates, EDCA, protection, beacon timing) | `iwx_mld_modify_link_fill` 6954 | missing | P0 | done (untested) |
| C5 | STA_CONFIG (not in CMD_VERSIONS → v1 size `iwx_mvm_sta_cfg_cmd`) / STA_REMOVE + link deactivate/remove | `iwx_mld_add_sta_cmd` 7039, `iwx_mld_rm_sta_cmd` 7254 | `iwx_add_sta_cmd` 7157 (ADD_STA) | P0 | done (untested) |
| C6 | No aux STA (scan uses no STA with reduced scan cfg v5+) | — | `iwx_add_aux_sta` 7275 called from init_hw | P0 | done (untested) |
| C7 | Session protection v2 with id_and_color | `iwx_schedule_session_protection` 3078 | `iwx_schedule_protect_session` 3617 | P1 | done: MLD path sends duration in TU (itlwm legacy path multiplies by 1024) |
| C8 | SEC_KEY_CMD v1 (sta_mask) for CCMP pairwise/group, IGTK | `iwx_mld_set_sta_key_cmd` 9351, `iwx_add_sta_key` 9513, `iwx_delete_key` 9590 | `iwx_set_key` 9526 (ADD_STA_KEY) | P0 (WPA2) | done (untested) |
| C9 | SCD_QUEUE_CONFIG v3 (ADD/REMOVE with sta_mask) for mgmt queue (qid 1) and agg queues; explicit removal before STA remove | `iwx_enable_txq` 2914, `iwx_disable_txq` 3003, `iwx_enable_mgmt_queue` 8475, `iwx_rm_sta` 7187 | `iwx_enable_txq` 3306, `iwx_tvqm_enable_txq` 3402 (legacy SCD_QUEUE_CFG) | P0 | done (untested) |
| C10 | RX BAID alloc v2 (`RX_BAID_ALLOCATION_CONFIG_CMD`, sta_mask) instead of ADD_STA BA fields | `iwx_sta_rx_agg_baid_cfg_cmd` 3436, `iwx_sta_rx_agg` 3546 | `iwx_sta_rx_agg` 4144 (ADD_STA) | P1 | done (untested) |
| C11 | TLC v4 | `iwx_rs_init_v4` 8692 | `iwx_rs_init` 8891 (v4 struct already) | P1 | ok (itlwm already sends v4 when advertised) |
| C12 | run(): STA update before MAC modify(assoc=1); run_stop: no quotas / binding | `iwx_run` 9139, `iwx_run_stop` 9260 | `iwx_run` 9332 (quotas only if !DYNAMIC_QUOTA → capa set, skipped) | P1 | done (untested) |
| C13 | Flush: TXPATH_FLUSH v? / no drain with MLD | `iwx_drain_sta` 6540, `iwx_flush_sta` 6581 | `iwx_drain_sta` 6989 (ADD_STA) | P1 | done (untested) |
| C14 | Beacon filter / power table | `iwx_set_pslevel` 6700, `iwx_disable_beacon_filter` 6808 | `iwx_power_update_device`, `iwx_power_mac_update_mode` | P2 | ok: legacy POWER_TABLE / MAC_PM_POWER_TABLE / BEACON_FILTERING are all in the -89 CMD_VERSIONS |

### D. Data path

| # | Requirement | OpenBSD | itlwm | Prio | Status |
|---|---|---|---|---|---|
| D1 | TX_CMD v10 = gen3 struct, rate_n_flags v2 for mgmt/multicast | `iwx_tx_fill_cmd` 6123, `iwx_tx` 6233 | `iwx_tx_fill_cmd` 6574 (v2 rate when TX ver > 8) | P0 | ok; MLD adds IWX_TX_FLAGS_HIGH_PRI for EAPOL like OpenBSD |
| D2 | TX resp notif v7 | `iwx_rx_tx_cmd` 5418 | `iwx_rx_tx_cmd` 5931 | P1 | ok (struct identical to OpenBSD; v7 only changes rate format) |
| D3 | RX MPDU desc for AX210 family (one pkt per buffer, rx_mpdu_desc v3) | `iwx_rx_mpdu_mq` 5201 | `iwx_rx_mpdu_mq` 5612 | P0 | ok (AX210 path exists) |
| D4 | TLC update notif with rate v2 | `iwx_rs_update` 8809 | `iwx_rs_update` 9061 | P2 | ok for HT/VHT/HE (itlwm decodes v2 when notif ver >= 3); legacy-rate branch still reads a v1 PLCP (display/ni_txrate only) |
| D5 | Compressed BA notif → txq advance by fw queue id | `iwx_rx_compressed_ba` 5488 | `iwx_rx_tx_ba_notif` 5849 | P1 | not changed; uses fw queue ids from the notif, should be fine |

## Where the port stands (resume here)

Stages 1-5 of the plan are implemented as far as possible without hardware. Code locations (search for `MTL`):

- **Bring-up**: `iwx_read_firmware` (MTL log + quiet TLVs), `iwx_pnvm_setup_fragmented` / `iwx_ctxt_info_gen3_set_pnvm` / `iwx_load_pnvm` (fragmented PNVM, flag wait), ALIVE v6 in `iwx_rx_pkt`, `iwx_run_init_mvm_ucode` (MLD switch, INIT_COMPLETE loop on MA), `iwx_send_update_mcc_cmd` (resp v4), `iwx_init_hw` (no aux STA / PHY add under MLD), `iwx_config_umac_scan` (bcast_sta_id by SCAN_CFG ver), `iwx_post_alive` (rate_n_flags version).
- **Scan**: `iwx_umac_scan_v17`, `iwx_umac_scan_fill_channels_v5`, `iwx_scan_umac_fill_general_p_v11`; dispatched from `iwx_umac_scan`.
- **Association**: block "MTL: MLD firmware API" before `iwx_umac_scan_fill_channels` (PHY/RLC/MAC/LINK/STA/SCD/BAID/SEC_KEY) and `iwx_mld_auth` / `iwx_mld_deauth` / `iwx_mld_session_protection` before `iwx_auth`. Hooks in `iwx_phy_ctxt_cmd`, `iwx_phy_ctxt_update`, `iwx_binding_cmd`, `iwx_mac_ctxt_cmd`, `iwx_add_sta_cmd`, `iwx_rm_sta_cmd`, `iwx_rm_sta`, `iwx_drain_sta`, `iwx_enable_txq`, `iwx_tvqm_enable_txq`, `iwx_enable_mgmt_queue`, `iwx_sta_rx_agg`, `iwx_set_key`, `iwx_delete_key`, `iwx_clear_statistics`, `iwx_run`, `iwx_ba_task`, `iwx_ampdu_tx_stop`.
- **Data**: `iwx_tx_fill_cmd` (HIGH_PRI EAPOL). TX cmd v10 / TX resp v7 / RX desc v3 needed no change.

Every MLD-specific path is gated on `sc_use_mld_api`, a firmware capability, or an advertised command version; other cards keep their old code paths (exception: none intended; review the diff `git diff 0134ec2 -- itlwm/hal_iwx` if in doubt).

### Expected boot log (grep `MTL:`)

```
MTL: firmware iwlwifi-ma-b0-gf-a0-89.ucode version 89.2987248927.0, 218 cmd versions, ... MLD capa 1, fragmented PNVM capa 1, IML 13944 bytes
MTL: ALIVE v6 status 0xcafe (ok) ... sku_id 0x000?10d1/0x0000000?/0x00000000
MTL: PNVM SKU block ... matches ALIVE sku_id
MTL: PNVM loaded (fragmented): ... 2 segments ...
MTL: PNVM doorbell done, complete=1 err=0
MTL: INIT_COMPLETE received (err 0)
MTL: MLD API ON (capa 1, mac type 0x44)
MTL: init TX_ANT_CONFIGURATION ... -> 0   (BT_CONFIG, SOC, DQA, TEMP, POWER, MCC, SCAN_CFG, BEACON_FILTERING follow)
MTL: SCAN_REQ_UMAC v17 (scan, ...) -> 0
MTL: SCAN_COMPLETE_UMAC status ... N nodes
MTL: auth (MLD) ... / PHY_CONTEXT v4 ... -> 0 / RLC_CONFIG v2 ... / MAC_CONFIG v2 add ... / LINK_CONFIG ... / STA_CONFIG v99 (... bytes) ... / SCD_QUEUE_CONFIG v3 ... fw assigned qid 1 / SYSTEM_STATISTICS v1 clear / SESSION_PROTECTION v2 add
```
(STA_CONFIG is not in CMD_VERSIONS, so it logs version 99 = unknown and uses the v1 struct size, exactly like OpenBSD.)

### Remaining gaps / not ported

1. `iwx_rs_update` legacy (non-HT) branch decodes a v1 PLCP from a v2 rate; only affects `ni_txrate` bookkeeping/logging.
2. MISSED_BEACONS_NOTIF (MAC_CONF 0xf6) is routed to the legacy `iwx_rx_bmiss` struct like OpenBSD; the -89 image does not list that notif, so the legacy 0xa2 one is expected.
3. No 6 GHz in the v17 channel list (OpenBSD does not do it either): channels are tagged 2.4 or 5 GHz only.
4. IGTK/BIP stays in software (itlwm never offloaded it); SEC_KEY is used for CCMP pairwise + group keys only.
5. uAPSD fields of STA_CONFIG are not set (itl80211 has no uAPSD).
6. Monitor mode under MLD is untested (same code path as OpenBSD).
8. **HE (11ax) under MLD**: OpenBSD has no 11ax. We set `MAC_CONFIG.he_support` for HE nodes (itlwm's legacy path sets `IWX_MAC_FILTER_IN_11AX`), but LINK_CONFIG HE params (`IWX_LINK_CONTEXT_MODIFY_HE_PARAMS`: bss_color, trig-based EDCA, pkt_ext) and STA_CONFIG `pkt_ext`/`htc_flags` are not filled. If HE associations misbehave while 11ac works, either port Linux `iwl_mvm_mld_link_changed()` HE parts or disable HE under MLD.
7. Key installation failure: OpenBSD deauths on SEC_KEY failure; itlwm sends keys async so a failure only shows up as a `MTL: SEC_KEY ... -> err` log / fw error.

### Risks for first hardware boot (most likely first)

1. **PNVM**: if the firmware does not accept the fragmented PNVM, `PNVM doorbell done, complete=0 err=35` (2 s timeout) and init aborts. Look at the SKU log line; if no SKU matched, the firmware proceeds without PNVM (regulatory/channels may be limited). Fallback experiment: clear CAPA 32 handling (use the concatenated path) to compare.
2. **Lost wakeups**: itlwm sleeps on `msleep` without holding the flag lock. On MA the ALIVE, PNVM and INIT_COMPLETE waits now check the flag before sleeping (log line `MTL: firmware load: alive_intr=...`); a wakeup between check and sleep can still cost one timeout.
3. **Firmware assert (SYSASSERT) on a malformed command** -> `iwx_nic_error` dump in the log, then the init task restarts the device in a loop. Most likely candidates: PHY_CONTEXT v4 layout (we send the UHB channel-info layout because CAPA 48 is set), STA_CONFIG v1 size, LINK_CONFIG fields. The fw error dump's `cmd 0x...` / last host command tells which.
4. **SCD queue id mismatch**: we require the firmware to assign qid 1 to the mgmt queue (OpenBSD does the same). If it returns another id, `iwx_enable_txq` fails with EIO and auth fails (log line `fw assigned qid X`).
5. **SYSTEM_STATISTICS clear** waits up to 1 s for END_NOTIF; on timeout auth fails (OpenBSD behaviour). If this shows up, make it non-fatal.
6. Panics: `panic()` only on unsupported opmode (unchanged). No new KASSERTs. Ring arrays: agg queues use `sc_tid_data[].qid` < `nitems(sc->txq)` checks.

### Hardware test checklist

1. Boot with the newest `mtl-<sha>` pre-release kext (see procedure below); collect `dmesg` / `log show` as described.
2. `grep MTL:` — check in order: firmware line, ALIVE v6 ok, PNVM SKU match + complete=1, INIT_COMPLETE, MLD API ON, every `init ... -> 0`.
3. With HeliPort: scan list shows networks (`SCAN_COMPLETE_UMAC` and node count > 0).
4. Join an open or WPA2-PSK 2.4/5 GHz network: `auth (MLD)`, all MLD commands `-> 0`, `SEC_KEY add ... -> 0` twice (pairwise + group), link up.
5. Data: ping the gateway, run a download (checks TX agg `SCD_QUEUE_CONFIG (tvqm)` and RX BA `RX_BAID_ALLOCATION ... add`).
6. Disconnect / reconnect / sleep-wake: `deauth (MLD)` sequence with `SCD_QUEUE_CONFIG v3 remove`, `STA_REMOVE`, `LINK_CONFIG remove`, `MAC_CONFIG remove`, PHY remove all `-> 0`.
7. On any firmware error: save the full log; the lines after `firmware error` / `nic_error` (error table) identify the failing command.

## Hardware test 1 result (2026-11-04, kext e7703c6)

- OpenCore injected the MTL kext (`Prelinked injection itlwm.kext v2.4.0 - Success`; binary contains the -89 firmware and all `MTL:` strings).
- `CNVW@14,3` (8086:7E40 sub 0094 rev 0x20) stayed busy ~9.9 s, then itlwm detached: no `ItlIwx`/`itlwm` instance, no network interface. Same picture as stock 2.3.0 on 2026-11-02 (8.4 s). Points at a firmware-load timeout (ALIVE / PNVM / INIT_COMPLETE) but **which step is unknown**:
  - `XYLog` used `kprintf`, which never reaches the unified log, and `sudo dmesg` (128 KB msgbuf) was already overwritten by NVRM output 23 min after boot.
- Fix in this commit: `XYLog` -> `IOLog` (msgbuf + unified log). Retest with the new `mtl-<sha>` build and read
  `log show --last boot --style compact --predicate 'eventMessage CONTAINS "itlwm:"'` (no sudo needed). boot-args also get `msgbuf=1048576`.

## Hardware test 2 result (2026-11-04, kext aaa77a3, IOLog)

- Same failure: CNVW busy 14.9 s, no itlwm instance. **Still no driver log**: on this machine kext `IOLog` output does not
  reach the unified log at all (ASUSATKD / CS35L56Amp lines are missing too, while their ioreg state is fine), the
  `msgbuf=1048576` boot-arg is ignored (`kern.msgbuf` stays 131072) and NVRM fills the 128 KB msgbuf in ~8 min.
- Fix: `itl80211/linux/mtl_ringlog.h` — every `XYLog` line also goes to a 64 KB ring buffer, published as OSData
  property **`MTL-Log`** on the PCI nub (CNVW) from `releaseAll()` (= any start failure), at the end of a successful
  `start()` and every ~30 s from the watchdog. The PCI nub survives a failed start. Read without sudo:
  `ioreg -a -r -n CNVW@14,3 -k MTL-Log | plutil -extract 0.MTL-Log raw - | base64 -D`
  (all `.c` files are compiled as C++ via project-level `GCC_INPUT_FILETYPE`, so the C++ header is safe).

## Hardware test 3 result (2026-11-04, kext a2d1223)

- `MTL-Log` was published, but only with itlwm.cpp lines (25x `free`, `attach fail`, `releaseAll`): the ring was a
  function-local static in a `static inline` function, i.e. one private copy per .cpp; with `-fapple-kext` there are no
  weak definitions, so plain `inline` does not merge them either (checked with llvm-nm: local `b` symbol per object).
- Fix: `mtl_ring_t g_mtl_ring` defined once in `ItlIwx.cpp`, `extern` in the header (verified: itlwm.o `U _g_mtl_ring`,
  ItlIwx.o `S _g_mtl_ring`). `attach fail` itself comes from `iwx_attach()` returning false ~15 s after start.

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

## Next after itlwm works (owner decision 2026-11-02)

Native Wi-Fi menu (AirportItlwm) is deferred until itlwm.kext + HeliPort works on the hardware without issues. Then: port the `itlwm/hal_iwx` MTL changes onto laobamac/itlwm (`AirportItlwm-Sequoia` target, macOS 15.2+, tested on 15.8 24H23) in a separate branch of thebcd69/itlwm, build in our CI, and switch config.plist from itlwm to AirportItlwm (never both enabled). Known limits of that fork: no WPA3-only/AWDL/MLO, HE opt-in via `itlwm_he=1`.
