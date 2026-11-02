# GU605MY dahili hoparlör (2x CS35L56) — macOS fizibilite raporu

Durum: v2 (2026-11-02). Kaynaklar: torvalds/linux master (2026-11-02), CirrusLogic/linux-firmware (GitHub aynası),
makinenin DSDT.aml'i, AppleALC master. Donanım haritası: [HARDWARE.md](HARDWARE.md).

## Özet karar
**Uygulanabilir, orta-yüksek güven (~%65).** Amfi tarafı (SPI master, GPIO, CS35L56 açılışı, DSP firmware
indirme) Linux kaynağından birebir taşındı, derleniyor (CI yeşil) ve firmware ayrıştırması gerçek GU605
dosyalarıyla host üzerinde doğrulandı (fw id 0x1a00d6 v3.11.16, 41 algoritma, 0xbedead sonlandırıcı, 42/42
.bin bloğu eşleşti). Codec tarafı için güçlü bir emsal var: CirrusAudioFixup, ALC285 + SPI CS35L41 kullanan
ASUS ROG Strix G16 G614JI'de **AppleALC layout 21 @ 48 kHz** ile hoparlör sesi bildiriyor. Kalan belirsizlikler
donanımda ölçülecek: (1) LPSS SPI saat bölücüsünün BIOS ön değeri, (2) ALC285 I2S beslemesinin layout 21'de
aktif olması (değilse layout 33), (3) amfilerin saat yokken sürekli PLAY'de kararlı kalması.

## 1. Linux'ta zincir uçtan uca
1. **ACPI**: `\_SB.PC00.SPI0.SPK1` (CSC3556) iki SpiSerialBusV2 kaynağı → serial-multi-instantiate iki
   `cs35l56-hda` SPI cihazı açar (CS0 = AMP1, CS1 = AMP2; `cirrus,dev-index` {0,1}).
2. **SPI denetleyici**: 8086:7E27 → `intel-lpss-pci` (tgl_spi_info: LPSS_CNL_SSP, 100 MHz) + `spi-pxa2xx`.
   CS0 donanım CS'i (LPSS CS_CONTROL SW modu), CS1 `cs-gpios` → GPP_E_2 GPIO. 4 MHz, mode 0, 8 bit.
3. **Regmap SPI biçimi** (cs35l56-shared.c `cs35l56_regmap_spi`): 32-bit BE adres, 16-bit dolgu, 32-bit BE değer;
   okumada adresin MSB baytına 0x80.
4. **Reset**: `reset-gpios` GPP_D_1 (iki amfi ortak): ilk amfi 0 → ≥1.1 ms → 1, sonra 2.2 ms bekleme.
   İkinci amfi "Reset GPIO busy, assume shared reset" der ve wake event (2 sahte okuma) kullanır.
5. **cs35l56_hw_init**: REVID (B0/B2 → HALO_STATE adresi değişir), HALO_STATE ≥ 2 bekle, DEVID=0x35A56,
   DSP_RESTRICT_STS1 (secured), OTPID, PROTECTION_STATUS (FIRMWARE_MISSING), IRQ maskeleri, silicon UID.
6. **Soft reset (SPI)**: MBOX_1 ← SYSTEM_RESET, 4.7 ms, MBOX_1_RAW ← PING, 4.7 ms, MBOX_2 ≠ 0 bekle.
7. **Patch + DAI**: `cs35l56_patch[]`, `cs35l56_patch_fw[]` (mute=0, volume=0 dB, posture=0),
   PREVENT_AUTO_HIBERNATE, `cs35l56_hda_dai_config[]` (I2S, 32-bit slot, 24-bit veri, BCLK id 0x21 = 3.072 MHz,
   yani 48 kHz x 2 x 32).
8. **Firmware (cs35l56_hda_fw_load)**: ROM firmware tek başına HDA'da **ses vermez** (Linux belgesi:
   "There will not be any audio from the amplifiers without firmware"). FIRMWARE_MISSING=1 ise:
   SHUTDOWN → wmfw indir → XM başlığından algoritma tablosu → .bin katsayıları → SYSTEM_RESET → boot →
   PREVENT_AUTO_HIBERNATE → regcache sync → FIRMWARE_MISSING temizle → (kalibrasyon) → AUDIO_REINIT.
   Dosyalar (system_name = `_SUB` + `-spkid<GPP_B_8>`, amp_name = AMP1/AMP2):
   - `cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid{0,1}.wmfw` → `cs35l56/CS35L56_Rev3.11.16.wmfw` (link)
   - `cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid{0,1}-amp{1,2}.bin` (4 dosya, tuning)
9. **Kalibrasyon**: EFI değişkeni (cs-amp-lib, `CirrusSmartAmpCalibrationData`), silicon UID ile eşleşir.
   Yoksa firmware varsayılan kalibrasyonla çalışır (Linux "No calibration" uyarısı, ses var).
10. **Codec (ALC285, quirk 0x1043:0x1c63 "ASUS GU605M")**: `ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1`
    → NID 0x17 (Bass Speaker) bağlantı listesi yalnızca DAC 0x02'ye zorlanır (0x14 ile aynı DAC; master volume
    tüm hoparlörleri kontrol etsin diye), zincirde `..._GU605_SPI_2_HEADSET_MIC` (0x19=0x03a11050,
    0x1b=0x03a11c30). Amfi bileşenleri artık `find_cirrus_companion_amps()` ile ACPI'den otomatik bulunuyor.
11. **Oynatma kancası**: PCM prepare → AUDIO_PLAY, PS0 bekle, ASP1_ENABLES1 RX1|RX2 (+TXn); cleanup → PAUSE.
    Ses seviyesi codec DAC'ında; amfi USER_VOLUME 0 dB.

## 2. macOS'ta eksik olan
| Parça | macOS durumu | Çözüm |
|---|---|---|
| CS35L56 sürücüsü | yok | **CS35L56Amp.kext** (bu repo) — Linux akışının birebir portu |
| HALO firmware yükleyici (cs_dsp) | yok | `CsDsp.cpp`: wmfw/bin ayrıştırıcı (ao_ops alt kümesi), host testli |
| MTL LPSS SPI master | yok (Apple'ın `AppleIntelLpssSpiController`'ı SKL/KBL MacBook'lar için, özel IOSPIFamily API'li, MTL LPSS kaydedici düzeni ve ACPI _DSM beklentileri farklı; ID spoof önerilmez) | kext içinde PXA2xx/LPSS SSP **PIO** sürücüsü (spi-pxa2xx + intel-lpss'ten) |
| GPIO (reset, CS1, spk-id) | VoodooGPIO yalnız I2C HID kesmeleri için; çıkış API'si yok | MTL-P pad DW0'a doğrudan MMIO (GPI0 `_CRS` bellek pencereleri, PAD_OWN/LOCK kontrolü) |
| Kalibrasyon (EFI var) | — | şimdilik yok (opsiyonel) |
| Oynatma kancası | AppleHDA'da yok | MVP: amfiler sürekli PLAY (PS0) durumunda; dönemsel durum izleme |
| Codec yönlendirme | AppleALC layout 21: hoparlör = 0x14←DAC 0x02 | Linux'a göre amfi beslemesi 0x17 yolunda olabilir → **layout 33** (0x14←0x02 + 0x17←0x03) denenmeli |

## 3. AppleALC ne yapabilir
- Pin config/verb ve PathMap; layout 33 (ROG Zephyrus G14, ALC285) hem 0x14 hem 0x17'yi hoparlör yolu yapar.
- Linux'taki "0x17 → DAC 0x02" birebir eşdeğeri için AppleALC'ye yeni layout (PathMap: 0x14←0x02 ve 0x17←0x02)
  gerekir → AppleALC'nin yeniden derlenmesi gerekir; ilk denemede gerekmiyor. Ayrıntı: [../applealc/README.md](../applealc/README.md).

## 4. Minimum uygulanabilir yol (bu repoda uygulanan)
1. SPI0 PCI cihazına bağlan (8086:7E27), BAR0 eşle, LPSS reset/clock/CS ayarı, PIO aktarım.
2. GPI0 (INTC1083) MMIO pencereleri: GPP_D_1 reset darbesi, GPP_E_2 CS1, GPP_B_8 spk-id okuma.
3. Her amfi: probe → firmware indir (gömülü linux-firmware dosyaları, spkid'e göre) → AUDIO_REINIT →
   güvenli ses seviyesi (varsayılan -6 dB amfi dijital kazancı, 0 dB üstü asla) → AUDIO_PLAY.
4. Hata olursa: mute + ASP kapalı + PAUSE, ardından ortak reset hattı LOW (iki amfi de reset'te).
5. Codec: önce mevcut layout 21 ile, ses yoksa layout 33 ile dene. Örnekleme hızı 48 kHz olmalı.

## 5. Tam yol (sonra)
EFI kalibrasyonu (silicon UID eşleşmesi + CAL_R/CAL_AMBIENT/CAL_STATUS/CAL_CHECKSUM kontrol yazımı),
AppleHDA ses akışına bağlı PLAY/PAUSE (IOAudioEngine durum bildirimi), uyku/uyanma yeniden init,
özel AppleALC layout'u (0x17→DAC 0x02).

## 6. Emsal çalışmalar
- **CirrusAudioFixup** (github.com/hoaug-tran/CirrusAudioFixup): Lilu tabanlı, CS35L41 B2 (I2C + SPI).
  "Working": Legion 7 16ACHg6, Legion 5 Pro (I2C); HP ZBook Fury 16 G9, **ASUS ROG Strix G16 G614JI (SPI,
  layout 21, 48 kHz)**. Firmware kext'e gömülü; ASP: 3.072 MHz BCLK, 32-bit slot, 24-bit veri; 44.1 kHz
  donanım akışı kabul edilmiyor. CS35L56 desteklemiyor. Mimari bu repo ile aynı (codec yolu AppleHDA/AppleALC,
  amfi yönetimi kext).
- InsanelyMac "Help wanted: developing kext for Cirrus Logic CS35L41 amplifier (I2C) on Legion 7" (konu 362242):
  ham I2C register tekrarı makineyi dondurdu; dersler: sürücü mantığını taşı, GPIO reset şart, ses I2S'ten gelir.
- Linux belgesi Documentation/sound/codecs/cs35l56.rst: HDA'da firmware olmadan ses yok; "patched=0" normal;
  dosyalar dizüstüne özgü.
- Bilinen bir "VoodooSPI" / MTL LPSS SPI macOS sürücüsü bulunamadı; Apple'ın LPSS SPI sürücüsü yeniden
  kullanılmıyor (özel API, farklı platform).

## 7. Riskler
- Hoparlör hasarı: firmware'in koruma algoritmaları (tuning .bin) yüklenmeden PLAY yapılmıyor; ses seviyesi
  ≤ 0 dB ile sınırlı, boost ayarlarına dokunulmuyor (yalnızca Linux'un yazdığı kayıtlar).
- SPI yanlış CS: CS1 GPIO'su yanlış pad'e giderse yanlış cihaza yazılabilir — pad numaraları DSDT + Linux
  pinctrl tablosu + BIOS GPCM tablosu ile üçlü doğrulandı; ayrıca DEVID 0x35A56 okunmadan hiçbir yazma yapılmıyor.
- Codec I2S yolu macOS'ta aktif olmayabilir (yukarıda). Tanı: `-cs35l56codecdump`.
- Amfiler AppleHDA akışından bağımsız sürekli PLAY'de; saat yokken firmware'in davranışı belirsiz
  (Linux PLAY'i akış başlamadan hemen önce verir, yani kısa süreli saatsiz PLAY'i tolere ediyor). İzleme
  döngüsü PS≠0 olursa en fazla 5 kez PLAY tekrarlar.
- Uyku: amfiler uykuda reset'e alınır, uyanışta firmware yeniden indirilir (~1 sn).

## 8. Kaynaktan çözülemeyen veri (son çare listesi)
| Veri | Neden kaynakta yok | En küçük yakalama |
|---|---|---|
| LPSS SPI `CLOCK_PARAMS` (BAR+0x200) M/N ön değeri | BIOS'a bağlı | macOS log'u zaten basıyor (`SPI: CAPS=... CLOCK_PARAMS`) — Linux gerekmez |
| ALC285 pin varsayılanları 0x14/0x17, bağlantılar | kernel yalnızca 0x19/0x1b'yi düzeltiyor | `-cs35l56codecdump` (macOS'ta) — Linux gerekmez |
| Calibration EFI değişkeni | makineye özgü | yalnızca tam yol için; live USB script'i okur |
| Linux'ta çalışan amfinin register durumu | çalışma anı | live USB script'i (regmap dump) — yalnızca macOS'ta amfi açılıp ses yoksa |

