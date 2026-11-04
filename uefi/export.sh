#!/bin/bash
# Save the picasso platform from Mu-Silicium/ into files/ here (uefi/export.sh runs this; HakiOS's own changes are uefi/'s).
set -e
cd "$(dirname "$(readlink -f "$0")")"
MU=${MU:-../../../Mu-Silicium}
rm -rf files
mkdir -p files/Resources/Configs files/Resources/DTBs files/Platforms/Xiaomi files/Binaries/picasso
cp -r $MU/Platforms/Xiaomi/picassoPkg files/Platforms/Xiaomi/
cp $MU/Resources/Configs/picasso.toml files/Resources/Configs/
cp $MU/Resources/DTBs/picasso.dtb files/Resources/DTBs/
[ -f $MU/Binaries/picasso/README.md ] && cp $MU/Binaries/picasso/README.md files/Binaries/picasso/
find files -name __pycache__ -prune -exec rm -rf {} +
echo "devices/picasso/uefi/export.sh: $(find files -type f | wc -l) files"
