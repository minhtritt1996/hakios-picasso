#!/bin/bash
# Pull the phone's own firmware out of its Android into the fw-*/ directories at the top of the
# repository (never committed: it is the vendor's). The phone is on adb, in Android.
set -e
cd "$(dirname "$0")/../.."
pull() { # DIR SOURCE...  (missing files are told, not fatal)
	local dir=$1; shift
	mkdir -p "$dir"
	for f in "$@"; do adb pull "$f" "$dir/" >/dev/null 2>&1 || echo "pull-firmware.sh: $f: not on the phone"; done
	echo "pull-firmware.sh: $dir: $(ls -1 "$dir" | wc -l | tr -d ' ') files"
}
adb get-state >/dev/null
pull fw-gpu /vendor/firmware/a650_gmu.bin /vendor/firmware/a630_sqe.fw /vendor/firmware/a620_zap.mdt /vendor/firmware/a620_zap.b00 /vendor/firmware/a620_zap.b01 /vendor/firmware/a620_zap.b02 /vendor/firmware/a620_zap.elf
pull fw-wifi /vendor/firmware_mnt/image/wlanmdsp.mbn /vendor/firmware_mnt/image/bdwlan.bin /vendor/firmware_mnt/image/bdwlan.b0a
pull fw-audio /vendor/firmware_mnt/image/adsp.mdt $(for i in $(seq -w 0 32); do echo /vendor/firmware_mnt/image/adsp.b$i; done)
pull fw-modem /vendor/firmware_mnt/image/modem.mdt $(for i in $(seq -w 0 28); do echo /vendor/firmware_mnt/image/modem.b$i; done)
mkdir -p fw-modem/image && adb pull /vendor/firmware_mnt/image fw-modem/ >/dev/null 2>&1 && echo "pull-firmware.sh: fw-modem/image: the modem's own files (TFTP)"
echo "pull-firmware.sh: done; Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (the network's name and passphrase)"
