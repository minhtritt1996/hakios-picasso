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

REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_DIR, 'build', 'out')
KERNEL_WS = os.path.join(REPO_DIR, 'build', 'hakios-xnu-kernel')
if not os.path.isdir(KERNEL_WS):
    KERNEL_WS = os.path.join(os.path.dirname(REPO_DIR), 'hakios-u')

print('=== Step 1: Checking fastboot device ===', flush=True)
devs = run('fastboot devices')
if 'xnuloader' not in devs and 'fastboot' not in devs:
    raise RuntimeError('Device not found in fastboot!')

print('=== Step 2: Stage resident system.apfs (2.4 GB) ===', flush=True)
run('fastboot oem ramdisk', timeout=10)
t0 = time.time()
print('Uploading system.apfs to RAM (this takes ~75-100s)...', flush=True)
sys_apfs = os.path.join(OUT_DIR, 'system.apfs') if os.path.isfile(os.path.join(OUT_DIR, 'system.apfs')) else os.path.join(KERNEL_WS, 'userland/system.apfs')
run(f'fastboot stage {sys_apfs}', timeout=300)
print(f'Upload complete in {time.time() - t0:.1f}s!', flush=True)

print('=== Step 3: Create Boot Payload from build/out/ ===', flush=True)
soc_args = ' '.join(l.strip() for l in open(os.path.join(REPO_DIR, 'soc.args')) if l.strip() and not l.startswith('#'))
extra = os.environ.get('HAKIOS_EXTRA_ARGS', 'wifi=1 press_power=40 thermal=1 pmic=1 loader_return_after=86400')
cmdline = (
    '-v debug=0x14e rd=md0 nointr_consio=1 buttons=1 touchscreen=1 hv_wdt_pet=1 cpus=8 small_cores_after=0 '
    'mfb=1 ca_vbl=1 wifi=1 modem=0 adsp=0 sound=0 mic=0 bt=0 adreno=0 gpu=0 touch_lat=0 '
    'ufs=2 ufs_native=1 serial=3 picasso_usb=2 picasso_usb_watchdog=0 modem_wlansmmu=2 virtual_timer_intid=19 glwait=0 glcopy=1 glenv=OSMESA_SOFTWARE=1 '
)
# The kernel keeps only ~600 bytes of boot-args: what the test needs goes before the soc_* addresses.
_all = [a for a in soc_args.split() if not a.startswith('soc_')] + [a for a in soc_args.split() if a.startswith('soc_')]
cmdline += extra + ' ' + ' '.join(_all)
print('cmdline length', len(cmdline), flush=True)

payload_path = os.path.join(OUT_DIR, 'payload.bin')
mkpayload_cmd = (
    f'python3 {os.path.join(KERNEL_WS, "xnuloader/mkpayload.py")} -o {payload_path} '
    f'loader.efi={os.path.join(OUT_DIR, "loader.efi")} '
    f'kernel={os.path.join(OUT_DIR, "hakios-kernel")} '
    f'ramdisk={os.path.join(OUT_DIR, "root.apfs")} --usb '
    f'--cmdline "{cmdline}"'
)
run(mkpayload_cmd, timeout=30)

print('=== Step 4: Staging Boot Payload ===', flush=True)
run(f'fastboot stage {payload_path}', timeout=30)

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
    os.makedirs(os.path.join(REPO_DIR, 'build'), exist_ok=True)
    log_file = open(os.path.join(REPO_DIR, 'build', 'gui_springboard.log'), 'wb')
    stream_start = time.time()
    while time.time() - stream_start < int(os.environ.get('HAKIOS_CAPTURE_SECS', '450')):
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
