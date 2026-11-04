#!/usr/bin/env python3
import subprocess
import sys
import time
import os
import glob
import termios
import struct
import select
import fcntl

def run(cmd, timeout=None):
    print(f'+ {cmd}', flush=True)
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)
    if res.stdout:
        print(res.stdout, flush=True)
    if res.stderr:
        print(res.stderr, file=sys.stderr, flush=True)
    if res.returncode != 0:
        raise RuntimeError(f'Command failed with exit {res.returncode}')
    return res.stdout

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

print('=== Step 1: Checking fastboot device ===', flush=True)
devs = run('fastboot devices')
if 'xnuloader' not in devs and 'fastboot' not in devs:
    raise RuntimeError('Device not found in fastboot!')

print('=== Step 2: Stage resident system.apfs (2.4 GB) ===', flush=True)
run('fastboot oem ramdisk', timeout=10)
t0 = time.time()
print('Uploading system.apfs to RAM (this takes ~75-100s)...', flush=True)
run('fastboot stage /home/tritm/hakios-u/userland/system.apfs', timeout=300)
print(f'Upload complete in {time.time() - t0:.1f}s!', flush=True)

print('=== Step 3: Create Boot Payload ===', flush=True)
cmdline = (
    '-v debug=0x14e rd=md0 nointr_consio=1 buttons=1 touchscreen=1 hv_wdt_pet=1 cpus=8 small_cores_after=0 mfb=1 wifi=0 modem=0 adreno=22 adreno_keep=1 '
    'ufs=2 ufs_native=1 serial=3 picasso_usb=2 sweettick=1 sheng-lite=1 '
    'soc_ipa_base=0x1e00000 soc_gsi_base=0x1e04000 soc_ipcc=0x408000 soc_aoss=0xc300000 soc_rsc=0x18220000 '
    'soc_apps_smmu=0x15000000 soc_smem=0x80900000 soc_imem=0x146ab000 soc_cmd_db=0x80860000'
)

mkpayload_cmd = (
    'cd /home/tritm/hakios-u && ./xnuloader/mkpayload.py -o /tmp/payload_gui_full.bin '
    'loader.efi=xnuloader/loader.efi kernel=hakios-kernel ramdisk=userland/root.apfs --usb '
    f'--cmdline "{cmdline}"'
)
run(mkpayload_cmd, timeout=30)

print('=== Step 4: Staging Boot Payload ===', flush=True)
run('fastboot stage /tmp/payload_gui_full.bin', timeout=30)

print('=== Step 5: Executing fastboot oem run ===', flush=True)
run('fastboot oem run', timeout=10)

print('=== Step 6: Monitoring CDC console /dev/ttyACM* for SpringBoard bring-up ===', flush=True)
start_wait = time.time()
found_port = None
fd = None
while time.time() - start_wait < 90:
    ports = glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*')
    if ports:
        found_port = sorted(ports)[0]
        try:
            fd = os.open(found_port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
            configure_serial(fd)
            assert_modem_lines(fd)
            print(f'Connected and asserted DTR/RTS on {found_port}!', flush=True)
            break
        except Exception:
            time.sleep(0.2)
    time.sleep(0.2)

if fd is None:
    print('CDC console not enumerated yet, system running live on screen.', flush=True)
    sys.exit(0)

try:
    log_file = open('/home/tritm/gui_springboard.log', 'wb')
    stream_start = time.time()
    while time.time() - stream_start < 150:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            data = os.read(fd, 4096)
            if data:
                log_file.write(data)
                log_file.flush()
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    log_file.close()
    os.close(fd)
except Exception as e:
    print(f'Serial port error: {e}', flush=True)

print("=== FINISHED: Check phone screen for SpringBoard UI! ===", flush=True)
