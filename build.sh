#!/bin/bash
# HakiOS Picasso End-to-End Clean Build Script (Kernel + Loader + Userland + UEFI)
#
# Clones unmodified repositories from ./upstream/ into ./build/, applies the downstream
# patch series from ./patches/series, and builds:
#   1. XNU Kernel (hakios-kernel) & UEFI XnuLoader (loader.efi)
#   2. Darwin Userland & APFS RAM disks (init, libhakios*.dylib, root.apfs, system.apfs)
#   3. HakiOS Mu-Silicium UEFI Firmware (SILICIUM_UEFI.fd) & Android boot image (boot-picasso.img)
#
# INVARIANT: ./upstream/ is strictly read-only and NEVER modified by this script.
#
# Usage:
#   ./build.sh [all|kernel|userland|uefi] [--clean]

set -euo pipefail

PICASSO_DIR=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
WORKSPACE_ROOT=$(dirname "$PICASSO_DIR")
UPSTREAM_DIR="$PICASSO_DIR/upstream"
BUILD_DIR="$PICASSO_DIR/build"
OUT_DIR="$BUILD_DIR/out"
SHARED_HOST_WS="${SHARED_HOST_WS:-$WORKSPACE_ROOT/hakios-u}"
SHARED_MU_VENV="${SHARED_MU_VENV:-$WORKSPACE_ROOT/hakios-mu/.venv}"

TARGET="all"
CLEAN=0
for arg in "$@"; do
	case "$arg" in
		all|kernel|userland|uefi) TARGET="$arg" ;;
		--clean) CLEAN=1 ;;
		*) echo "Usage: $0 [all|kernel|userland|uefi] [--clean]" >&2; exit 1 ;;
	esac
done

verify_upstream_clean() {
	echo "==> Checking integrity of $UPSTREAM_DIR (must be 100% clean)..."
	local expected=(
		"hakios-xnu-kernel:96ffbc532e970f3540885755d457e401e04d4707"
		"hakios-uefi:687fa6c6a5a70f43184de92980e0a76eea5f2c4b"
		"hakios-metal:7025778b8c0bbb1a99793f1be06474782d50cdaf"
		"xnu-8796.121.2:c54f35ca767986246321eb901baf8f5ff7923f6a"
		"hfs-627.100.6:31d5ec238dfab485e78a9a30b8b92d13e8914f03"
		"Mu-Silicium:b363060778a1ebbc9a2b705f0a0a00cff432c009"
	)
	for item in "${expected[@]}"; do
		local repo="${item%%:*}"
		local sha="${item##*:}"
		[ -d "$UPSTREAM_DIR/$repo/.git" ] || { echo "ERROR: Missing upstream repo $UPSTREAM_DIR/$repo" >&2; exit 1; }
		local cur_sha
		cur_sha=$(git -C "$UPSTREAM_DIR/$repo" rev-parse HEAD)
		if [ "$cur_sha" != "$sha" ]; then
			echo "ERROR: $UPSTREAM_DIR/$repo HEAD is $cur_sha, expected $sha" >&2
			exit 1
		fi
		local dirty
		dirty=$(git -C "$UPSTREAM_DIR/$repo" status --porcelain)
		if [ -n "$dirty" ]; then
			echo "ERROR: $UPSTREAM_DIR/$repo has modified/untracked files:" >&2
			echo "$dirty" >&2
			exit 1
		fi
	done
	echo "    [OK] All 6 upstream repositories in $UPSTREAM_DIR are clean and at exact expected commits."
}

prepare_workspaces() {
	if [ "$CLEAN" -eq 1 ]; then
		echo "==> Removing previous $BUILD_DIR..."
		rm -rf "$BUILD_DIR"
	fi
	mkdir -p "$BUILD_DIR" "$OUT_DIR"

	# 1. hakios-xnu-kernel build workspace
	if [ ! -d "$BUILD_DIR/hakios-xnu-kernel" ]; then
		echo "==> Cloning $UPSTREAM_DIR/hakios-xnu-kernel into $BUILD_DIR/hakios-xnu-kernel..."
		git clone -q "$UPSTREAM_DIR/hakios-xnu-kernel" "$BUILD_DIR/hakios-xnu-kernel"
		git -C "$BUILD_DIR/hakios-xnu-kernel" checkout -q 96ffbc532e970f3540885755d457e401e04d4707

		echo "==> Applying hakios-picasso kernel/driver/userland/loader patches (0001..0005)..."
		for p in \
			0001-xnu-picasso-smp-timer-iomap-display.patch \
			0002-xnu-picasso-usb-cdc-console-driver.patch \
			0003-drivers-picasso-adreno620-audio-ufs-mpss-buttons.patch \
			0004-userland-picasso-init-inject-springboard.patch \
			0005-loader-configs-and-tools-picasso.patch; do
			echo "    + applying $p"
			git -C "$BUILD_DIR/hakios-xnu-kernel" apply --whitespace=nowarn "$PICASSO_DIR/patches/$p"
		done

		ln -sfn "$PICASSO_DIR" "$BUILD_DIR/hakios-xnu-kernel/devices/picasso"

		echo "==> Linking prebuilt host toolchain (xnu-deps) and Darwin 16.5 sysimage..."
		mkdir -p "$BUILD_DIR/hakios-xnu-kernel/xnu-deps" "$BUILD_DIR/hakios-xnu-kernel/sysimage"
		cp -alf "$SHARED_HOST_WS/xnu-deps/." "$BUILD_DIR/hakios-xnu-kernel/xnu-deps/"
		for d in rootfs cryptex cryptex-app "20F66__iPhone10,3_6"; do
			if [ -e "$SHARED_HOST_WS/sysimage/$d" ]; then
				cp -alf "$SHARED_HOST_WS/sysimage/$d" "$BUILD_DIR/hakios-xnu-kernel/sysimage/"
			fi
		done
		for f in system.files system.manifest var.skel .done-rootfs .done-cryptex .done-cryptex-app a2s.json; do
			if [ -e "$SHARED_HOST_WS/sysimage/$f" ]; then
				cp -a "$SHARED_HOST_WS/sysimage/$f" "$BUILD_DIR/hakios-xnu-kernel/sysimage/"
			fi
		done
		touch "$BUILD_DIR/hakios-xnu-kernel/sysimage/.done-rootfs"
		for d in "$SHARED_HOST_WS"/fw-*; do
			[ -d "$d" ] && ln -sfn "$d" "$BUILD_DIR/hakios-xnu-kernel/$(basename "$d")"
		done
		if [ -f "$SHARED_HOST_WS/xnuloader/identity.h" ]; then
			cp -a "$SHARED_HOST_WS/xnuloader/identity.h" "$BUILD_DIR/hakios-xnu-kernel/xnuloader/identity.h"
		fi
		if [ -f "$SHARED_HOST_WS/userland/obj/stage0" ]; then
			mkdir -p "$BUILD_DIR/hakios-xnu-kernel/userland/obj"
			cp -a "$SHARED_HOST_WS/userland/obj/stage0" "$BUILD_DIR/hakios-xnu-kernel/userland/obj/stage0"
		fi
		if [ -f "$SHARED_HOST_WS/userland/system.apfs" ]; then
			cp -alf "$SHARED_HOST_WS/userland/system.apfs" "$BUILD_DIR/hakios-xnu-kernel/userland/system.apfs"
			touch "$BUILD_DIR/hakios-xnu-kernel/userland/system.apfs"
		fi
	fi

	# 2. hakios-metal build workspace
	if [ ! -d "$BUILD_DIR/hakios-metal" ]; then
		echo "==> Cloning $UPSTREAM_DIR/hakios-metal into $BUILD_DIR/hakios-metal..."
		git clone -q "$UPSTREAM_DIR/hakios-metal" "$BUILD_DIR/hakios-metal"
		git -C "$BUILD_DIR/hakios-metal" checkout -q 7025778b8c0bbb1a99793f1be06474782d50cdaf
		echo "    + applying 0006-hakios-metal-clang15-constexpr-signbit.patch"
		git -C "$BUILD_DIR/hakios-metal" apply --whitespace=nowarn "$PICASSO_DIR/patches/0006-hakios-metal-clang15-constexpr-signbit.patch"
	fi

	# 3. hakios-uefi build workspace
	if [ ! -d "$BUILD_DIR/hakios-uefi" ]; then
		echo "==> Cloning $UPSTREAM_DIR/hakios-uefi into $BUILD_DIR/hakios-uefi..."
		git clone -q "$UPSTREAM_DIR/hakios-uefi" "$BUILD_DIR/hakios-uefi"
		git -C "$BUILD_DIR/hakios-uefi" checkout -q 687fa6c6a5a70f43184de92980e0a76eea5f2c4b
		mkdir -p "$BUILD_DIR/hakios-uefi/devices"
		ln -sfn "$PICASSO_DIR" "$BUILD_DIR/hakios-uefi/devices/picasso"
	fi
}

build_kernel_and_loader() {
	echo "========================================================================"
	echo "==> [1/3] Building XNU Kernel & XnuLoader from patched upstream..."
	echo "========================================================================"
	cd "$BUILD_DIR/hakios-xnu-kernel"

	python3 tools/hakios-device.py check picasso
	make picasso_defconfig
	python3 tools/hakios-config.py check

	if [ ! -d xnu-src/xnu-8796.121.2 ]; then
		echo "==> Constructing XNU source tree via xnu/apply.sh (from $UPSTREAM_DIR/xnu-8796.121.2)..."
		XNU_URL="file://$UPSTREAM_DIR/xnu-8796.121.2" \
		HFS_URL="file://$UPSTREAM_DIR/hfs-627.100.6" \
			./xnu/apply.sh
	fi

	echo "==> Compiling XNU kernel (-j$(nproc))..."
	./tools/make-kernel.sh -j"$(nproc)"
	cp -f hakios-kernel "$OUT_DIR/hakios-kernel"

	echo "==> Compiling UEFI XnuLoader (loader.efi)..."
	./xnuloader/build.sh
	cp -f xnuloader/loader.efi "$OUT_DIR/loader.efi"
}

build_userland() {
	echo "========================================================================"
	echo "==> [2/3] Building Darwin Userland & APFS RAM Disks..."
	echo "========================================================================"
	cd "$BUILD_DIR/hakios-xnu-kernel"

	HAKIOS_METAL="$BUILD_DIR/hakios-metal" GL=1 ./userland/build.sh
	cp -f userland/root.apfs "$OUT_DIR/root.apfs"
	[ -f userland/system.apfs ] && cp -al -f userland/system.apfs "$OUT_DIR/system.apfs"
}

build_uefi() {
	echo "========================================================================"
	echo "==> [3/3] Building HakiOS Mu-Silicium UEFI Firmware for Picasso..."
	echo "========================================================================"
	local MU_BUILD="$BUILD_DIR/Mu-Silicium"
	if [ ! -d "$MU_BUILD" ]; then
		echo "==> Preparing isolated Mu-Silicium build tree in $MU_BUILD..."
		cp -a --reflink=auto "$UPSTREAM_DIR/Mu-Silicium" "$MU_BUILD"
		git -C "$MU_BUILD" apply --whitespace=nowarn "$BUILD_DIR/hakios-uefi/hakios.patch"
		cp -r "$BUILD_DIR/hakios-uefi/files/." "$MU_BUILD/"
		python3 "$BUILD_DIR/hakios-uefi/branding/banner.py" "$MU_BUILD/Silicon/Silicium/SiliciumPkg/Include/Resources"
		cp -r "$PICASSO_DIR/uefi/files/." "$MU_BUILD/"
	fi

	cd "$MU_BUILD"
	# Activate Python venv with edk2-pytool-library / edk2-pytool-extensions
	if [ -f "$SHARED_MU_VENV/bin/activate" ]; then
		# shellcheck disable=SC1090
		source "$SHARED_MU_VENV/bin/activate"
	fi
	python3 build_uefi.py -d picasso -r RELEASE
	local FD="$MU_BUILD/Build/picassoPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd"
	[ -f "$FD" ] || { echo "ERROR: UEFI FD not produced at $FD" >&2; exit 1; }
	cp -f "$FD" "$OUT_DIR/SILICIUM_UEFI.fd"
	cp -f "$FD" "$PICASSO_DIR/prebuilt/SILICIUM_UEFI.fd"

	if [ -f "$OUT_DIR/loader.efi" ] && [ -f "$OUT_DIR/hakios-kernel" ] && [ -f "$OUT_DIR/root.apfs" ]; then
		echo "==> Packing XNUP payload and boot-picasso.img..."
		local ARGS
		ARGS=$(cd "$BUILD_DIR/hakios-xnu-kernel" && python3 tools/hakios-config.py args)
		python3 "$BUILD_DIR/hakios-xnu-kernel/xnuloader/mkpayload.py" -o "$OUT_DIR/payload.bin" \
			loader.efi="$OUT_DIR/loader.efi" \
			kernel="$OUT_DIR/hakios-kernel" \
			ramdisk="$OUT_DIR/root.apfs" \
			--cmdline "$ARGS"
		python3 "$PICASSO_DIR/tools/build-bootimg.py" \
			--mu "$MU_BUILD" \
			--uefi "$MU_BUILD" \
			--xnup "$OUT_DIR/payload.bin" \
			-o "$OUT_DIR/boot-picasso.img"
	fi
}

verify_upstream_clean
prepare_workspaces

case "$TARGET" in
	kernel)
		build_kernel_and_loader
		;;
	userland)
		build_userland
		;;
	uefi)
		build_uefi
		;;
	all)
		build_kernel_and_loader
		build_userland
		build_uefi
		;;
esac

verify_upstream_clean

echo "========================================================================"
echo "==> BUILD COMPLETE! Output artifacts in $OUT_DIR:"
ls -lh "$OUT_DIR"
echo "========================================================================"
