#!/bin/bash
# Chay kernel PREBUILT cua HakiOS (ban da kiem tren redwood) tren picasso,
# voi loader + identity that. Neu no qua duoc "VM boostrap: 0 maps" thi loi nam
# o config build cua ta; neu cung dung yen thi la van de port SM7250.
#
#   hakios/ref-kernel-test.sh [SECS]
set -u
SECS=${1:-30}
H=/home/tritm/hakios-u
IOS=$(cd "$(dirname "$0")/.." && pwd)

for i in $(seq 1 40); do
	fastboot devices 2>/dev/null | grep -q xnuloader && break
	sleep 3
done
fastboot devices 2>/dev/null | grep -q xnuloader || { echo "ref-kernel-test: loader khong co (can power-cycle)"; exit 1; }

SOC=$(grep -v '^#' "$H/devices/picasso/soc.args" | grep . | tr '\n' ' ')
cd "$IOS" || exit 1
KERNEL=$H/prebuilt/hakios-kernel RAMDISK=prebuilt/userland/hello \
	timeout $(( SECS + 90 )) bash hakios/xl-run.sh "$SECS" \
	"-v debug=0x8 rd=md0 hv_wdt_pet=1 cpus=1 $SOC" 2>&1 |
	grep -aE "payload entry 'kernel'|kernel.ver|jumping to|kernel returned|VM boostrap|CP[0-9]+|H[0-9]+=|result:"
