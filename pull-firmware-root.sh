#!/bin/bash
# pull-firmware.sh, through root. On some ROMs /vendor/firmware_mnt is owned by system with
# restrictive permissions and /vendor/firmware is not readable by the adb shell user.
# This copies the firmware under root to a temporary staging directory in /data/local/tmp,
# pulls it from there, and removes the staging directory. Nothing on the phone is changed.
set -e
cd "$(dirname "$0")/../.."
S=/data/local/tmp/fw
adb get-state >/dev/null
adb shell "su -c 'sh -c \"
S=$S; rm -rf \$S; mkdir -p \$S/gpu \$S/wifi \$S/audio \$S/modem
cp /vendor/firmware/a650_gmu.bin /vendor/firmware/a630_sqe.fw /vendor/firmware/a620_zap.* \$S/gpu/ 2>/dev/null || true
cp /vendor/firmware_mnt/image/wlanmdsp.mbn /vendor/firmware_mnt/image/bdwlan.* \$S/wifi/ 2>/dev/null || true
cp /vendor/firmware_mnt/image/adsp.* \$S/audio/ 2>/dev/null || true
cp /vendor/firmware_mnt/image/modem.* \$S/modem/ 2>/dev/null || true
cp -r /vendor/firmware_mnt/image \$S/modem/ 2>/dev/null || true
chmod -R a+rX \$S\"'"
rm -rf fw-stage
adb pull "$S" fw-stage >/dev/null
rm -rf fw-gpu fw-wifi fw-audio fw-modem
mv fw-stage/gpu fw-gpu; mv fw-stage/wifi fw-wifi; mv fw-stage/audio fw-audio
mv fw-stage/modem fw-modem; rmdir fw-stage
adb shell "su -c 'rm -rf $S'"
for d in fw-gpu fw-wifi fw-audio fw-modem fw-modem/image; do
	[ -d "$d" ] && echo "pull-firmware-root.sh: $d: $(ls -1 "$d" | wc -l | tr -d ' ') files"
done
echo "pull-firmware-root.sh: done; Wi-Fi also wants fw-wifi/mac (the radio's address) and fw-wifi/network (the network's name and passphrase)"
