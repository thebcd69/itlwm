# (Opsiyonel, son çare) Ubuntu Live USB ile doğrulama verisi

Sürücü kernel kaynakları + DSDT + linux-firmware'den yazıldı; bu adım yalnızca **doğrulama** içindir ve
ancak macOS testinde çözülemeyen bir sorun kalırsa istenecek.

## Ne gerekir
- **Ayrı** bir USB bellek (≥ 8 GB) — OPENCORE USB'si ile karıştırma.
- En güncel Ubuntu Desktop ISO (ör. ubuntu.com/download/desktop). Yazmak için Windows'ta Rufus/balenaEtcher.

## Adımlar
1. ISO'yu ayrı USB'ye yaz.
2. Hem Ubuntu USB'sini hem OPENCORE USB'sini tak.
3. Açılışta **Esc** → boot menüsünden Ubuntu USB'sini seç (boot sırası/NVRAM değişmez).
4. "Try Ubuntu" / "Ubuntu'yu dene" seç. **Kurulum yapma.**
5. Masaüstü açılınca hoparlörden ses geldiğini kontrol et (Ayarlar > Ses > Test).
6. Terminal (Ctrl+Alt+T):
   ```sh
   sudo bash /media/ubuntu/OPENCORE/cs35l56-capture.sh
   ```
   (Script'i önceden OPENCORE USB'sinin köküne kopyala. Ağ varsa daha fazla veri için `-a` ekleyebilirsin:
   alsa-utils/acpica-tools yalnızca live oturumun RAM'ine kurulur.)
7. Script sırasında 2-10 sn sinüs sesi çalar (ses seviyesini düşük tut).
8. Sonuç: `OPENCORE/cs35l56-capture-<tarih>.tar.gz`. Kapat, USB'yi çıkar.

Script dahili diskleri (Windows SSD, KIOXIA macOS) **mount etmez, yazmaz**; yalnızca `OPENCORE` etiketli
FAT32 bölümüne (veya `-o` ile verilen yere) yazar.
