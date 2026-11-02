#!/bin/bash
# GU605M CS35L56 ground-truth capture for an Ubuntu LIVE session ("Try Ubuntu").
# OPTIONAL / LAST RESORT: the driver is built from kernel sources + DSDT; this
# only verifies it. READ-ONLY with respect to the machine:
#  - never mounts, writes or fscks the internal disks (Windows SSD, KIOXIA macOS disk)
#  - installs nothing permanent (optional apt-get only into the live RAM overlay, -a)
#  - writes results ONLY to the OpenCore USB (FAT32 label OPENCORE) or the live
#    USB's writable area, or a path given with -o.
# Usage:  sudo bash cs35l56-capture.sh [-o OUTDIR] [-a]
#   -a  allow 'apt-get install' of alsa-utils/acpica-tools INTO THE LIVE SESSION (RAM only)
set -u
ALLOW_APT=0; OUT=""
while getopts "ao:" o; do case $o in a) ALLOW_APT=1;; o) OUT=$OPTARG;; *) exit 2;; esac; done
[ "$(id -u)" = 0 ] || { echo "sudo ile çalıştırın: sudo bash $0"; exit 1; }

# ---- output location: OPENCORE USB (already mounted by the live desktop, or mount it rw) ----
if [ -z "$OUT" ]; then
  for m in /media/*/OPENCORE /run/media/*/OPENCORE; do [ -d "$m" ] && OUT=$m && break; done
fi
if [ -z "$OUT" ]; then
  dev=$(blkid -L OPENCORE 2>/dev/null || true)
  if [ -n "$dev" ]; then
    # Only a vfat partition labelled OPENCORE is ever mounted by this script.
    if [ "$(blkid -o value -s TYPE "$dev")" = vfat ]; then
      mkdir -p /mnt/opencore-usb && mount -t vfat -o rw,uid=0,gid=0 "$dev" /mnt/opencore-usb && OUT=/mnt/opencore-usb
    fi
  fi
fi
[ -n "$OUT" ] || { OUT=$HOME; echo "OPENCORE USB bulunamadı, $OUT kullanılıyor (live oturum kapanınca silinir!)"; }
TS=$(date +%Y%m%d-%H%M%S)
D="$OUT/cs35l56-capture-$TS"
mkdir -p "$D" || exit 1
echo "Çıktı: $D"
log() { echo "== $*"; }
run() { local f=$1; shift; { echo "\$ $*"; "$@"; } > "$D/$f" 2>&1 || true; }

if [ $ALLOW_APT = 1 ]; then
  log "apt (live RAM overlay only)"
  apt-get -y install alsa-utils acpica-tools >/dev/null 2>&1 || echo "apt-get failed (no network?)"
fi
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug 2>/dev/null

log "system"
run uname.txt uname -a
run os-release.txt cat /etc/os-release
run cmdline.txt cat /proc/cmdline
run lspci.txt lspci -nnvvk
run lsmod.txt lsmod
run dmi.txt sh -c 'for f in sys_vendor product_name product_version board_name bios_version bios_date; do printf "%s: " $f; cat /sys/class/dmi/id/$f; done'

log "dmesg"
dmesg > "$D/dmesg-full.txt" 2>&1
dmesg | grep -iE "cs35l56|cs_dsp|cs-amp|cirrus|realtek|alc285|snd_hda|spi|pxa2xx|lpss|serial-multi|firmware" > "$D/dmesg-audio.txt"
run firmware-loaded.txt sh -c 'dmesg | grep -iE "wmfw|\.bin|Firmware:|Tuning|Calibration|spkid|system name"'
run firmware-files.txt sh -c 'ls -la /lib/firmware/cirrus/ 2>/dev/null | grep -i 10431c63; ls -la /lib/firmware/cirrus/cs35l56/ 2>/dev/null'

log "spi / acpi devices"
run spi-devices.txt sh -c 'for d in /sys/bus/spi/devices/*; do echo "--- $d"; ls -l $d/driver 2>/dev/null; cat $d/modalias 2>/dev/null; cat $d/firmware_node/path 2>/dev/null; done'
run spi-controllers.txt sh -c 'for d in /sys/class/spi_master/*; do echo "--- $d"; readlink -f $d; cat $d/device/firmware_node/path 2>/dev/null; done'
run acpi-csc3556.txt sh -c 'for d in /sys/bus/acpi/devices/CSC3556*; do echo "--- $d"; cat $d/path $d/status 2>/dev/null; done'
run lpss-clk.txt sh -c 'cat /sys/kernel/debug/clk/clk_summary 2>/dev/null | grep -iE "spi|lpss|pxa" '
run lpss-regs.txt sh -c 'BAR=$(lspci -s 00:1e.2 -v 2>/dev/null | awk "/Memory at/{print \$3; exit}"); echo BAR=$BAR; command -v busybox >/dev/null && for o in 0x200 0x204 0x224 0x238 0x2fc; do printf "%s " $o; busybox devmem $((0x$BAR + o)) 32; done'

log "gpio / pinctrl"
run gpio.txt cat /sys/kernel/debug/gpio
run pinctrl-handles.txt sh -c 'ls /sys/kernel/debug/pinctrl/'
for p in /sys/kernel/debug/pinctrl/INTC1083*; do
  [ -d "$p" ] || continue
  n=$(basename "$p")
  cat "$p/pins" > "$D/pinctrl-$n-pins.txt" 2>&1
  cat "$p/pinmux-pins" > "$D/pinctrl-$n-pinmux.txt" 2>&1
  grep -E "GPP_E_2|GPP_D_1|GPP_D_2|GPP_B_8" "$p/pins" > "$D/pinctrl-amp-pads.txt" 2>&1
done

log "acpi tables"
if command -v acpidump >/dev/null; then
  mkdir -p "$D/acpi" && (cd "$D/acpi" && acpidump -b >/dev/null 2>&1)
else
  mkdir -p "$D/acpi" && for t in /sys/firmware/acpi/tables/DSDT /sys/firmware/acpi/tables/SSDT*; do cp "$t" "$D/acpi/" 2>/dev/null; done
fi

log "efi calibration variable (read only)"
run efivars.txt sh -c 'ls -la /sys/firmware/efi/efivars/ | grep -iE "cirrus|02f9af02"'
for v in /sys/firmware/efi/efivars/CirrusSmartAmpCalibrationData-*; do [ -f "$v" ] && cp "$v" "$D/" ; done

dump_regmaps() {  # $1 = tag
  for r in /sys/kernel/debug/regmap/*cs35l56* /sys/kernel/debug/regmap/spi-*; do
    [ -e "$r/registers" ] || continue
    n=$(basename "$r")
    head -c 200000 "$r/registers" > "$D/regmap-$1-$n.txt" 2>&1
  done
}
dump_codec() {   # $1 = tag
  for c in /proc/asound/card*/codec#*; do
    [ -f "$c" ] && cp "$c" "$D/codec-$1-$(echo $c | tr '/#' '__').txt"
  done
  command -v amixer >/dev/null && amixer -c 0 contents > "$D/amixer-$1.txt" 2>&1
}

log "idle snapshot"
dump_regmaps idle
dump_codec idle
run asound-cards.txt cat /proc/asound/cards
run hda-hints.txt sh -c 'for f in /sys/class/sound/hwC*D*/{init_pin_configs,driver_pin_configs,user_pin_configs,vendor_id,subsystem_id,hints}; do echo "--- $f"; cat $f; done'

log "playing snapshot (5 s sine on default output)"
if command -v speaker-test >/dev/null; then
  ( speaker-test -D default -c 2 -r 48000 -t sine -f 440 -l 1 >/dev/null 2>&1 & echo $! > /tmp/st.pid ) ; sleep 2
elif command -v paplay >/dev/null && [ -f /usr/share/sounds/alsa/Front_Center.wav ]; then
  ( for i in 1 2 3; do paplay /usr/share/sounds/alsa/Front_Center.wav; done & echo $! > /tmp/st.pid ); sleep 1
else
  echo "speaker-test/paplay yok: lütfen şimdi bir ses çalın (YouTube vb.), 10 sn bekleniyor"; sleep 10
fi
dump_regmaps playing
dump_codec playing
cat /proc/asound/card*/pcm*p/sub*/hw_params > "$D/hw_params-playing.txt" 2>&1
kill "$(cat /tmp/st.pid 2>/dev/null)" 2>/dev/null; sleep 1

log "pack"
tar -C "$OUT" -czf "$D.tar.gz" "$(basename "$D")" && echo "Tamam: $D.tar.gz"
sync
echo "Bu dosyayı (ve klasörü) macOS'a/bana gönderin."
