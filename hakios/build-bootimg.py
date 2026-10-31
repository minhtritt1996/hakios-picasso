#!/usr/bin/env python3
"""
Dong goi boot image HakiOS cho picasso (Android boot image v1, gzip kernel + DTB).

  kernel part = gzip(BootShim.bin + SILICIUM_UEFI.fd + XNUP container) + picasso.dtb

BootShim (Mu-Silicium, da patch boi HakiOS) chep FD toi FD_BASE va container XNUP
toi 0xF0000000; BootManagerLib/XnuLoader LoadImage entry "loader.efi" trong container.

usage: build-bootimg.py [-o OUT] [--mu MU] [--hakios HAKIOS] [--xnup XNUP] [--dtb DTB]
  MU      cay Mu-Silicium da build (BootShim/BootShim.bin, Build/picassoPkg/.../FV)
  HAKIOS  cay hakios-xnu-kernel (cho Resources/ramdisk + mkbootimg.py khong co: lay tu UEFI)
  XNUP    container (mac dinh: build tai cho tu loader.efi + prebuilt/hakios-kernel)
"""
import argparse, gzip, os, subprocess, sys

FD_BASE, FD_SIZE = 0x9F100000, 0x200000

ap = argparse.ArgumentParser()
ap.add_argument('-o', '--output', default='/tmp/hakios-picasso-boot.img')
ap.add_argument('--mu', default='/home/tritm/hakios-mu')
ap.add_argument('--hakios', default='/home/tritm/hakios-u')
ap.add_argument('--uefi', default='/home/tritm/uefi_picasso')
ap.add_argument('--xnup', default='')
ap.add_argument('--dtb', default='')
ap.add_argument('--cmdline', default='debug=0x8 hv_wdt_pet=1 cpus=1')
ap.add_argument('--no-usb', action='store_true')
args = ap.parse_args()

shim = os.path.join(args.mu, 'BootShim/BootShim.bin')
fd = os.path.join(args.mu, 'Build/picassoPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd')
dtb = args.dtb or os.path.join(args.uefi, 'Resources/DTBs/picasso.dtb')
mkbootimg = os.path.join(args.uefi, 'Resources/Scripts/mkbootimg.py')
ramdisk = os.path.join(args.uefi, 'Resources/ramdisk')

for p in (shim, fd, dtb, mkbootimg):
    if not os.path.isfile(p):
        sys.exit(f'build-bootimg.py: {p} not found')

shim_data = open(shim, 'rb').read()
fd_data = open(fd, 'rb').read()
assert len(fd_data) == FD_SIZE, f'FD is {len(fd_data)} bytes, expected {FD_SIZE}'

if args.xnup:
    xnup = args.xnup
else:
    xnup = '/tmp/xnup-picasso.bin'
    cmd = [sys.executable, os.path.join(args.hakios, 'xnuloader/mkpayload.py'), '-o', xnup,
           f'loader.efi={os.path.join(args.hakios, "xnuloader/loader.efi")}',
           f'kernel={os.path.join(args.hakios, "prebuilt/hakios-kernel")}',
           '--cmdline', args.cmdline]
    if not args.no_usb:
        cmd.append('--usb')
    subprocess.check_call(cmd)
xnup_data = open(xnup, 'rb').read()
assert xnup_data[:4] == b'XNUP', 'not an XNUP container'

blob = shim_data + fd_data + xnup_data
blob += b'\0' * (-len(blob) % 16)
dtb_data = open(dtb, 'rb').read()

kernel = args.output + '.kernel~'
open(kernel, 'wb').write(gzip.compress(blob, 6) + dtb_data)
try:
    subprocess.check_call([sys.executable, mkbootimg, '--kernel', kernel, '--ramdisk', ramdisk,
                           '--header_version', '1', '-o', args.output])
finally:
    os.unlink(kernel)
print(f'{args.output}: {os.path.getsize(args.output)} bytes (shim {len(shim_data)}, fd {len(fd_data)}, '
      f'xnup {len(xnup_data)}, uncompressed blob {len(blob)})')
