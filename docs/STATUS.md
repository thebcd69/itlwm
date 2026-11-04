# STATUS (cs35l56-speakers)

Son güncelleme: 2026-11-04 (oturum 2)

## Bitti
- DSDT çözüldü, amfi/SPI/GPIO haritası: docs/HARDWARE.md
- Fizibilite v2 (emsal: CirrusAudioFixup, layout 21 @ 48 kHz): docs/FEASIBILITY.md
- Taşınabilir çekirdek: src/Cs35l56.{h,cpp}, src/CsDsp.{h,cpp}; host test gerçek firmware ile PASS.
- kext: src/LpssSpi (PIO SPI, LPSS_CNL_SSP), src/MtlGpio (INTC1083 pad MMIO), src/CS35L56Amp (IOService,
  thread_call worker, PM sleep/wake, boot-args, opsiyonel `-cs35l56follow` PLAY/PAUSE takibi), src/HdaDump (opt-in salt okunur codec dökümü),
  gömülü firmware (scripts/fetch_fw.sh sha256 sabitli + scripts/fw2c.py).
- CI: .github/workflows/cs35l56.yml (macos-14) yeşil; prerelease `cs35l56-<sha>` + artifact.
- Belgeler: docs/TEST-CHECKLIST.md (TR), applealc/README.md, linux-capture/ (opsiyonel live USB).

## Devam ediyor
- Test 1 (2026-11-04, 75c5f43) GEÇERSİZ: XNU kext'i hiç yüklemedi —
  `Kext com.thebcd69.CS35L56Amp error - CFBundleVersion bad value '0.1.0-75c5f43'`.
  Düzeltme: CFBundleVersion yalnız sayı (0.1.0), sha yalnız CFBundleShortVersionString'de;
  Makefile artık sayısal olmayan CFBundleVersion'da derlemeyi kırıyor. Yeni derleme ile test tekrarlanacak.

## Sıradaki adım
1. Sahibi docs/TEST-CHECKLIST.md adım 2'yi (`-cs35l56noplay -cs35l56dbg`) çalıştırıp log'u göndersin.
2. Log'a göre: SPI saat (`-cs35l56clkfix`), GPIO kilidi (`-cs35l56noreset`), codec (`alcid=33`, `-cs35l56codecdump`).
3. Kod: EFI kalibrasyonu, özel AppleALC layout'u (applealc/layout-gu605-proposal.md).

## Riskler
- LPSS SPI clock M/N BIOS ön değeri bilinmiyor (log basıyor; `-cs35l56clkfix` yedek).
- Codec I2S yolu: layout 21 (emsal G614JI'de çalışıyor) / 33.
- Amfiler sürekli PLAY: saat yokken davranış belirsiz (izleme döngüsü sınırlı tekrar yapar).
- Kalibrasyon yok: firmware varsayılan kalibrasyonla çalışır (Linux'ta "No calibration" durumu ile aynı).

## Boot-args
`-cs35l56off`, `-cs35l56noplay`, `-cs35l56dbg`, `-cs35l56noreset`, `-cs35l56clkfix`, `-cs35l56codecdump`, `-cs35l56follow`,
`cs35l56_vol=<0..40 dB zayıflatma, varsayılan 6>`, `cs35l56_spkid=<0|1>`.
