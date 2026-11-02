#!/bin/sh
# Fetch the GU605M (SSID 10431c63) CS35L56 firmware from linux-firmware and
# verify pinned SHA-256 sums. Usage: scripts/fetch_fw.sh <outdir>
# Source: CirrusLogic/linux-firmware (GitHub mirror of linux-firmware; WHENCE
# lines 9710-9715). kernel.org/gitlab are tried as fallbacks.
set -eu
out=${1:-build/fw}
mkdir -p "$out"
BASES="https://raw.githubusercontent.com/CirrusLogic/linux-firmware/main
https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain
https://gitlab.com/kernel-firmware/linux-firmware/-/raw/main"
FILES="cirrus/cs35l56/CS35L56_Rev3.11.16.wmfw 12c6d4fe63dc6a471473182dfdb72b1a91092e62db0038c48d9676d9566eff58
cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid0-amp1.bin 5732e3bab79d239dcbc937c1b1c1ef7518d32a2eb5737c6efca5c66819d699fa
cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid0-amp2.bin 4000b8794a0995c3a8b26a0ffcb39e7f1082047cb95dd1ab6fc1ebd22a3cac86
cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid1-amp1.bin e34ae31ede3e4ecdd1fc247a49f7b3413049224af2abae2a586fa2aa8dda10a1
cirrus/cs35l56-b0-dsp1-misc-10431c63-spkid1-amp2.bin 30c8e8c6d8fa5f4020c65ad22bc42c1a8ef44fb0175370b141e5156909a3bee0
LICENSE.cirrus opt"
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
echo "$FILES" | while read -r f want; do
  dst="$out/$(basename "$f")"
  if [ -f "$dst" ] && { [ "$want" = opt ] || [ "$(sha "$dst")" = "$want" ]; }; then echo "ok (cached) $f"; continue; fi
  got=0
  for b in $BASES; do
    if curl -fsSL --retry 3 -o "$dst.tmp" "$b/$f"; then
      if [ "$want" = opt ] || [ "$(sha "$dst.tmp")" = "$want" ]; then mv "$dst.tmp" "$dst"; got=1; echo "ok $f <- $b"; break; fi
      echo "sha mismatch for $f from $b"
    fi
  done
  rm -f "$dst.tmp"
  if [ $got = 0 ] && [ "$want" = opt ]; then echo "warning: optional $f not fetched"; continue; fi
  [ $got = 1 ] || { echo "FAILED: $f"; exit 1; }
done
