#!/usr/bin/env python3
"""Send commands to the phone's root console (USB CDC-ACM) and print the answers.
usage: tools/console.py [-w SECONDS] COMMAND...   (default port /dev/ttyACM0, override with HAKIOS_TTY)"""
import fcntl, os, select, struct, sys, time

args = sys.argv[1:]
wait = 5.0
if args[:1] == ['-w']:
    wait, args = float(args[1]), args[2:]
fd = os.open(os.environ.get('HAKIOS_TTY', '/dev/ttyACM0'), os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
fcntl.ioctl(fd, 0x5416, struct.pack('I', 0x006))   # TIOCMBIS: DTR | RTS

def read_for(seconds):
    out, end = b'', time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.3)[0]:
            out += os.read(fd, 4096)
    return out.decode(errors='replace')

print(read_for(1.5), end='')
for command in args:
    os.write(fd, command.encode() + b'\r\n')
    print(read_for(wait), end='')
