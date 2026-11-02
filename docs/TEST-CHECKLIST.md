# CS35L56Amp — donanım test listesi (GU605MY, macOS Sequoia 15.8.1)

> Kural: hiçbir dosyayı **silme**, devre dışı bırakmak için **yeniden adlandır** (ör. `CS35L56Amp.kext.off`).
> Windows SSD'sine, boot sırasına, NVRAM boot girdilerine dokunulmuyor. Güç limitleri değişmiyor.

## 0. Hazırlık
1. GitHub → Releases → en yeni `cs35l56-<sha>` prerelease → `CS35L56Amp-<sha>.zip` indir (veya Actions artifact).
2. Mevcut EFI'nin yedeğini al: OPENCORE USB/ESP'deki `EFI` klasörünü `EFI.yedek-cs35l56` olarak kopyala.
3. Zip'ten çıkan `CS35L56Amp.kext`'i `EFI/OC/Kexts/` içine koy.

## 1. config.plist — Kernel > Add (en sona, VoodooGPIO'dan SONRA ekle)
```xml
<dict>
    <key>Arch</key><string>x86_64</string>
    <key>BundlePath</key><string>CS35L56Amp.kext</string>
    <key>Comment</key><string>GU605M CS35L56 speaker amps (SPI)</string>
    <key>Enabled</key><true/>
    <key>ExecutablePath</key><string>Contents/MacOS/CS35L56Amp</string>
    <key>MaxKernel</key><string></string>
    <key>MinKernel</key><string>20.0.0</string>
    <key>PlistPath</key><string>Contents/Info.plist</string>
</dict>
```
Kext Lilu'ya bağlı değil; sırası önemli değil ama VoodooGPIO'dan sonra olması tavsiye edilir.

## 2. İlk açılış: SESSİZ deneme (önerilen)
NVRAM > Add > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > `boot-args` sonuna ekle:
`-cs35l56noplay -cs35l56dbg`
Bu modda amfiler bulunur, firmware yüklenir ama **PLAY yapılmaz, mute kalır**. Hoparlörden ses çıkmaz.

Açılıştan sonra Terminal:
```sh
log show --last boot --predicate 'eventMessage CONTAINS "CS35L56"' --style compact > ~/Desktop/cs35l56-log.txt
ioreg -l -w0 | grep -E '"CS35L56-' > ~/Desktop/cs35l56-ioreg.txt
```
Beklenen satırlar (sırayla):
| Satır | Anlamı |
|---|---|
| `CS35L56Amp <sha> starting on SPI0, subsystem 1c631043` | kext SPI0'a bağlandı |
| `GPIO: found INTC1083 ...`, `com1 PADBAR=0x600 com5 PADBAR=0x600` | GPIO MMIO doğru |
| `GPIO: GPP_D_1 ... own=host ... lock=0 locktx=0` | reset pad'i bizim kontrolümüzde |
| `SPI: ... ssp_clk=100000000 Hz`, `SPI: SCLK 4000000 Hz` | SPI saat doğru |
| `spk-id (GPP_B_8) = 0` veya `1` | hoparlör varyantı |
| `AMP1: REVID raw 0xb0 -> rev B0` | **SPI çalışıyor** (ilk gerçek okuma) |
| `AMP1: Cirrus Logic CS35L56 Rev B0 OTP.. fw:3.11.. (patched=0)` | amfi ROM firmware'de |
| `AMP1: wmfw: 70 regions, 15752 bytes downloaded` | wmfw indirildi |
| `AMP1: firmware: 0x1a00d6 vendor: 0x2 v3.11.16, 41 algorithms` | DSP doğru firmware'i görüyor |
| `AMP1: bin: 42 blocks, ... 0 errors` | tuning yüklendi |
| `AMP1: running fw 3.11.16, FIRMWARE_MISSING=0` | patch tamam |
| aynısı AMP2 için | ikinci amfi (CS1 = GPIO) çalışıyor |
| `-cs35l56noplay: amps patched but left paused+muted` | sessiz test bitti |
| `bring-up OK in ... ms` | |

Hata satırları:
| Satır | Anlamı / yapılacak |
|---|---|
| `INTC1083 ACPI device not found` / `unexpected PADBAR` | GPIO bulunamadı; hiçbir şeye yazılmadı. Log'u gönder. |
| `GPP_.. not owned by host` / `locked` | BIOS pad'i kilitlemiş; `-cs35l56noreset` ile tekrar dene |
| `REVID raw 0xffffffff` veya `0x0` | SPI veri yok: saat/CS sorunu → `-cs35l56clkfix` ekleyip tekrar dene |
| `unexpected device ... refusing` | yanlış cihaz/CS: dur, log'u gönder |
| `firmware boot timed out` | amfi reset'ten çıkmadı |
| `FAIL-SAFE: ... -> muting amps`, `amps held in reset` | güvenli kapatıldı, ses yok — log'u gönder |

## 3. İkinci açılış: sesli deneme
1. boot-args'tan `-cs35l56noplay`'i kaldır (`-cs35l56dbg` kalsın). Varsayılan amfi kazancı **-6 dB**.
2. Açılışta macOS ses seviyesini **%25 civarına** indir. Ses > Çıkış: "Dahili Hoparlörler".
3. **Audio MIDI Setup**: Built-in Output = **48000 Hz** (amfiler 48 kHz x 32 bit I2S bekliyor).
4. Kısa bir test sesi çal (Sistem Ayarları > Ses > uyarı sesi).
5. Ses yoksa log'u al (yukarıdaki komut). `status PS=0` görünüyorsa amfiler çalışıyor demektir → sorun codec
   tarafında (bkz. adım 4).

## 4. Ses yok ama amfiler PS=0 ise: codec layout
Şu an `alc-layout-id` 21 (AppleALC ALC285 layout 21: hoparlör = NID 0x14←DAC 0x02).
Linux'a göre amfi beslemesi NID 0x17 yolunda olabilir. Denenecek: **layout 33** (0x14←0x02 + 0x17←0x03).
- config.plist > DeviceProperties > `PciRoot(0x0)/Pci(0x1f,0x3)` > `layout-id` = `21000000` (Data, 33)
  **veya** geçici olarak boot-args'a `alcid=33` ekle (config değişmeden).
- Sonra 3. adımı tekrarla. Kulaklık çıkışının hâlâ çalıştığını kontrol et.

## 5. Ses seviyesi
- `cs35l56_vol=N` → amfi dijital kazancı -N dB (0..40). Varsayılan 6. **0'ın altına (pozitif kazanç) gidilemez.**
- Normal kullanım için macOS ses tuşları codec DAC'ını kontrol eder.

## 6. Geri alma
- Hızlı: boot-args'a `-cs35l56off` ekle (kext yüklenir ama hiçbir şeye dokunmaz).
- Kalıcı: config.plist'te girdinin `Enabled`'ını `false` yap **veya** kext'i `CS35L56Amp.kext.off` olarak
  yeniden adlandır (Enabled true kalırsa OpenCore uyarı verir; önce Enabled=false yap).
- Açılmazsa: OpenCore menüsünde yedek EFI'li USB'den aç.

## 7. Uyku
Uykuda amfiler mute + reset'e alınır; uyanınca kext yeniden kurar (~1-2 sn). Uyandıktan sonra ses yoksa log'u gönder.

## 8. Geri gönderilecekler
`~/Desktop/cs35l56-log.txt`, `~/Desktop/cs35l56-ioreg.txt`, hangi boot-args/layout ile denendiği,
ses var/yok/cızırtı/tek taraf bilgisi.
