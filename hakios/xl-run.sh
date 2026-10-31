#!/bin/bash
# Chay kernel HakiOS tren loader dang song, KHONG qua `xl enter` (duong do
# doi hoi anh U-Boot/Silicium va getvar de treo sau mot lan kernel hang).
#
#   hakios/xl-run.sh [SECS] [extra kernel args...]
#
# 1. dung payload bang xnuloader/mkpayload.py (kernel + cmdline)
# 2. fastboot stage / oem run
# 3. theo doi getvar result, fetch log moi 3 s (oem log + get_staged)
set -u
H=${HAKIOS:-/home/tritm/hakios-u}
SECS=${1:-20}; shift || true
CKPT=${CHECKPOINT:-}
ARGS=${*:-"-v debug=0x8 hv_wdt_pet=1 cpus=1"}
cd "$H" || exit 1
OUT=logs/hakios-run-$(date +%H%M%S).log
mkdir -p logs

if ! fastboot devices 2>/dev/null | grep -q xnuloader; then
	echo "xl-run: loader khong co tren USB (data: $(fastboot devices 2>&1 | head -2))" >&2
	exit 1
fi

K=${KERNEL:-hakios-kernel}; [ -f "$K" ] || K=prebuilt/hakios-kernel
CKOPT=
RM=${RAMDISK:-}; ROPT=""; [ -n "$RM" ] && ROPT="ramdisk=$RM"
[ -n "$CKPT" ] && CKOPT="--checkpoint $CKPT"
xnuloader/mkpayload.py -o run-payload.bin kernel=$K $ROPT $CKOPT \
	--cmdline "loader_return_after=$SECS epoch=$(date +%s) sb_delay=8 $ARGS" >/dev/null || exit 1
echo "xl-run: payload $(stat -c %s run-payload.bin) B, args: $ARGS"
timeout 60 fastboot stage run-payload.bin 2>&1 | tail -1 || { echo "xl-run: stage hung (loader can ket; can power-cycle)"; exit 1; }
timeout 30 fastboot oem run 2>&1 | tail -1

end=$(( $(date +%s) + SECS + 60 ))
last=0
while [ "$(date +%s)" -lt "$end" ]; do
	r=$(FB_TIMEOUT=10 timeout 20 fastboot getvar result 2>/dev/null | sed -n 's/^result: //p')
	[ -n "$r" ] && case $r in *running*) ;; *) echo "xl-run: result: $r"; break;; esac
	if [ $(( $(date +%s) - last )) -ge 3 ]; then
		last=$(date +%s)
		timeout 30 fastboot get_staged "$OUT" >/dev/null 2>&1
	fi
	sleep 2
done
[ -s "$OUT" ] || { fastboot oem log >/dev/null 2>&1 && fastboot get_staged "$OUT" >/dev/null 2>&1; }
echo "xl-run: log $OUT ($(stat -c %s "$OUT" 2>/dev/null || echo 0) B)"
tr -d '\0' < "$OUT" 2>/dev/null | tail -40
