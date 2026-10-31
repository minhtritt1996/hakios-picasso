# HakiOS on the Redmi K30 5G (picasso, SM7250)

This repository holds only the work done to bring [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel)
— Apple's open-source Darwin kernel (XNU) on a Qualcomm phone — up on the Redmi K30 5G.

The ios-sweet port this work started from lives on the `agy` branch and is untouched.

| Path | What |
|---|---|
| `hakios/README.md` | the handoff: what runs on the phone, what does not, and every number measured |
| `hakios/patches/` | the fix series, in the order they were found (see the table in the README) |
| `hakios/quick.sh` | the iteration loop: wait for the loader, optionally upload the 2.3 GB userland, run, fetch the log |
| `hakios/phone-darwin.sh`, `hakios/bringup-run.sh`, `hakios/fast-run.sh`, `hakios/xl-run.sh` | the same with different steps (flash a boot image, kernel only, stage a payload) |
| `hakios/read-logdump.sh` | read the loader's log of the last run from the phone's `logdump` partition (recovery) |
| `hakios/build-bootimg.py` | pack BootShim + Mu-Silicium FD (with HakiOS's XnuLoader) + an XNUP container + the DTB into a boot image |
| `hakios/pull-picasso-fdt.sh` | pull the phone's own device tree and SoC data (`hakios/picasso/soc.args`) |
| `hakios/picasso-device/` | the device repository of the port (the same content as `hakios-device-picasso`) |
| `hakios/upstream-question.md` | the questions for HakiOS's authors, with the measurements behind them |

The upstream work (the platform entry for picasso, two kernel fixes, the launchd plist permissions) is in the fork
`minhtritt1996/hakios-xnu-kernel`, branch `picasso`, and the device repository is
`minhtritt1996/hakios-device-picasso`.
