#!/bin/bash
# Vong lap NHANH: chi stage kernel (khong nap 2,3 GB) roi chay.
# Dung de kiem tra kernel di duoc bao xa (man hinh hien cac dong HAKIOS:).
#   hakios/fast-run.sh [SECS] [cmdline extra...]
set -u
SECS=${1:-40}; shift || true
H=${HAKIOS:-/home/tritm/hakios-u}
cd "$H" || exit 1
for i in $(seq 1 80); do timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader && { echo "loader up sau $((i*5))s"; break; }; sleep 5; done
timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader || { echo "fast-run: khong co loader (tat/bat may)"; exit 1; }
SOC=$(grep -v '^#' devices/picasso/soc.args | grep . | tr '\n' ' ')
./xnuloader/mkpayload.py -o /tmp/fast-payload.bin loader.efi=xnuloader/loader.efi kernel=hakios-kernel --usb \
	--cmdline "loader_return_after=$SECS -v debug=0x8 rd=md0 hv_wdt_pet=1 cpus=1 $SOC $*" >/dev/null || exit 1
timeout 90 fastboot stage /tmp/fast-payload.bin 2>&1 | grep -q OKAY || { echo "fast-run: stage that bai"; exit 1; }
timeout 20 fastboot oem run 2>&1 | tail -1
echo "== dang chay $SECS s: xem man hinh"
