#!/bin/bash
# Lay firmware vendor cua chinh may (GPU/audio) vao fw-* cho HakiOS.
# Chay khi dien thoai o Android hoac recovery co /vendor mount.
#
#   devices/picasso/pull-firmware.sh [adb-serial]
#
# Driver GPU cua HakiOS doc fw-gpu/ voi ten a660_* (ban redwood); Adreno 620 cua picasso
# dung bo a620_* -- script nay chep a620_* sang ca hai ten de thu.
set -u
SER=${1:-}
ADB="adb"
[ -n "$SER" ] && ADB="adb -s $SER"
OUT=$(cd "$(dirname "$0")" && pwd)
$ADB get-state >/dev/null 2>&1 || { echo "pull-firmware: khong thay adb"; exit 1; }

mkdir -p "$OUT/fw-gpu"
copy() { # copy <src> <dst-name>
	if $ADB shell "test -f $1" 2>/dev/null; then
		$ADB exec-out "cat $1 2>/dev/null" > "$OUT/fw-gpu/$2"
		echo "  fw-gpu/$2  $(stat -c %s "$OUT/fw-gpu/$2") B  (tu $1)"
	else
		echo "  thieu $1"
	fi
}

echo "== GPU (Adreno 620)"
copy /vendor/firmware/a620_zap.mdt       a620_zap.mdt
copy /vendor/firmware/a620_zap.mbn       a620_zap.mbn
copy /vendor/firmware/a620_gmu.bin       a620_gmu.bin
copy /vendor/firmware/a620_sqe.fw        a620_sqe.fw
# driver doi ten a660_*: chep y nguyen
for f in a620_zap.mdt a620_zap.mbn a620_gmu.bin a620_sqe.fw; do
	[ -s "$OUT/fw-gpu/$f" ] && cp "$OUT/fw-gpu/$f" "$OUT/fw-gpu/${f/a620_/a660_}"
done
ls -l "$OUT/fw-gpu" | tail -8
