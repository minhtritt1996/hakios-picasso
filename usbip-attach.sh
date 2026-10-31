#!/bin/bash
# Attach the phone that sits on the Windows relay (usbipd-win) to this machine,
# and wait until fastboot/adb can see it.  Re-run after every reboot of the phone:
# the USB device re-enumerates on the relay and the share has to be attached again.
#
#   ./usbip-attach.sh [WINDOWS_IP] [PASSWORD]
#
# Defaults: 100.94.9.79, 123.  Needs sudo for vhci_hcd/usbip.
set -u
WIN=${1:-100.94.9.79}
PW=${2:-123}
SUDOPW=${3:-$PW}
SSH="sshpass -p $PW ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 administrator@$WIN"
USBIPD='"C:\Program Files\usbipd-win\usbipd.exe"'

line=$($SSH "$USBIPD list" 2>/dev/null | grep -E "18d1:d00d|2717:|05c6:" | head -1)
[ -n "$line" ] || { echo "usbip-attach: khong thay thiet bi Android tren relay"; exit 1; }
BUSID=$(echo "$line" | awk '{print $1}')
echo "usbip-attach: $BUSID  $(echo "$line" | sed 's/  */ /g' | cut -c1-70)"

case "$line" in
*"Not shared"*) echo "usbip-attach: bind $BUSID"; $SSH "$USBIPD bind --busid $BUSID --force" >/dev/null 2>&1 || true ;;
esac

echo "$SUDOPW" | sudo -S modprobe vhci_hcd 2>/dev/null
echo "$SUDOPW" | sudo -S usbip detach -p 00 >/dev/null 2>&1
echo "$SUDOPW" | sudo -S usbip attach -r "$WIN" -b "$BUSID" 2>&1 | tail -1

for i in $(seq 1 10); do
	if fastboot devices 2>/dev/null | grep -qE "fastboot|recovery"; then
		echo "usbip-attach: $(fastboot devices | head -1)"
		exit 0
	fi
	if adb devices 2>/dev/null | grep -qE "device$|recovery"; then
		echo "usbip-attach: $(adb devices | sed -n 2p)"
		exit 0
	fi
	sleep 2
done
echo "usbip-attach: attach xong nhung chua thay thiet bi (kiem tra cap/thiet bi tren relay)"
exit 2
