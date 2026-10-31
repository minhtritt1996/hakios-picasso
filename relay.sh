#!/bin/bash
# Chay fastboot/adb tren mot may trung gian (relay) qua ssh, tu may nay.
#
#   ./relay.sh [--host H] [--pass P] -- <lenh fastboot/adb ...>
#
# Vi du:
#   ./relay.sh -- fastboot devices
#   ./relay.sh --host k60 -- fastboot oem ramdisk
#   ./relay.sh --host 100.125.15.70 --pass 1234 -- fastboot stage /root/hakios/system.apfs
#
# Mac dinh: k60 = 100.121.254.115 (root/k60). Voi box kia: --host 100.125.15.70 --pass 1234.
# Helper tu kiem tra ssh truoc (fail2ban hay chan thi bao ngay, khong treo).
set -u
HOST=100.121.254.115
USER=root
PASS=k60
while [ $# -gt 0 ]; do
	case "$1" in
	--host) shift; case "${1:-}" in k60) HOST=100.121.254.115; PASS=k60;; *) HOST=$1;; esac ;;
	--pass) shift; PASS=${1:-} ;;
	--user) shift; USER=${1:-root} ;;
	--) shift; break ;;
	*) break ;;
	esac
	shift
done
[ $# -gt 0 ] || { echo "relay: thieu lenh (vd: -- fastboot devices)" >&2; exit 64; }

SSH="sshpass -p $PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=8 $USER@$HOST"
if ! timeout 20 $SSH true 2>/dev/null; then
	echo "relay: khong vao duoc $USER@$HOST (fail2ban? sshd tat?)" >&2
	echo "relay: tren box: fail2ban-client set sshd unbanip <ip>  |  systemctl restart ssh" >&2
	exit 2
fi
# lenh duoc quote lai de giu nguyen tham so
cmd=$(printf '%q ' "$@")
timeout ${RELAY_TIMEOUT:-1800} $SSH "$cmd"
