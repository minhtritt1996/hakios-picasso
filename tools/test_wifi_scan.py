#!/usr/bin/env python3
import time
import os
import glob
import termios
import fcntl
import struct
import select
import sys

def configure_serial(fd):
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

def assert_modem_lines(fd):
    wanted = termios.TIOCM_DTR | termios.TIOCM_RTS
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', wanted))

print("[*] Waiting for CDC ACM serial port (/dev/ttyACM*)...", flush=True)
found_port = None
start_wait = time.time()
while time.time() - start_wait < 30:
    ports = glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*')
    if ports:
        found_port = sorted(ports)[0]
        try:
            fd = os.open(found_port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
            configure_serial(fd)
            assert_modem_lines(fd)
            print(f"[+] Connected to {found_port} with DTR/RTS asserted!", flush=True)
            break
        except Exception as e:
            time.sleep(0.3)
    time.sleep(0.3)

if not found_port or 'fd' not in locals():
    print("[-] Serial port did not appear in 30s!", flush=True)
    sys.exit(1)

log_buffer = []
prompt_seen = False
wlan_ready = False
t0 = time.time()

# Phase 1: Wait for bootup, prompt and WLAN radio ready
print("[*] Monitoring boot sequence...", flush=True)
while time.time() - t0 < 25:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            chunk = os.read(fd, 4096)
            if chunk:
                text = chunk.decode('utf-8', errors='replace')
                sys.stdout.write(text)
                sys.stdout.flush()
                log_buffer.append(text)
                if 'WLAN RADIO IS ACTIVE' in text or 'CE0/CE2 MMIO configured' in text or 'CE3/CE2 MMIO configured' in text:
                    wlan_ready = True
                if 'root@hakios-picasso' in text or '#' in text:
                    prompt_seen = True
        except Exception as e:
            print(f"\n[!] Read error: {e}", flush=True)
            break
    if prompt_seen and wlan_ready and (time.time() - t0 > 12):
        break

time.sleep(1)

# Phase 2: Send 'wifi status'
print("\n" + "=" * 60, flush=True)
print("[*] Sending 'wifi status'...", flush=True)
os.write(fd, b"wifi status\r\n")
t1 = time.time()
while time.time() - t1 < 4:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        chunk = os.read(fd, 4096)
        if chunk:
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()

# Phase 3: Send 'wifi scan'
print("\n" + "=" * 60, flush=True)
print("[*] Sending 'wifi scan'...", flush=True)
os.write(fd, b"wifi scan\r\n")
t2 = time.time()
while time.time() - t2 < 10:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        chunk = os.read(fd, 4096)
        if chunk:
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()

# Phase 4: Send another 'wifi scan' to see accumulated BSSs
print("\n" + "=" * 60, flush=True)
print("[*] Sending second 'wifi scan'...", flush=True)
os.write(fd, b"wifi scan\r\n")
t3 = time.time()
while time.time() - t3 < 8:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        chunk = os.read(fd, 4096)
        if chunk:
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()

print("\n" + "=" * 60, flush=True)
print("[*] Test completed. Closing port.", flush=True)
os.close(fd)
