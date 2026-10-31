# HakiOS on the Redmi K30 5G (picasso, SM7250)

This repository holds only the work done to bring [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel)
— Apple's open-source Darwin kernel (XNU) on a Qualcomm phone — up on the Redmi K30 5G.

The ios-sweet port this work started from lives on the `agy` branch and is untouched.

| Path | What |
|---|---|
| `HANDOFF.md` | the handoff: what runs on the phone, what does not, and every number measured |
| `patches/` | the fix series, in the order they were found (see the table in the README) |
| `quick.sh` | the iteration loop: wait for the loader, optionally upload the 2.3 GB userland, run, fetch the log |
| phone-darwin.sh`, bringup-run.sh`, fast-run.sh`, xl-run.sh` | the same with different steps (flash a boot image, kernel only, stage a payload) |
| read-logdump.sh` | read the loader's log of the last run from the phone's `logdump` partition (recovery) |
| `build-bootimg.py` | pack BootShim + Mu-Silicium FD (with HakiOS's XnuLoader) + an XNUP container + the DTB into a boot image |
| pull-picasso-fdt.sh` | pull the phone's own device tree and SoC data (`picasso/soc.args`) |
| `picasso-device/` | the device repository of the port (the same content as `hakios-device-picasso`) |
| upstream-question.md` | the questions for HakiOS's authors, with the measurements behind them |

The upstream work (the platform entry for picasso, two kernel fixes, the launchd plist permissions) is in the fork
`minhtritt1996/hakios-xnu-kernel`, branch `picasso`, and the device repository is
`minhtritt1996/hakios-device-picasso`.

## The repositories this work sits between

| Repository | What it is | Relationship |
|---|---|---|
| [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel) | HakiOS itself (XNU + its drivers, loader, tools, docs) | upstream |
| [minhtritt1996/hakios-xnu-kernel](https://github.com/minhtritt1996/hakios-xnu-kernel) `picasso` | our fork: the picasso platform entry in `xnuloader/loader.c`, the host build fixes, the two kernel fixes (`redzone`, the RAM-disk `IOBSD` wait), the `mfb=1` gate, the launchd plist permissions, and the `devices/manifest.conf` block | fork of the above; a pull request to upstream carries these commits |
| [minhtritt1996/hakios-device-picasso](https://github.com/minhtritt1996/hakios-device-picasso) | the device repository of the port (`device.conf` with what works on the phone, `configs/`, `soc/`, `soc.args`, `pull-firmware.sh`) | referenced from the fork's manifest at a locked revision; `picasso-device/` here is a copy of it |
| [oosexgang/hakios-uefi](https://github.com/oosexgang/hakios-uefi) | HakiOS's UEFI (Mu-Silicium + its XnuLoader) | applied on top of the picasso Mu-Silicium tree (`YinCQL/uefi_picasso`) to build the boot image |
| [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) / [YinCQL/uefi_picasso](https://github.com/YinCQL/uefi_picasso) | the UEFI firmware and its picasso platform | the `SILICIUM_UEFI.fd` that `build-bootimg.py` packs |
| this repository | the HakiOS-on-picasso work of the session: the measured numbers (`HANDOFF.md`), the fix series (`patches/`), the iteration scripts, the SoC data of the phone | standalone (not a fork); the ios-sweet port it started from is the `agy` branch |

## Where to work

| Path | What |
|---|---|
| `/home/tritm/hakios` | **this repository** — the port's own work: the handoff, the fix series, the scripts |
| `/home/tritm/hakios-u` | a clone of HakiOS itself (upstream `main`), the tree the kernel and the images are built in (`make kernel`, `userland/build.sh`) |
| `/home/tritm/hakios-fork` | the clone of the fork `minhtritt1996/hakios-xnu-kernel`, branch `picasso` (what a pull request carries) |
| `/home/tritm/hakios-device-picasso` | the clone of the device repository |
| `/home/tritm/hakios-mu` | Mu-Silicium (picasso) with HakiOS's XnuLoader applied; its `SILICIUM_UEFI.fd` is what the boot images pack |
| `/home/tritm/ios` | the ios-sweet port this work started from (its own branches; nothing of the HakiOS work is there any more) |

The scripts here take the HakiOS tree from `$HAKIOS` (default `/home/tritm/hakios-u`).
