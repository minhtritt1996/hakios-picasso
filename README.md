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
| **Boot Chain** | Stock XBL -> Mu-Silicium UEFI (`YinCQL/uefi_picasso`) -> HakiOS XnuLoader -> Darwin Kernel 22.5.0 |

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
| **Wi-Fi** | **Partial** | `wifid` daemon active under `launchd`; Picasso modem-hosted WLAN (`hakios.picasso_wlan.cpp`) via MPSS PAS 4. |
| **Buttons** | **Missing** | Power & Volume keys via PMIC SPMI arbiter pending bringup. |
| **Audio / ADSP** | **Missing** | Audio DSP bringup pending. |
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

### Running (RAM-Boot Only)
> [!IMPORTANT]
> **Safety Invariant:** HakiOS runs entirely from RAM. It does not overwrite physical partitions on the device.

```bash
# Boot Mu-Silicium UEFI into RAM
fastboot stage payload.bin
fastboot oem run
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
