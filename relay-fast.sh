#!/bin/sh
# Vong lap NHANH tren relay: chi 15 MB payload, khong nap anh root, kernel panic
# de loader tra log qua fastboot (khong can chup man hinh).
#   /root/hakios/fast.sh [payload]
# Mat ~40 s: (neu can) reboot vao loader 20 s + stage 1 s + chay + panic + lay log.
set -u
D=/root/hakios
P=${1:-$D/probe-panic.bin}
t() { timeout "${1}" shift; "$@"; }
say() { echo "$*"; }

state() {
	if timeout 6 fastboot devices 2>/dev/null | grep -q xnuloader; then
		if timeout 6 fastboot getvar product 2>/dev/null | grep -q xnuloader; then echo loader; return; fi
		echo loader-dead; return
	fi
	if timeout 6 fastboot devices 2>/dev/null | grep -q fastboot; then echo abl; return; fi
	echo none
}

case "$(state)" in
loader) say "fast: loader OK" ;;
abl)
	say "fast: o ABL -> reboot vao boot image"
	timeout 20 fastboot reboot >/dev/null 2>&1
	sleep 15
	for i in $(seq 1 15); do
		[ "$(state)" = loader ] && break
		sleep 3
	done
	[ "$(state)" = loader ] || { say "fast: loader khong len"; exit 2; }
	;;
loader-dead) say "fast: loader ket -> TAT/BAT may"; exit 2 ;;
none) say "fast: khong thay thiet bi -> bat may / cam cap"; exit 2 ;;
esac

timeout 40 fastboot stage "$P" 2>&1 | grep -q OKAY || { say "fast: stage that bai"; exit 2; }
timeout 15 fastboot oem run >/dev/null 2>&1
say "fast: da chay, doi panic..."
for i in $(seq 1 12); do
	sleep 3
	if timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader; then break; fi
done
if ! timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader; then
	say "fast: loader khong tra loi -> kernel treo (khong panic)"; exit 0
fi
timeout 20 fastboot oem log >/dev/null 2>&1
rm -f $D/fast.log
timeout 40 fastboot get_staged $D/fast.log >/dev/null 2>&1
say "fast: log $(stat -c %s $D/fast.log 2>/dev/null || echo 0) B"
tr -d '\0' < $D/fast.log 2>/dev/null | grep -aE "probe:|panic\(cpu|panicked|IOFindBSDRoot|HAKIOSI" | head -12
