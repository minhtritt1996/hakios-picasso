#!/bin/sh
# Doc log cua lan chay vua roi tu partition logdump, qua recovery.
#   /root/hakios/readlog.sh            (may o ABL fastboot hoac recovery)
# 1. vao recovery bang fastboot boot (RAM, khong ghi gi vao may)
# 2. adb: dd logdump -> loc cac dong probe/panic/HAKIOS
set -u
D=/root/hakios
fb() { timeout "${FB_T:-25}" fastboot "$@"; }

if fb devices 2>/dev/null | grep -q recovery; then
	echo "readlog: da o recovery"
else
	if fb devices 2>/dev/null | grep -q fastboot; then
		echo "readlog: boot recovery tu RAM ($D/recovery.img)"
		FB_T=180 fb boot $D/recovery.img 2>&1 | tail -1
	else
		echo "readlog: khong thay thiet bi tren USB (tat/bat may roi chay lai)"; exit 2
	fi
fi
for i in $(seq 1 20); do
	adb -s f9f3d7b0 devices 2>/dev/null | grep -q recovery && { echo "readlog: recovery up"; break; }
	sleep 3
done
adb -s f9f3d7b0 devices 2>/dev/null | grep -q recovery || { echo "readlog: recovery khong len (adb)"; exit 2; }

adb -s f9f3d7b0 exec-out "dd if=/dev/block/by-name/logdump bs=1048576 2>/dev/null" > $D/logdump.raw
echo "readlog: logdump $(stat -c %s $D/logdump.raw) B"
echo "---- cac dong dang chu y ----"
tr -d '\0' < $D/logdump.raw | grep -aE "probe:|HAKIOSI|HAKIOS: (setconf|IOFindBSDRoot|root|VFS|utask|load_init)|launchd|ignition|panic\(cpu|kernel returned|resident image" | tail -40; echo "---- H markers ----"; tr -d '\0' < $D/logdump.raw | grep -aoE "H[0-9]+=[0-9a-f]+" | tail -20
