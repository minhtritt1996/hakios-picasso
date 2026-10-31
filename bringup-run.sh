#!/bin/bash
# Man quyet dinh: flash boot image co fix-up, roi dua Darwin userland 16.5 vao RAM
# va chay kernel.
#
#   hakios/bringup-run.sh [/tmp/boot-fix.img] [SECS]
#
# 1. fastboot flash boot IMG (phai o ABL fastboot: Vol-Down + Power)
# 2. reboot -> loader len (~20 s)
# 3. `oem ramdisk` + nap userland/system.apfs (2,3 GB) vao RAM  [~80 s]
# 4. `oem run` voi kernel + root.apfs (md1) + cmdline cua .config
# 5. theo doi ket qua, fetch log, in cac moc CP/H
set -u
IMG=${1:-/tmp/boot-fix.img}
SECS=${2:-90}
H=${HAKIOS:-/home/tritm/hakios-u}
cd "$H" || exit 1

fastboot devices 2>/dev/null | grep -q fastboot || { echo "bringup: khong thay fastboot (can Vol-Down + Power)"; exit 1; }
prod=$(timeout 8 fastboot getvar product 2>/dev/null | sed -n 's/^product: //p')
[ "$prod" = "picasso" ] || { echo "bringup: dang o '$prod', can ABL fastboot (Vol-Down + Power)"; exit 1; }

echo "== 1. flash $(basename "$IMG")"
timeout 180 fastboot flash boot "$IMG" 2>&1 | tail -2 || exit 1
timeout 20 fastboot reboot >/dev/null 2>&1

echo "== 2. cho loader"
for i in $(seq 1 20); do fastboot devices 2>/dev/null | grep -q xnuloader && { echo "loader up"; break; }; sleep 3; done
fastboot devices 2>/dev/null | grep -q xnuloader || { echo "bringup: loader khong len"; exit 1; }

echo "== 3. nap system.apfs ($(du -h userland/system.apfs | cut -f1)) vao RAM"
size=$(stat -c %s userland/system.apfs)
t0=$(date +%s)
timeout 600 fastboot oem ramdisk 2>&1 | grep -q OKAY || { echo "bringup: oem ramdisk tu choi"; exit 1; }
# dung dung duong cua `xl ramdisk`: oem ramdisk (arm) roi stage (upload)
if ! timeout 1800 fastboot stage userland/system.apfs 2>&1 | grep -q OKAY; then
	echo "bringup: nap system.apfs that bai"
	exit 1
fi
echo "   nap xong sau $(( $(date +%s) - t0 ))s"

echo "== 4. chay kernel voi root.apfs"
SOC=$(grep -v '^#' devices/picasso/soc.args | grep . | tr '\n' ' ')
cd /home/tritm/ios || exit 1
RAMDISK=/home/tritm/hakios-u/userland/root.apfs timeout $(( SECS + 200 )) bash hakios/xl-run.sh "$SECS" \
	"-v debug=0x8 hv_wdt_pet=1 cpus=1 $SOC" 2>&1 | tail -40
