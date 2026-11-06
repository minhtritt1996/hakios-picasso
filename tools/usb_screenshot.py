#!/usr/bin/env python3
import serial
import time
import sys
import os

def capture(output_path="/home/tritm/hakios-picasso/build/screen.bmp"):
    print(f"Connecting to /dev/ttyACM0 to capture framebuffer...")
    s = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
    s.write(b'\r\n')
    time.sleep(0.3)
    if s.in_waiting:
        s.read(s.in_waiting)

    print("Sending dumpfb command...")
    s.write(b'dumpfb\r\n')
    
    # Read until ---BEGIN_SCREENSHOT_BMP---
    buf = b""
    t0 = time.time()
    marker = b"---BEGIN_SCREENSHOT_BMP---"
    found = False
    while time.time() - t0 < 15:
        chunk = s.read(s.in_waiting or 1024)
        if not chunk:
            time.sleep(0.05)
            continue
        buf += chunk
        if marker in buf:
            found = True
            idx = buf.find(marker) + len(marker)
            while idx < len(buf) and buf[idx:idx+1] in (b'\r', b'\n'):
                idx += 1
            buf = buf[idx:]
            break

    if not found:
        print(f"Marker not found! Buffer tail was:\n{buf[-200:]}")
        s.close()
        return False

    print("Found header marker, streaming 10,368,054 BMP bytes...")
    total_wanted = 10368054
    data = bytearray(buf)
    last_print = time.time()
    t_start = time.time()

    while len(data) < total_wanted:
        to_read = min(65536, total_wanted - len(data))
        chunk = s.read(to_read)
        if chunk:
            data.extend(chunk)
            if b"---END_SCREENSHOT_BMP---" in data:
                break
            if time.time() - last_print > 1.0:
                pct = len(data) * 100.0 / total_wanted
                mb = len(data) / (1024 * 1024)
                mb_s = mb / max(0.001, time.time() - t_start)
                print(f"Progress: {mb:.1f}/9.88 MB ({pct:.1f}%) - {mb_s:.2f} MB/s", flush=True)
                last_print = time.time()
        else:
            if time.time() - t_start > 15 and len(data) >= 10368000:
                print(f"Pacing complete, saving received {len(data)} bytes...")
                break
            time.sleep(0.01)

    s.close()
    print(f"Capture complete: received {len(data)} bytes in {time.time() - t_start:.2f}s!")
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(data[:total_wanted])
    print(f"Saved BMP to: {output_path}")

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "/home/tritm/hakios-picasso/build/screen.bmp"
    capture(out)
