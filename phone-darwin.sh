#!/bin/bash
# Dua HakiOS + Darwin userland 16.5 len picasso (ban day du).
#
#   ./phone-darwin.sh [/tmp/boot-loader-real.img] [SECS]
#
# 1. flash boot image (loader that identity that + usb)
# 2. nap userland/system.apfs (2,3 GB) vao RAM
# 3. chay kernel (da fix redzone) o core 0 voi root.apfs + ufs=2
# Can ABL fastboot: giu Volume Down + Power.
set -u
IMG=${1:-/tmp/boot-loader-real.img}
SECS=${2:-150}
H=${HAKIOS:-/home/tritm/hakios-u}
cd "$H" || exit 1

fastboot devices 2>/dev/null | grep -q fastboot || { echo "phone-darwin: khong thay fastboot (Vol-Down + Power)"; exit 1; }
prod=$(timeout 12 fastboot getvar product 2>/dev/null | sed -n 's/^product: //p')
[ "$prod" = "picasso" ] || { echo "phone-darwin: dang o '$prod', can ABL fastboot (Vol-Down + Power)"; exit 1; }

echo "== 1. flash $(basename "$IMG") ($(stat -c %s "$IMG") B)"
timeout 180 fastboot flash boot "$IMG" 2>&1 | grep -q OKAY || { echo "phone-darwin: flash that bai"; exit 1; }
timeout 20 fastboot reboot >/dev/null 2>&1
for i in $(seq 1 20); do fastboot devices 2>/dev/null | grep -q xnuloader && { echo "loader up"; break; }; sleep 3; done
fastboot devices 2>/dev/null | grep -q xnuloader || { echo "phone-darwin: loader khong len"; exit 1; }

echo "== 2. nap system.apfs ($(du -m userland/system.apfs | cut -f1) MB) vao RAM"
t0=$(date +%s)
timeout 60 fastboot oem ramdisk 2>&1 | grep -q OKAY || { echo "phone-darwin: oem ramdisk tu choi"; exit 1; }
timeout 1800 fastboot stage userland/system.apfs 2>&1 | grep -q OKAY || { echo "phone-darwin: nap that bai"; exit 1; }
echo "   xong sau $(( $(date +%s) - t0 ))s"

echo "== 3. chay kernel tren core 0"
SOC=$(grep -v '^#' devices/picasso/soc.args | grep . | tr '\n' ' ')
./xnuloader/mkpayload.py -o run-payload.bin kernel=hakios-kernel ramdisk=userland/root.apfs \
	--cmdline "loader_return_after=$SECS epoch=$(date +%s) -v debug=0x8 hv_wdt_pet=1 cpus=1 ufs=2 $SOC" >/dev/null || exit 1
timeout 120 fastboot stage run-payload.bin 2>&1 | grep -q OKAY || { echo "phone-darwin: stage kernel that bai"; exit 1; }
timeout 30 fastboot oem run 2>&1 | tail -1
echo "== dang chay $SECS s: nhin man hinh dien thoai"
