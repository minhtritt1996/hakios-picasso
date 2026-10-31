#!/bin/bash
# Chay day du tren may: nap system.apfs vao RAM, roi chay kernel (core 0) voi root.apfs.
# Dung khi loader dang song (sau mot lan bat may). Sau khi chay, man hinh la kenh duy nhat.
#
#   hakios/phone-full-run.sh [SECS]
set -u
SECS=${1:-120}
H=${HAKIOS:-/home/tritm/hakios-u}
cd "$H" || exit 1

fastboot devices 2>/dev/null | grep -q xnuloader || { echo "phone-full-run: loader khong co (bat may lai)"; exit 1; }

echo "== nap system.apfs ($(du -m userland/system.apfs|cut -f1) MB)"
t0=$(date +%s)
timeout 60 fastboot oem ramdisk 2>&1 | grep -q OKAY || { echo "phone-full-run: oem ramdisk tu choi"; exit 1; }
timeout 1800 fastboot stage userland/system.apfs 2>&1 | grep -q OKAY || { echo "phone-full-run: nap system.apfs that bai"; exit 1; }
echo "   xong sau $(( $(date +%s) - t0 ))s"

echo "== chay kernel"
SOC=$(grep -v '^#' devices/picasso/soc.args | grep . | tr '\n' ' ')
./xnuloader/mkpayload.py -o run-payload.bin kernel=hakios-kernel ramdisk=userland/root.apfs \
	--cmdline "loader_return_after=$SECS epoch=$(date +%s) -v debug=0x8 hv_wdt_pet=1 cpus=1 ufs=2 $SOC" >/dev/null || exit 1
timeout 120 fastboot stage run-payload.bin 2>&1 | grep -q OKAY || { echo "phone-full-run: stage that bai"; exit 1; }
timeout 30 fastboot oem run 2>&1 | tail -1
echo "== dang chay: nhin man hinh dien thoai (kernel -v in ra do)."
