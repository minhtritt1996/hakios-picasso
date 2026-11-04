## Firmware Infos

- **Device:** Redmi K30 5G (picasso)
- **Codename:** picasso
- **SoC:** Qualcomm Snapdragon 765G (SM7250)

## Vendor Proprietary Binaries

In accordance with HakiOS provenance rules and licensing restrictions, proprietary Qualcomm and Xiaomi UEFI DXE drivers (QcomPkg, MiPkg, RawFiles) extracted from `xbl` / `xbl_config` partitions are **NOT** stored in this repository.

To build the UEFI firmware locally:
1. Extract vendor DXE drivers using [UEFIReader](https://github.com/WOA-Project/UEFIReader) from your phone's `xbl`/`xbl_config` partition or official stock ROM.
2. Place the extracted driver directories (`QcomPkg`, `MiPkg`, `RawFiles`) into `Binaries/picasso/` in your Mu-Silicium build tree or working directory.
3. Build using:
   ```sh
   python3 build_uefi.py -d picasso -r RELEASE
   ```
