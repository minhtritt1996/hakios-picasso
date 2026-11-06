#!/usr/bin/env python3
"""Stage the payload in RAM and run it; print whatever is on the phone's screen.

No UFS write: `fastboot stage` + `fastboot oem run` only.
The payload is signed over out/payload.bin each time, which is cheap; nothing
else is rebuilt.

  tools/quick_run.py            # payload.bin as built
"""
import glob
import os
import re
import subprocess
import sys
import time

REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_DIR, 'build', 'out')


def run(cmd):
    print(f'+ {cmd}', flush=True)
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f'{cmd} failed: {res.stdout}{res.stderr}')


def main():
    payload = os.path.join(OUT_DIR, 'payload.bin')
    if len(sys.argv) > 1:
        payload = sys.argv[1]
    run(f'fastboot stage {payload}')
    run('fastboot oem run')
    port = None
    deadline = time.time() + 60
    while time.time() < deadline and port is None:
        ports = glob.glob('/dev/ttyACM*')
        port = ports[0] if ports else None
        if port is None:
            time.sleep(0.3)
    if port is None:
        sys.exit('no CDC console')
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    time.sleep(1.5)
    try:
        while os.read(fd, 65536):
            pass
    except BlockingIOError:
        pass
    os.write(fd, b'\r\n')
    time.sleep(1)
    for _ in range(20):
        try:
            data = os.read(fd, 1 << 20)
        except BlockingIOError:
            data = b''
        if data:
            sys.stdout.buffer.write(re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b' ', data)[-20000:])
            sys.stdout.buffer.flush()
            break
        time.sleep(0.3)
    os.close(fd)


if __name__ == '__main__':
    main()
