#!/bin/sh
# Chay tren MAY TRUNG GIAN (relay): nạp ảnh root + kernel payload + run, roi lay log.
#   /root/hakios/run.sh [--no-upload] [--reboot]
# Can: /root/hakios/system.apfs, /root/hakios/run-payload.bin (copy tu workstation).
# Nguyen tac: MOI lenh fastboot co timeout ngan; neu loader khong dap thi bao ngay
# (kernel dang chiem core 0) thay vi treo.
set -u
D=/root/hakios
UP=1; RB=0
for a in "$@"; do
	case "$a" in
	--no-upload) UP=0 ;;
	--reboot) RB=1 ;;
	esac
done
say() { echo "$*"; }
fb() { timeout "${FB_T:-20}" fastboot "$@"; }

if [ "$RB" = 1 ]; then
	say "run: reboot vao boot image"
	fb reboot >/dev/null 2>&1
	sleep 20
	for i in $(seq 1 20); do fb devices 2>/dev/null | grep -q xnuloader && break; sleep 3; done
fi
if ! fb devices 2>/dev/null | grep -q fastboot; then
	say "run: khong thay fastboot (loader chet? can tat/bat may)"; exit 2
fi
if ! fb getvar product 2>/dev/null | grep -q xnuloader; then
	say "run: thiet bi co tren USB nhung KHONG dap -> loader chet (kernel dang chay) -> tat/bat may"; exit 2
fi
say "run: loader OK"

if [ "$UP" = 1 ]; then
	fb oem ramdisk 2>/dev/null | grep -q OKAY || { say "run: oem ramdisk tu choi"; exit 2; }
	say "run: nap $(du -m $D/system.apfs | cut -f1) MB ..."
	t0=$(date +%s)
	FB_T=900 fb stage $D/system.apfs 2>&1 | grep -q OKAY || { say "run: nap system.apfs that bai"; exit 2; }
	say "run: nap xong $(( $(date +%s) - t0 ))s"
fi
FB_T=60 fb stage $D/run-payload.bin 2>&1 | grep -q OKAY || { say "run: stage payload that bai"; exit 2; }
fb oem run >/dev/null 2>&1
say "run: kernel dang chay (payload co loader_return_after)"

sleep 45
if ! fb getvar product 2>/dev/null | grep -q xnuloader; then
	say "run: loader chet -> KERNEL DANG CHAY (hoac treo). Nhin man hinh dien thoai."
	exit 0
fi
say "run: kernel tra ve, lay log"
fb oem log >/dev/null 2>&1
rm -f $D/last.log
FB_T=60 fb get_staged $D/last.log >/dev/null 2>&1
say "run: log $(stat -c %s $D/last.log 2>/dev/null || echo 0) B"
tr -d '\0' < $D/last.log 2>/dev/null | grep -aE "probe:|HAKIOSI|launchd|ignition|panic|kernel returned" | tail -25
