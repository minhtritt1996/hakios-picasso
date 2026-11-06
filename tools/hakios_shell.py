#!/usr/bin/env python3
"""
HakiOS Interactive Root Terminal
Connects standard host terminal (stdin/stdout) to HakiOS Root Console on Xiaomi Redmi K30 5G.
Supports full interactive input, arrow keys, commands, and escape sequences.
"""

import serial
import sys
import os
import tty
import termios
import select
import time

def main():
    port = os.environ.get("HAKIOS_TTY", "/dev/ttyACM0")
    if not os.path.exists(port):
        sys.stderr.write(f"Error: Device {port} not found. Is phone connected in HakiOS mode?\n")
        sys.exit(1)

    try:
        s = serial.Serial(port, 115200, timeout=0.05)
        s.dtr = True
        s.rts = True
    except Exception as e:
        sys.stderr.write(f"Error opening {port}: {e}\n")
        sys.exit(1)

    # Wake shell
    s.write(b"\n")
    time.sleep(0.05)
    if s.in_waiting:
        out = s.read(s.in_waiting)
        sys.stdout.buffer.write(out)
        sys.stdout.buffer.flush()

    old_attr = termios.tcgetattr(sys.stdin)
    try:
        tty.setraw(sys.stdin.fileno())
        s_fd = s.fileno()
        in_fd = sys.stdin.fileno()

        while True:
            rlist, _, _ = select.select([in_fd, s_fd], [], [])
            if in_fd in rlist:
                ch = os.read(in_fd, 512)
                if not ch:
                    break
                # Ctrl-] to exit terminal
                if b'\x1d' in ch:
                    break
                s.write(ch)
                s.flush()

            if s_fd in rlist:
                data = os.read(s_fd, 4096)
                if not data:
                    break
                os.write(sys.stdout.fileno(), data)
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_attr)
        s.close()
        print("\n[Disconnected from HakiOS Root Terminal]")

if __name__ == "__main__":
    main()
