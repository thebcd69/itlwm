# STATUS (cs35l56-speakers)

Son güncelleme: 2026-11-02 (oturum 1)

## Bitti
- DSDT çözüldü, amfi/SPI/GPIO haritası: docs/HARDWARE.md
- Linux analizi + fizibilite v1: docs/FEASIBILITY.md
- Taşınabilir çekirdek: src/Cs35l56.{h,cpp} (probe, fw_load, play/pause, volume, failSafe),
  src/CsDsp.{h,cpp} (wmfw/bin yükleyici). tests/host_test.cpp gerçek firmware ile PASS
  (`make -C tests run FW=<fw dizini>`).
- HALO packed bellek düzeni doğrulandı: 4x24-bit LE / 96 bit / 3 BE kayıt.

## Devam ediyor
- src/LpssSpi (PIO SPI), src/MtlGpio (pad MMIO), src/CS35L56Amp (IOService), firmware gömme, CI.

## Sıradaki adım
- kext parçalarını yaz, MacKernelSDK ile -fsyntax-only, CI workflow, sonra docs (TEST, capture script).

## Riskler
- Codec ALC285 I2S yolu (layout 21 vs 33) donanımda doğrulanmalı.
