# hakios-picasso: Xiaomi Redmi K30 5G (`picasso`) and Qualcomm Snapdragon 765G (`sm7250`)

Official HakiOS device port repository for the **Xiaomi Redmi K30 5G** (`picasso`), powered by Qualcomm Snapdragon 765G (`sm7250`) running Apple Darwin Kernel 22.5.0 (XNU) and iOS 16.5 userland.

This repository holds the device configuration (`device.conf`, `configs/`), the SoC layer (`soc/`), device-specific drivers (`drivers/`), and tools required to build and run HakiOS on `picasso`.

---

## 1. Hardware Specifications

| Component | Hardware Details |
|---|---|
| **Device Model** | Xiaomi Redmi K30 5G (`picasso`) |
| **SoC** | Qualcomm Snapdragon 765G (`sm7250` / `lito`) |
| **CPU Architecture** | 8 Cores ARMv8-A (1x Kryo 475 Prime @ 2.4 GHz + 1x Gold @ 2.2 GHz + 6x Silver @ 1.8 GHz) |
| **GPU** | Qualcomm Adreno 620 (API: Metal / Vulkan / Turnip) |
| **Display Panel** | 6.67" IPS LCD, 1080 x 2400 pixels, 120Hz refresh rate |
| **Touch Controller** | Novatek NT36672C on GENI SE7 SPI (`i2c@988000` / SPI bus) |
| **Storage** | 64GB / 128GB / 256GB UFS 2.1 (109 partitions, 6 LUNs) |
| **WLAN / BT** | Qualcomm WCN3990 via MPSS PAS 4 |
| **Audio** | Qualcomm WCD938x codec over SoundWire, NXP TFA9874 amplifier on I2C-2 (0x34) & Secondary MI2S |
| **Boot Chain** | Stock XBL -> HakiOS UEFI (Mu-Silicium `picassoPkg`) -> HakiOS XnuLoader (`loader.efi`) -> Darwin Kernel 22.5.0 |

---

## 2. Current Implementation Status

All statuses reflect verified, reproducible runs on physical hardware:

| Subsystem | State | Implementation Details |
|---|---|---|
| **CPU / Kernel** | **Works** | Apple Darwin 22.5.0 (XNU-8796.121.2) boots SMP 4 cores; passes 36/36 Darwin userland test suite; Apple XPC `launchd`, `backboardd`, `wifid`, and `SpringBoard` running stable. |
| **Display** | **Partial** | UEFI GOP framebuffer; `MiniIOMobileFramebuffer` initialized early at `GenericARMPE::start()`, publishes nub `disp0` under `/arm-io` in `gIODTPlane`; CADisplay and CAWindowServerDisplay matched (1125x2436 on 1080x2400 panel); WindowServer unblank and display update hooks installed. |
| **Touch** | **Partial** | Driver `/dev/hakios-touch` communicates with `backboardd` `IOHIDEventSystem`; Novatek NT36672C SPI initialization and firmware staging. |
| **USB** | **Works** | `xnuloader` Fastboot (`product: xnuloader`), OEM paniclog preserve (512 KB PRAM staging across warm reboots), CDC serial console (`/dev/ttyACM0`). |
| **Storage** | **Partial** | Loader surveys UFS 2.1 (109 partitions across 6 LUNs); root filesystem mounted from APFS RAM disk; Apple embedded panic log preserved in `logdump` partition. |
| **GPU** | **Partial** | Firmware (`a620_zap.mdt`, `a630_sqe.fw`, `a650_gmu.bin`) authenticated via PAS; Step 22 Adreno 2D Blitter draws test color bands on screen; OpenGL ES 3.2 Mesa 25.0.7 (`FD620`) loaded by CoreAnimation in `backboardd`. |
| **Wi-Fi / Network** | **Works** | Qualcomm WCN3990 via MPSS PAS 4 (`hakios.picasso_wlan.cpp`); physical over-the-air 802.11 beacon scanning, WPA2-PSK 4-way handshake, TX/RX Data Plane (`picasso_wlan_transmit`), and userland BPF DHCP client lease (`10.14.42.115` on `en0`). |
| **Audio / ADSP** | **Works** | Qualcomm ADSP PAS / Secondary MI2S / APR audio stream; NXP TFA9874 amplifier on I2C @ 0x34 (reset GPIO58); Qualcomm WCD938x codec over SoundWire Master Port 2 / Bolero 1.2 TX macro / APR capture stream; `/dev/hakios-audio` and `/dev/hakios-mic` active in Darwin userland. Verified audible playback and room sound recording on hardware. |
| **Bluetooth** | **Works** | Qualcomm WCN3990 via QUPV3 SE5 UART @ 0x894000 (`hakios.picasso_bt.c`); in-band 2400/115200 baud power sequence, standard 4-byte packing, devfs character device `/dev/hakios-bt` (0666), userland `bt` tool (`bt status`, `bt reset`, `bt addr`). Verified live on hardware. |
| **Buttons** | **Missing** | Power & Volume keys via PMIC SPMI arbiter pending bringup. |
| **Battery / PMIC** | **Missing** | Battery gauge reading via ADSP pending. |

---

## 3. Repository Structure

This repository follows the official [HakiOS Porting Architecture](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/repositories.md):

```
hakios-picasso/
├── device.conf                 # Device definition and subsystem status matrix
├── configs/
│   ├── picasso_defconfig       # Includes common_defconfig, sm7250.config, picasso.config
│   └── picasso.config          # Picasso-specific kernel configuration symbols and arguments
├── soc/
│   ├── soc.conf                # Snapdragon 765G (SM7250) SoC family declaration
│   └── configs/sm7250.config   # SM7250 SoC configuration (cores, clocks, socinfo)
├── drivers/
│   ├── bluetooth/
│   │   ├── conf                # driver PICASSO_BT declaration
│   │   └── hakios.picasso_bt.c # Picasso WCN3990 Bluetooth UART driver (/dev/hakios-bt)
│   ├── input/
│   │   ├── conf                # driver NOVATEK declaration
│   │   └── hakios.novatek.c    # Novatek NT36672C SPI touch driver + embedded fw01
│   └── wifi/
│       ├── conf                # driver PICASSO_WLAN declaration
│       └── hakios.picasso_wlan.cpp # Picasso WLAN driver via MPSS PAS
├── uefi/
│   ├── apply.sh                # Delegates to common uefi/apply.sh picasso
│   ├── export.sh               # Exports picassoPkg, picasso.toml, picasso.dtb from Mu-Silicium
│   ├── README.md               # UEFI build and vendor binary extraction instructions
│   └── files/                  # picassoPkg platform, picasso.toml, picasso.dtb
├── prebuilt/
│   ├── BootShim.bin            # Mu-Silicium BootShim with HakiOS XNUP payload support
│   ├── device-tree.dtb         # Xiaomi Redmi K30 5G device tree
│   ├── mkbootimg.py            # Android boot image v1/v2 packager
│   └── ramdisk                 # Minimal dummy ramdisk for Android boot header
├── soc.args                    # SoC hardware MMIO addresses extracted from device tree
├── pull-firmware.sh            # Pulls proprietary vendor firmware from device over adb
├── pull-firmware-root.sh       # Pulls proprietary vendor firmware via root staging over adb
├── pull-soc.sh                 # Pulls /sys/firmware/fdt over adb and generates soc.args
├── build.sh                    # Clean end-to-end build script (clones upstream/ -> build/, applies patches, builds all)
├── tools/
│   ├── build-bootimg.py        # Packs BootShim + Mu-Silicium FD + XnuLoader into boot payload
│   ├── boot_gui_full.py        # Full GUI boot & CDC console monitor script
│   ├── fetch_data.py           # USB bulk upload helper for xnuloader
│   ├── raw2bmp.py              # Converts 1080x2400 BGRA raw framebuffer dumps to BMP
│   └── raw_download.py         # USB bulk download helper for xnuloader
├── AGENT.md                    # Standard porting & verification workflow for AI agents
├── note.md                     # Technical debug notes & bringup status
├── patches/                    # Clean, reproducible 6-patch downstream series against upstream HakiOS
│   ├── UPSTREAM                # Exact upstream repository URLs and base commit hashes
│   ├── series                  # Ordered patch list (0001..0006)
│   └── 000*.patch              # Split by subsystem: XNU core, USB CDC, Drivers, Userland, Loader/Configs, Metal
├── upstream/                   # (gitignored) Read-only pristine upstream repositories (NEVER modified)
└── build/                      # (gitignored) Isolated build workspaces and build/out/ artifacts
```

---

## 4. Building and Running

### Prerequisites
* A Linux workstation with Python 3, bash, Clang 15/18, and fastboot.
* Read-only upstream repositories cloned under `./upstream/` (`hakios-xnu-kernel`, `hakios-uefi`, `hakios-metal`, `xnu-8796.121.2`, `hfs-627.100.6`, `Mu-Silicium`).
* Redmi K30 5G with unlocked bootloader.

### Automated Clean Build (Kernel + Loader + Userland + UEFI)
Use `./build.sh` to clone `./upstream/` into `./build/`, apply all patches from `./patches/series`, and build everything without ever modifying `./upstream/`:
```bash
# Build all targets (kernel, loader, userland APFS images, UEFI firmware, and boot-picasso.img)
./build.sh all --clean

# Or build individual targets incrementally:
./build.sh kernel
./build.sh userland
./build.sh uefi
```
All build outputs are placed in `./build/out/`:
* `build/out/hakios-kernel` — Darwin 22.5.0 ARM64 kernel
* `build/out/loader.efi` — HakiOS UEFI XnuLoader
* `build/out/root.apfs` — Small boot APFS container (`init`, `LaunchDaemons`, `libhakios*.dylib`)
* `build/out/system.apfs` — iOS 16.5 system root filesystem APFS container
* `build/out/SILICIUM_UEFI.fd` — HakiOS Mu-Silicium UEFI firmware image
* `build/out/payload.bin` — XNUP container (`loader.efi` + `hakios-kernel` + `root.apfs`)
* `build/out/boot-picasso.img` — Complete Android boot image for `fastboot boot`

### Syncing into an Existing HakiOS Common Tree
In the root of the common HakiOS repository (`hakios-xnu-kernel`):
```bash
# Clone or link this repository into devices/picasso
python3 tools/hakios-device.py sync picasso
# Check repository integrity
python3 tools/hakios-device.py check picasso
```

### Development Loop (Staying in xnuloader)
> [!IMPORTANT]
> **Safety Invariant:** HakiOS boots and runs entirely from RAM. It never writes to physical device storage partitions without explicit authorization.

1. **Boot into xnuloader (Fastboot Listener Mode):**
   ```bash
   # Boot the complete image directly into RAM
   fastboot boot build/out/boot-picasso.img

   # Or package a loader-only boot image with --usb flag:
   python3 build/hakios-xnu-kernel/xnuloader/mkpayload.py -o build/out/loader-payload.bin \
       loader.efi=build/out/loader.efi --usb
   python3 tools/build-bootimg.py --xnup build/out/loader-payload.bin -o build/out/loader-picasso.img
   fastboot boot build/out/loader-picasso.img
   ```
   The phone boots into `xnuloader` and stays resident in USB Fastboot loop (`fastboot getvar product` -> `xnuloader`).

2. **Stage System Ramdisk (APFS) once:**
   ```bash
   ./xl ramdisk build/out/system.apfs
   ```

3. **Stage and Run Kernel iteratively:**
   ```bash
   ./xl run
   # Or manually:
   # fastboot stage build/out/payload.bin && fastboot oem run
   ```

4. **Collect Logs & Screenshots:**
   ```bash
   ./xl log
   ./xl screenshot
   ```

---

## 5. Reference Repositories

This port draws technical architecture and register mappings from:
* **[YinCQL/UniXNU-picasso](https://github.com/YinCQL/UniXNU-picasso):** Reference UniXNU port for Redmi K30 5G (`picasso`), device tree nodes, and display initialization.
* **[silenzio111/UniXNU-picasso](https://github.com/silenzio111/UniXNU-picasso/commits/stage12/picasso-cpufreq-big/):** TrustZone Zap shader authentication (`a620_zap.mdt`), CP secure mode switching, and CPU frequency scaling.
* **[oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel):** Upstream HakiOS XNU Kernel tree.
* **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet):** Apple WindowServer, `CAWindowServer`, and `IOMobileFramebuffer` initialization reference.

---

## 6. License & Compliance

* Device configuration, tools, and scripts are licensed under the **MIT License**.
* Device drivers derived from XNU / IOKit are licensed under the **Apple Public Source License 2.0 (APSL-2.0)**.
* No proprietary Apple binaries or vendor firmware blobs are distributed in this repository. Firmware must be pulled by the end-user using `pull-firmware.sh`.
