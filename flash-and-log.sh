#!/bin/bash
# Flash mot boot image (dang o ABL fastboot), reboot, cho loader len, roi fetch log
# khong can `stage` (payload da nam trong boot image).
#
#   hakios/flash-and-log.sh IMG [SECS]
#
# Sau khi kernel chay xong (loader_return_after trong cmdline), loader tra loi
# `oem log` + `get_staged`; script in cac moc CP/H va phan ket qua.
set -u
IMG=${1:?usage: flash-and-log.sh IMG [SECS]}
WAIT=${2:-40}
OUT=/tmp/hakios-bootlog-$(date +%H%M%S).txt

fastboot devices 2>/dev/null | grep -q fastboot || { echo "flash-and-log: khong thay fastboot (can Vol-Down + Power)"; exit 1; }
prod=$(timeout 8 fastboot getvar product 2>/dev/null | sed -n 's/^product: //p')
echo "flash-and-log: product=$prod img=$(stat -c %s "$IMG") B"
timeout 120 fastboot flash boot "$IMG" 2>&1 | tail -2
timeout 20 fastboot reboot >/dev/null 2>&1

echo "flash-and-log: cho loader (${WAIT}s)..."
for i in $(seq 1 $((WAIT / 3))); do
	fastboot devices 2>/dev/null | grep -q xnuloader && break
	sleep 3
done
fastboot devices 2>/dev/null | grep -q xnuloader || { echo "flash-and-log: loader khong len"; exit 1; }

# kernel chay loader_return_after giay; doi them cho no tra ve
sleep 12
for try in 1 2 3 4 5 6; do
	if timeout 20 fastboot oem log >/dev/null 2>&1; then
		timeout 40 fastboot get_staged "$OUT" >/dev/null 2>&1 && [ -s "$OUT" ] && break
	fi
	sleep 5
done
echo "flash-and-log: log $OUT ($(stat -c %s "$OUT" 2>/dev/null || echo 0) B)"
tr -d '\0' < "$OUT" 2>/dev/null | grep -aoE "(CP[0-9]+|H[0-9]+=[0-9a-f]+|VM boostrap.*|kernel returned.*|result:.*|jumping to [0-9a-f]+)" | tr '\n' ' '
echo
