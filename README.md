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
| **Display** | **Partial** | UEFI GOP framebuffer; `MiniIOMobileFramebuffer` registers `IOPlatformDevice` nub `disp0` under `/arm-io` in `gIODTPlane` with physical DeviceMemory. |
| **Touch** | **Partial** | Driver `/dev/hakios-touch` communicates with `backboardd` `IOHIDEventSystem`; Novatek NT36672C SPI initialization and firmware staging. |
| **USB** | **Works** | `xnuloader` Fastboot (`product: xnuloader`), OEM paniclog preserve (512 KB PRAM staging across warm reboots), CDC serial console (`/dev/ttyACM0`). |
| **Storage** | **Partial** | Loader surveys UFS 2.1 (109 partitions across 6 LUNs); root filesystem mounted from APFS RAM disk; Apple embedded panic log preserved in `logdump` partition. |
| **GPU** | **Partial** | Firmware (`a620_zap.mdt`, `a630_sqe.fw`, `a650_gmu.bin`) extracted; TrustZone SCM PAS authentication and CP secure mode unlock (`RBBM_SECVID_TRUST_CNTL 0x1 -> 0x3`) in progress. |
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
│   │   ├── hakios.novatek.c    # Novatek NT36672C SPI touch driver
│   │   └── hakios.novatek_fw.h # Novatek firmware array
│   └── wifi/
│       ├── conf                # driver PICASSO_WLAN declaration
│       └── hakios.picasso_wlan.cpp # Picasso WLAN driver via MPSS PAS
├── soc.args                    # SoC hardware MMIO addresses extracted from device tree
├── pull-firmware.sh            # Pulls proprietary vendor firmware from device over adb
├── tools/
│   ├── build-bootimg.py        # Packs BootShim + Mu-Silicium FD + XnuLoader into boot payload
│   └── pull-picasso-fdt.sh     # Dumps device tree from Android /sys/firmware/fdt
└── patches/                    # Historical bringup fix series and bootloader patches
```

---

## 4. Building and Running

### Prerequisites
* A Linux workstation with Python 3, bash, and fastboot.
* The common HakiOS repository ([`hakios-xnu-kernel`](https://github.com/oosexgang/hakios-xnu-kernel)).
* Redmi K30 5G with unlocked bootloader.

### Syncing into HakiOS
In the root of the common HakiOS repository (`hakios-u`):
```bash
# Clone or link this repository into devices/picasso
python3 tools/hakios-device.py sync picasso
# Check repository integrity
python3 tools/hakios-device.py check picasso
```

### Building the Kernel
```bash
make picasso_defconfig
make kernel
```

### Building HakiOS UEFI Firmware
```bash
# Apply HakiOS patches and install picasso platform
cd /home/tritm/hakios-uefi
./apply.sh picasso

# Build UEFI image
cd /home/tritm/hakios-mu
source .venv/bin/activate
python3 build_uefi.py -d picasso -r RELEASE
# Output: Build/picassoPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd
```

### Development Loop (Staying in xnuloader)
> [!IMPORTANT]
> **Safety Invariant:** HakiOS boots and runs entirely from RAM. It never writes to physical device storage partitions without explicit authorization.

1. **Boot into xnuloader (Fastboot Listener Mode):**
   ```bash
   # Package boot image with loader.efi and --usb flag
   python3 /home/tritm/hakios-u/xnuloader/mkpayload.py -o /tmp/loader-payload.bin \
       loader.efi=/home/tritm/hakios-u/xnuloader/loader.efi --usb

   python3 tools/build-bootimg.py --xnup /tmp/loader-payload.bin -o /tmp/loader-picasso.img
   fastboot boot /tmp/loader-picasso.img
   ```
   The phone boots into `xnuloader` and stays resident in USB Fastboot loop (`fastboot getvar product` -> `xnuloader`).

2. **Stage System Ramdisk (APFS) once:**
   ```bash
   ./xl ramdisk userland/system.apfs
   ```

3. **Stage and Run Kernel iteratively:**
   ```bash
   ./xl run
   # Or manually:
   # fastboot stage build/hakios-kernel && fastboot oem run
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
