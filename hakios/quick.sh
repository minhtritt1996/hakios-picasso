#!/bin/bash
# Vong thu chuanc hoa: cho loader, (tuy chon) nap system.apfs, chay kernel, roi lay log.
#
#   hakios/quick.sh [SECS] [--full] [--mfb] [-- <cmdline extra>]
#
#   SECS   thoi gian kernel chay (mac dinh 60)
#   --full nap userland/system.apfs (2,3 GB) vao RAM truoc khi chay
#   --mfb  bat driver hien thi (mfb=1) - can cho SpringBoard
#   --     moi thu sau day dua vao cmdline cua kernel
#
# Vi sao probe khac truoc: `fastboot devices` chi can USB enumerate (nhanh, khong
# can thiet bi tra loi protocol). Chi khi da thay thiet bi moi hoi getvar; neu
# getvar khong dap sau 8 s thi that su loader da ket.
set -u
SECS=${1:-60}; shift || true
FULL=0; MFB=0; EXTRA=""
while [ $# -gt 0 ]; do
	case "$1" in
	--full) FULL=1 ;;
	--mfb)  MFB=1 ;;
	--)     shift; EXTRA="$*"; break ;;
	*)      EXTRA="$EXTRA $1" ;;
	esac
	shift
done
H=${HAKIOS:-/home/tritm/hakios-u}
cd "$H" || exit 1
say() { printf '%s\n' "$*" >&2; }

# 1. loader: doi enumerate roi doi protocol
ok=0
for i in $(seq 1 40); do
	if timeout 4 fastboot devices 2>/dev/null | grep -q xnuloader; then
		if timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader; then ok=1; break; fi
		# co tren USB ma khong dap: co the dang boot - thu lai vai lan
	fi
	sleep 4
done
[ "$ok" = 1 ] || { say "quick: loader khong tra loi -> TAT/BAT may (hoac cam lai USB)"; exit 2; }
say "quick: loader OK"

SOC=$(grep -v '^#' devices/picasso/soc.args | grep . | tr '\n' ' ')
BITS="loader_return_after=$SECS -v debug=0x8 rd=md0 hv_wdt_pet=1 cpus=1 $SOC"
[ "$MFB" = 1 ] && BITS="$BITS mfb=1"
[ -n "$EXTRA" ] && BITS="$BITS $EXTRA"

# 2. (tuy chon) userland vao RAM
if [ "$FULL" = 1 ]; then
	timeout 8 fastboot oem ramdisk 2>/dev/null | grep -q OKAY || { say "quick: oem ramdisk tu choi"; exit 2; }
	say "quick: nap system.apfs $(du -m userland/system.apfs|cut -f1) MB ..."
	t0=$(date +%s)
	timeout 300 fastboot stage userland/system.apfs 2>&1 | grep -q OKAY || { say "quick: nap that bai"; exit 2; }
	say "quick: nap xong sau $(( $(date +%s) - t0 ))s"
fi

# 3. payload + run
RD=""
[ "$FULL" = 1 ] && RD="ramdisk=userland/root.apfs"
./xnuloader/mkpayload.py -o /tmp/quick-payload.bin loader.efi=xnuloader/loader.efi kernel=hakios-kernel --usb \
	$RD --cmdline "$BITS" >/dev/null || exit 1
timeout 25 fastboot stage /tmp/quick-payload.bin 2>&1 | grep -q OKAY || { say "quick: stage that bai -> loader ket, TAT/BAT may"; exit 2; }
timeout 10 fastboot oem run 2>&1 | grep -q OKAY || { say "quick: oem run that bai"; exit 2; }
say "quick: dang chay $SECS s - nhin man hinh (HAKIOS:/HAKIOSI:)"

# 4. sau khi kernel xong: neu no tra ve/panic thi lay log luon
sleep $(( SECS + 5 ))
OUT=/tmp/quick-log-$(date +%H%M%S).log
if timeout 8 fastboot getvar product 2>/dev/null | grep -q xnuloader; then
	timeout 20 fastboot oem log >/dev/null 2>&1
	timeout 40 fastboot get_staged "$OUT" >/dev/null 2>&1
	say "quick: kernel tra ve; log $OUT ($(stat -c %s "$OUT" 2>/dev/null || echo 0) B)"
	tr -d '\0' < "$OUT" 2>/dev/null | grep -aE "HAKIOS|HAKIOSI|launchd|ignition|hardware target|panic" | tail -20
else
	say "quick: loader bi chiem core (kernel chay/treo) -> chup man hinh, roi TAT/BAT de vong tiep"
fi
