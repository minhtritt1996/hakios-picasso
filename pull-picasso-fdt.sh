#!/bin/sh
# Thu dữ liệu picasso cần cho port HakiOS (bước 1 trong docs/porting của họ).
# Chạy khi điện thoại ở Android/recovery và adb thấy nó. Không ghi gì vào máy.
set -eu
SER=${1:-}
OUT=$(cd "$(dirname "$0")" && pwd)/picasso
ADB="adb"
[ -n "$SER" ] && ADB="adb -s $SER"
mkdir -p "$OUT"

echo "== adb"
$ADB devices -l

echo "== device tree"
$ADB shell "cat /sys/firmware/fdt" 2>/dev/null | tr -d '\r' > "$OUT/fdt.dtb" || true
# recovery thường không export /sys/firmware/fdt; thử /proc/device-tree
if [ ! -s "$OUT/fdt.dtb" ]; then
	$ADB shell "ls /proc/device-tree >/dev/null 2>&1" && \
	$ADB shell "cd /proc/device-tree && tar cf - . 2>/dev/null" | tar xf - -C "$OUT.dt" 2>/dev/null || true
fi
ls -l "$OUT/fdt.dtb" 2>/dev/null || echo "  (khong co fdt: chay lai tu Android, hoac /proc/device-tree)"

echo "== cmdline / soc"
$ADB shell "cat /proc/cmdline" 2>/dev/null | tr -d '\r' > "$OUT/cmdline.txt" || true
$ADB shell "for f in soc_id machine platform family; do echo \$f=\$(cat /sys/devices/soc0/\$f 2>/dev/null); done" 2>/dev/null | tr -d '\r' > "$OUT/soc0.txt" || true

echo "== partitions"
$ADB shell "ls -l /dev/block/bootdevice/by-name" 2>/dev/null | tr -d '\r' > "$OUT/partitions.txt" || true

echo "== xong -> $OUT"
ls -l "$OUT"
