#!/usr/bin/env python3
"""Boot the payload kept in RAM and watch the WCN3990 bring-up.

Nothing is written to the phone: `fastboot stage` puts the payload in RAM and
`fastboot oem run` starts it. Logs land in build/wifi_scan.log.

  HAKIOS_EXTRA_ARGS='wifi=1 wifi_scan=1 ...' tools/boot_wifi_scan.py
"""
import fcntl
import glob
import os
import select
import struct
import subprocess
import sys
import termios
import time

REPO_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_DIR, 'build', 'out')
KERNEL_WS = os.path.join(REPO_DIR, 'build', 'hakios-xnu-kernel')
LOG = os.path.join(REPO_DIR, 'build', 'wifi_scan.log')

# `sweettick=1` is not implicit: this port has no timer interrupt of its own (PPI 27 is group-0,
# owned by the secure side), so sweettick_start() is what pends the group-1 SPI that wakes
# assert_wait_timeout(). Without it dlil_init's msleep() never returns and the kernel hangs at
# "Waiting for all the create dlil kernel threads to get scheduled at least once."
#
# The loader keeps only BOOT_LINE_LENGTH-1 = 607 characters of the kernel's command line and it
# writes its own prefix (debug=0x8, cpus=1, rd=md0, hv_wdt_pet=1, hakios_kver=) BEFORE the payload's
# arguments. Anything past the 607th character is dropped without a diagnostic.
CMDLINE = (
    '-v debug=0x14e rd=md0 nointr_consio=1 buttons=1 touchscreen=1 hv_wdt_pet=1 cpus=8 small_cores_after=0 '
    'sweettick=1 mfb=0 ca_vbl=0 wifi=1 modem=0 modem_wlansmmu=2 adsp=0 sound=0 mic=0 bt=0 adreno=0 gpu=0 '
    'touch_lat=0 ufs=2 ufs_native=1 serial=3 picasso_usb=2 picasso_usb_watchdog=0 virtual_timer_intid=19 '
)
LIMIT = 607


def run(cmd):
    print(f'+ {cmd}', flush=True)
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    sys.stderr.write(res.stderr)
    if res.returncode != 0:
        raise RuntimeError(f'{cmd} failed ({res.returncode})')
    return res.stdout


def configure(fd):
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    fcntl.ioctl(fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR | termios.TIOCM_RTS))


def main():
    if 'fastboot' not in run('fastboot devices'):
        sys.exit('device not in fastboot')

    # xl args are for the loader itself (it strips them before the kernel sees them), so a long
    # loader_return_after= is not worth paying for: it is the payload's own words that overflow.
    extra = os.environ.get('HAKIOS_EXTRA_ARGS', '')
    soc_args = [l.strip() for l in open(os.path.join(REPO_DIR, 'soc.args')) if l.strip() and not l.startswith('#')]
    order = [a for a in soc_args if not a.startswith('soc_')] + [a for a in soc_args if a.startswith('soc_')]
    cmdline = (CMDLINE + extra).strip()

    # What the loader drops without a word. It writes its own words first (debug=0x8, then cpus=1
    # unless we pass cpus=, rd=md0, hv_wdt_pet=1 and two 0x-prefixed 64-bit addresses only when
    # battery=1/ufs_shared= are asked for, hakios_kver=) and the arguments above last.
    PREFIX = 'debug=0x8 battery_shared_pa=0x0000000000000000 ufs_shared_pa=0x0000000000000000 hakios_kver=8796.121.2 '
    total = len(PREFIX) + len(cmdline) + 1
    print(f'[*] kernel cmdline {total} of {LIMIT} characters', flush=True)
    if total > LIMIT:
        sys.exit(f'[!] the payload cmdline overflows the kernel\'s {LIMIT} characters by {total - LIMIT}: '
                 f'reduce it, or the end of it (soc_* addresses) is silently lost')

    payload = os.path.join(OUT_DIR, 'payload.bin')
    run(f'python3 {KERNEL_WS}/xnuloader/mkpayload.py -o {payload} '
        f'loader.efi={OUT_DIR}/loader.efi kernel={OUT_DIR}/hakios-kernel '
        f'ramdisk={KERNEL_WS}/userland/root.apfs --usb --cmdline "{cmdline}"')
    run(f'fastboot stage {payload}')
    run('fastboot oem run')

    fd, port = None, None
    deadline = time.time() + 90
    while time.time() < deadline and fd is None:
        for p in sorted(glob.glob('/dev/ttyACM*')):
            try:
                fd, port = os.open(p, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY), p
                configure(fd)
            except OSError:
                fd = None
            if fd is not None:
                break
        if fd is None:
            time.sleep(0.3)
    if fd is None:
        sys.exit('no CDC console appeared')

    print(f'[*] watching {port}, log: {LOG}', flush=True)
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    sent_scan = sent_status = False
    start = time.time()
    with open(LOG, 'wb') as log:
        while time.time() - start < int(os.environ.get('HAKIOS_CAPTURE_SECS', '240')):
            r, _, _ = select.select([fd], [], [], 0.3)
            if r:
                data = os.read(fd, 4096)
                if data:
                    log.write(data)
                    log.flush()
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    if not sent_status and b'root@hakios' in data:
                        os.write(fd, b'wifi status\r\n')
                        sent_status = True
                    elif not sent_scan and b'RADIO IS ACTIVE' in data:
                        os.write(fd, b'wifi scan\r\n')
                        sent_scan = True
    os.close(fd)


if __name__ == '__main__':
    main()
