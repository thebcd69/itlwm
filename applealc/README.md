# Codec tarafı (ALC285, 10EC0285, SSID 10431C63) — AppleALC seçenekleri

**config.plist'e bu repo dokunmaz.** Aşağıdakiler denenecek değişikliklerdir.

## Linux ne yapıyor (sound/hda/codecs/realtek/alc269.c)
```
SND_PCI_QUIRK(0x1043, 0x1c63, "ASUS GU605M", ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1)
ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 = alc285_fixup_speaker2_to_dac1
    -> snd_hda_override_conn_list(codec, 0x17, 1, {0x02})     // Bass Speaker (0x17) DAC1'e (0x14 ile ortak)
  chain ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC
    -> pins { 0x19, 0x03a11050 }, { 0x1b, 0x03a11c30 }       // kulaklık mikrofonu
```
Amfiler `find_cirrus_companion_amps()` ile (ACPI CSC3556) bulunur; codec'e amfiler için ek verb gönderilmez.
Benzer ASUS CS35L56 tasarımında (GA403W, lkml 2601.1/06212) Windows pin dökümü: 0x14 = 0x90170510,
0x17 = 0x90170120 (speaker). Yani iki dahili hoparlör pin'i var; amfi I2S beslemesinin 0x17 yolunun
DAC'ından geldiği (o yüzden 0x17'yi volume'lu DAC 0x02'ye bağlıyorlar) en olası yorum.

## AppleALC ALC285 layout'ları (Resources/ALC285/Platforms*.xml)
| layout | PathMap (çıkış) | not |
|---|---|---|
| 21 (şu an) | Speaker: 0x14←0x02; HP: 0x21←0x03 | 0x17 hiç sürülmüyor |
| **33** (ROG Zephyrus G14) | Speaker: 0x14←0x02 **+ 0x17←0x03**; HP: 0x21←0x03 | 0x17 sürülüyor (DAC 0x03, HP ile ortak) |
| 71 | Speaker 0x14 + 0x17 tanımlı (ConfigData) | yedek aday |

Layout 33 ConfigData: 0x12=0x90a60170, 0x14=0x90170140 (EAPD), 0x17=0x90170141, 0x19=0x028b1020,
0x21=0x022b1050 (+boş pinler 0x400000F0).

## Deneme sırası
1. `alcid=33` boot-arg'ı (config.plist DeviceProperties'e dokunmadan). Kulaklık çalışmaya devam etmeli.
2. Kalıcı olursa: DeviceProperties > `PciRoot(0x0)/Pci(0x1f,0x3)` > `layout-id` = `<21000000>` (Data).
3. Hiçbiri olmazsa: özel layout (AppleALC kaynağından derleme gerekir) — PathMap:
   `Speaker: 0x14←0x02, 0x17←0x02` (Linux'un birebir eşi), HP `0x21←0x03`, Mic `0x12`, HeadsetMic `0x19`
   (ConfigData 0x19=0x03a11050, 0x1b=0x03a11c30 Linux'tan). AppleALC'ye PR olarak gönderilebilir;
   şimdilik yalnızca tasarım (bkz. `layout-gu605-proposal.md`).

## Tanı
Kext `-cs35l56codecdump` boot-arg'ı ile (salt okunur HDA verb'leri) codec düğümlerini log'a döker:
pin config default, bağlantı listesi, seçili bağlantı, pin control, EAPD. Bu, Linux'a gerek kalmadan
0x14/0x17'nin BIOS varsayılanlarını ve AppleHDA'nın hangi DAC'ı seçtiğini gösterir.
