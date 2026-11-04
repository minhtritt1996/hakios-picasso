# UEFI for picasso

The Redmi K30 5G's platform for HakiOS's UEFI (`uefi/` of the common repository: Mu-Silicium with HakiOS's changes and banner).
It uses `LitoPkg` (`SM7250`) as its SoC package.

What is here, in `files/`:

- `Platforms/Xiaomi/picassoPkg`: the memory map (`MemoryMap.inc`), ACPI tables, and driver list (`picasso.dsc`, `picasso.fdf`);
- `Resources/Configs/picasso.toml`, `Resources/DTBs/picasso.dtb` (the device tree for the UEFI build);
- `Binaries/picasso`: the vendor's UEFI drivers, **not tracked**: you make this directory yourself from your own phone
  (`uefi/README.md` says how); `files/Binaries/picasso/README.md` is kept: it documents the extraction and setup.

```sh
devices/picasso/uefi/apply.sh     # = uefi/apply.sh picasso
```

The build and the vendor's drivers: [uefi/README.md](../../../uefi/README.md).
