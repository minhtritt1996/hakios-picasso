#!/usr/bin/env python3
"""
Dump and parse Apple embedded panic log from /pram (512 KB PRAM buffer)
either directly via fastboot or from a saved binary file.
"""
import sys
import os
import struct
import argparse
import subprocess

EMBEDDED_PANIC_MAGIC = 0x46554E4B  # 'FUNK'

def parse_panic_buf(data):
    if len(data) < 40:
        print(f"[-] Buffer too small ({len(data)} bytes, expected >= 40)")
        return False

    magic, crc, ver, flags, log_off, log_len, ss_off, ss_len, other_off, other_len = struct.unpack_from('<10I', data, 0)
    if magic != EMBEDDED_PANIC_MAGIC:
        print(f"[-] Invalid magic: 0x{magic:08x} (expected 0x{EMBEDDED_PANIC_MAGIC:08x} 'FUNK')")
        return False

    print(f"[+] Apple Embedded Panic Log Detected:")
    print(f"    Version:          {ver}")
    print(f"    Flags:            0x{flags:08x}")
    print(f"    Panic Log Offset: 0x{log_off:x} ({log_off}), Length: {log_len} bytes")
    print(f"    Stackshot Offset: 0x{ss_off:x} ({ss_off}), Length: {ss_len} bytes")
    print(f"    Other Log Offset: 0x{other_off:x} ({other_off}), Length: {other_len} bytes")
    print("=" * 60)

    if log_off < len(data) and log_len > 0:
        panic_text = data[log_off : min(log_off + log_len, len(data))].decode('utf-8', errors='replace')
        print(panic_text)
    else:
        print("[-] Panic log string empty or invalid offset.")

    if other_off < len(data) and other_len > 0:
        print("=" * 60)
        print("[+] Other Log:")
        other_text = data[other_off : min(other_off + other_len, len(data))].decode('utf-8', errors='replace')
        print(other_text)

    return True

def main():
    parser = argparse.ArgumentParser(description="Extract & parse Apple embedded panic log from Picasso PRAM")
    parser.add_argument("--file", "-f", help="Read panic log from dumped file instead of fastboot")
    parser.add_argument("--out", "-o", default="/tmp/panic_pram.bin", help="Output file when pulling from fastboot")
    args = parser.parse_args()

    if args.file:
        with open(args.file, "rb") as f:
            data = f.read()
        parse_panic_buf(data)
        return

    print("[*] Pulling paniclog from device via 'fastboot oem paniclog'...")
    res1 = subprocess.run(["fastboot", "oem", "paniclog"], capture_output=True, text=True)
    if res1.returncode != 0:
        print(f"[-] fastboot oem paniclog failed: {res1.stderr.strip() or res1.stdout.strip()}")
        sys.exit(1)

    print(f"[*] Staging paniclog to {args.out}...")
    res2 = subprocess.run(["fastboot", "get_staged", args.out], capture_output=True, text=True)
    if res2.returncode != 0:
        print(f"[-] fastboot get_staged failed: {res2.stderr.strip() or res2.stdout.strip()}")
        sys.exit(1)

    with open(args.out, "rb") as f:
        data = f.read()

    parse_panic_buf(data)

if __name__ == "__main__":
    main()
