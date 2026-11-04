---
name: hakios-picasso-porting
description: >-
  Standardized workflow for building, patching, testing on hardware via RAM-boot,
  running HakiOS CI checks, and rebasing upstream updates in the hakios-picasso
  (Xiaomi Redmi K30 5G / SM7250) repository. Activate this skill whenever building
  the kernel/userland/UEFI, modifying drivers or XNU/userland patches, or debugging
  hardware subsystems on picasso.
---

# HakiOS Picasso Porting & Build Runbook

Use this skill to execute the canonical patch-first and driver-first development workflow in `hakios-picasso`.

## 1. Pre-Flight Checklist

1. Read [note.md](../../../note.md) at the repository root to review the latest hardware test results, active root-cause analysis, and next-step checklist.
2. Verify that `upstream/` is untouched and passes integrity checks:
   ```bash
   ./build.sh check
   ```

---

## 2. Where to Make Changes (Driver-First & Patch-First Architecture)

| Change Type | Where to Edit | How to Persist |
|---|---|---|
| **Picasso-specific driver** (Touch, BT, Wi-Fi, etc.) | Directly in `drivers/<subsystem>/` and `drivers/<subsystem>/conf` | Tracked directly in git (`drivers/`) |
| **SoC / Device config or MMIO args** | `soc/configs/sm7250.config`, `configs/picasso.config`, `soc.args` | Tracked directly in git |
| **XNU Kernel Core** (`osfmk`, `bsd`, `iokit`, `MiniIOMobileFramebuffer`) | In `build/hakios-xnu-kernel/xnu-src/xnu-8796.121.2/` or `build/hakios-xnu-kernel/xnu/files/` | Run `./build.sh export-patches` -> updates `patches/0001-*.patch` & `0002-*.patch` |
| **Common HakiOS Drivers** (`adreno`, `sound`, `ufs`, `mpss`) | In `build/hakios-xnu-kernel/drivers/<subsystem>/` | Run `./build.sh export-patches` -> updates `patches/0003-*.patch` |
| **Darwin Userland** (`init.c`, `inject.c`, `inject_sb.c`, `build.sh`) | In `build/hakios-xnu-kernel/userland/` | Run `./build.sh export-patches` -> updates `patches/0004-*.patch` |
| **Loader / Common Configs / Tools** (`xnuloader/loader.c`, `manifest.conf`) | In `build/hakios-xnu-kernel/xnuloader/` or `configs/` | Run `./build.sh export-patches` -> updates `patches/0005-*.patch` |
| **HakiOS Metal** (`hakios-metal`) | In `build/hakios-metal/` | Run `./build.sh export-patches` -> updates `patches/0006-*.patch` |

---

## 3. Step-by-Step Build & Verification Procedure

### Step 1: Incremental Build During Development
After editing a driver in `drivers/` or experimenting in `build/hakios-xnu-kernel/`:
```bash
# Rebuild kernel and loader.efi into build/out/
./build.sh kernel

# Rebuild userland dylibs, init, and root.apfs into build/out/
./build.sh userland

# Rebuild Mu-Silicium UEFI firmware and boot-picasso.img into build/out/
./build.sh uefi
```

### Step 2: Hardware RAM-Boot Testing (Never Flash UFS)
With the phone connected in `xnuloader` Fastboot mode (`fastboot devices` -> `xnuloader`):
```bash
# Run full GUI boot & CDC serial console monitor (/dev/ttyACM0) using build/out/ artifacts:
python3 tools/boot_gui_full.py
```
When isolating a specific subsystem, always disable unrelated drivers via kernel command-line arguments:

| Order | Subsystem | Isolated Boot-Args | Done Criteria |
|---|---|---|---|
| **1** | **SoC Core** (`CPUFREQ`, `THERMAL`, `SOCINFO`) | `cpus=8 small_cores_after=0` | Reads SoC ID, thermal sensors, and CPU clocks cleanly. |
| **2** | **Input** (`BUTTONS`, `NOVATEK`) | `buttons=1 touchscreen=1` | Hardware keys and touch events reach `backboardd` `IOHIDEventSystem`. |
| **3** | **Storage** (`UFS`) | `ufs=2 ufs_native=1` | Enumerates UFS 2.1 GPT partitions read-only. |
| **4** | **GPU & Display GUI** (`ADRENO` + `MiniIOMobileFramebuffer`) | `wifi=0 modem=0 adsp=0 sound=0 mic=0 bt=0 adreno=22 gpu=1 adreno_keep=1 ca_vbl=1` | Adreno 620 Step 22 passes; `backboardd` initializes GLES 3.2 (`FD620`); `SpringBoard` checks in and swaps frames. |
| **5** | **Audio & ADSP** (`ADSP`, `SPEAKER`, `SOUND`, `MIC`) | `adsp=1 sound=4 mic=4` | ADSP PAS 1 boots, TFA9874 playback & WCD938x capture verified. |
| **6** | **Wi-Fi** (`PICASSO_WLAN`) | `wifi=1` | MPSS PAS 4 boots, WLFW QMI attaches, WPA2-PSK & DHCP on `en0`. |
| **7** | **Bluetooth** (`PICASSO_BT`) | `bt=1` | QUPV3 SE5 UART @ `0x894000` attaches `/dev/hakios-bt`. |

### Step 3: Export Patches & Verify Clean Build
Once a change in `build/` is verified on hardware:
1. Export all changes into `patches/0001..0006`:
   ```bash
   ./build.sh export-patches
   ```
2. Run HakiOS CI & provenance checks:
   ```bash
   ./build.sh check
   ```
3. Verify a complete clean build from `upstream/` + `patches/`:
   ```bash
   ./build.sh all --clean
   ```
4. Update [device.conf](../../../device.conf) and [note.md](../../../note.md) before committing.

---

## 4. Rebasing onto New Upstream Releases

When pulling newer commits from `oosexgang/hakios-xnu-kernel`, `oosexgang/hakios-uefi`, or `oosexgang/hakios-metal`:
1. Fetch and checkout the new commit inside `upstream/<repo>` (leaving its working tree 100% clean).
2. Update the commit hash in [patches/UPSTREAM](../../../patches/UPSTREAM) and `verify_upstream_clean()` / `prepare_workspaces()` in [build.sh](../../../build.sh).
3. Run `./build.sh all --clean` to test applying `patches/series` on the new upstream base, resolve any patch context shifts in `build/`, and run `./build.sh export-patches`.
