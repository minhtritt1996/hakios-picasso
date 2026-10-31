#!/bin/bash
# Doc log loader da ghi vao partition `logdump` (loader ghi o moi lan chay kernel),
# qua recovery (OrangeFox) + adb. Dung khi loader da mat USB sau mot lan chay.
#
#   hakios/read-logdump.sh [OUT]
#
# Tien dieu kien: may dang o ABL fastboot (Vol-Down + Power).
set -u
OUT=${1:-/tmp/logdump-hakios.log}
fastboot devices 2>/dev/null | grep -q fastboot || { echo "read-logdump: khong thay fastboot"; exit 1; }

echo "== vao recovery"
timeout 30 fastboot reboot recovery 2>&1 | tail -1
for i in $(seq 1 30); do
	adb devices 2>/dev/null | grep -q recovery && { echo "recovery up"; break; }
	sleep 3
done
adb devices 2>/dev/null | grep -q recovery || { echo "read-logdump: recovery khong len"; exit 1; }

echo "== doc logdump"
adb exec-out "dd if=/dev/block/by-name/logdump bs=65536 count=48 2>/dev/null" > "$OUT.raw"
ls -l "$OUT.raw" | awk '{print "   " $5 " bytes"}'
# chi giu phan co chu (loader + kernel log), bo phan trong
tr -d '\0' < "$OUT.raw" | tr -s '\n' > "$OUT"
echo "== $OUT"
grep -aE "XNULOADER|resident image|VM boostrap|launchd|SpringBoard|ignition|hardware target|panic|init:|cp [0-9]+ " "$OUT" | head -40
