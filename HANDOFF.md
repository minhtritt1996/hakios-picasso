# HakiOS trên picasso (SM7250, Redmi K30 5G)

Nhánh `hakios` của repo này. **Không đụng nhánh `agy`** (đường ios-sweet/shim cũ vẫn nguyên).

## Vì sao

Repo `oosexgang/hakios-xnu-kernel` chạy **Darwin/iOS 16.5 userland có GUI thật** trên
Qualcomm (POCO X5 Pro, SM7325): lock screen, home, Control Center, app hệ thống, touch,
**GPU Adreno qua Mesa (OpenGL ES)**, Wi-Fi, camera, loa/mic, rung, `/private/var` bền,
Sileo, SSH. Xem `docs/` của họ; tóm tắt:

| Hạng mục | HakiOS (redwood/SM7325) |
|---|---|
| Firmware khởi động | Mu-Silicium UEFI **hoặc** U-Boot (open source) — `fastboot boot`, **RAM only** |
| Loader | `xnuloader` (loader.efi) — đọc payload, dựng memory map + DT, chạy XNU |
| Kernel | XNU (xnu-8796.121.2) + driver riêng (`drivers/`) bật bằng boot-arg |
| Userland | `system.apfs` (~2,4 GiB) **đẩy vào RAM** qua `xl ramdisk` (~80 s/lần vào loader) |
| Ghi UFS | chỉ khi `ufs_write=1 var_store=1` → partition `darwin` |

Vòng lặp phát triển kernel: `xl run` (upload payload 16 MiB = 0,55 s) — **không flash**,
không recovery, không power-cycle.

## Ta có sẵn gì

- **Mu-Silicium UEFI cho picasso**: `YinCQL/uefi_picasso` (đã boot thật trên máy này qua
  UniXNU-picasso) ⇒ đường **Silicium** dùng được ngay, không cần port U-Boot trước.
- **ADT/DT picasso** trong `shim/` + `tools/mkadt.py` (địa chỉ SoC đã kiểm chứng trên máy).
- Partition `darwin` + backup image (`ios-work/work9.img`, `work11.img`).

## Địa chỉ SoC picasso đã biết (từ `tools/mkadt.py`, đã chạy thật)

| Khối | Địa chỉ |
|---|---|
| UFS host | `0x01d84000` (size `0x3000`) |
| GICD / GICR | `0x17a00000` / `0x17a60000` |
| Framebuffer (cont_splash) | `0xa0000000` (35 MiB) |
| smem | `0x80900000` |
| modem_wlan carve-out | `0x8c000000` + `0xF800000` |
| Kernel window ta dùng | `0xac000000` + `0x50000000` |
| ECID (synthetic) | `0x5357454554000001` |

Cần bổ sung (lấy từ device tree thật của máy): IPCC, AOSS, RSC, apps-SMMU, cmd_db,
IPA/GSI, mpss/smem/imem, PAS ids, tên firmware (`*_ipa_fws`), panel, touch, PMIC.

## Kế hoạch

1. **Thu dữ liệu máy** (khi nối điện thoại, Android/recovery):
   pull-picasso-fdt.sh` → `picasso/fdt.dtb`, cmdline, soc_id, partitions.
2. **Dựng skeleton device repo** `hakios-picasso` theo mẫu `hakios-sm7325`
   (`device.conf`, `soc.args`, `configs/`, `uefi/` trỏ tới picasso UEFI).
   SoC layer mới: `--soc sm7250` (HakiOS chưa có SM7250).
3. **Mốc 1**: loader + kernel của họ boot trên picasso tới `userland/hello`
   (chỉ `fastboot boot`, không flash).
4. **Mốc 2**: UFS read → `system.apfs` từ RAM.
5. **Mốc 3**: display + touch → thấy màn hình.
6. **Mốc 4**: GPU (Adreno 620) qua Mesa → có GUI.

## Ghi chú

- Nhánh này **không** sửa `xnu/patches` của agy; mọi thay đổi HakiOS nằm dưới `.
- Repo tham chiếu đã clone: `/home/tritm/hakios-u` (core, nhánh `u-boot`),
  `/home/tritm/hakios-sm7325` (device + SoC layer mẫu).

## Pipeline đã dựng (2026-10-31)

| Bước | Lệnh / kết quả |
|---|---|
| Firmware | `hakios-mu` = uefi_picasso + `hakios-uefi/hakios.patch` + `files/` + `patches/0002` → `Build/picassoPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd` (2 MiB) |
| Loader | `hakios-u/xnuloader/loader.efi`, build bằng clang-15 + lld-link-15, có `platform_picasso` (`patches/0001`) |
| Container | `xnuloader/mkpayload.py` → loader.efi + kernel + cmdline + usb |
| Boot image | `build-bootimg.py` → gzip(BootShim + FD + XNUP) + picasso.dtb, header v1 (**7.720.960 B**) |

Chi tiết trong `HANDOFF.md` mục dưới; boot image hiện tại ở `/tmp/hakios-picasso-boot.img`.
Backup `boot` trước khi thử: `ad6ea9d0bea679c70c7c3801bbf50939` (128 MiB, trên `/sdcard` của máy).

## Kết quả trên máy (2026-10-31 07:58)

Chain HakiOS **chạy được trên picasso**:

| Mốc | Bằng chứng |
|---|---|
| UEFI (Mu-Silicium picasso + HakiOS XnuLoader) | boot image 7.720.960 B flash vào `boot`, máy lên USB `product: xnuloader` |
| Loader | log trên máy: `platform picasso`, `display 1080 x 2400 at a0000000`, `CNTFRQ=19200000`, `usb: started` |
| Kernel build riêng (sm7250) | `hakios-kernel` 14.800.464 B, `kernel.ver=...-sm7250-...` |
| Kernel chạy | `kernel returned: log at ..., 1306 bytes` — kernel chạy 20 s rồi trả về loader |
| Log kernel | `kprintf initialized`, `initialize_screen: b=A0000000 w=438 h=960`, `gic: v3 0xfffffff155064000`, `Darwin Kernel Version 22.5.0 ... sm7250`, `vm_page_bootstrap: 269057 free pages` |

Kernel `prebuilt` (redwood/sc7280) **treo cứng tại entry** — phải build kernel với config picasso
(`make picasso_defconfig && make kernel`) mới chạy.

### Sửa toolchain để build được kernel trên Ubuntu 22.04
- `patches/0003`: `xnu-deps/host/bin/xnu-clang` dùng clang-15 và bỏ `-ftrivial-auto-var-init=zero`
  (clang <16 từ chối cờ này).
- `patches/0004`: `SETUP/replacecontents` cần `strlcat` (glibc < 2.38 chưa có) — shim static.
- EDK2: Python 3.10 thiếu `tomllib` → file `tomllib.py` re-export `tomli` trong venv.
- `patches/0001`/`0002`: platform_picasso + bỏ UniXNUPkg/DarwinBootPkg + `-DPAYLOAD_BLOCK` cho BootShim.

### Vòng lặp test
xl-run.sh [SECS] [args...]` — stage payload + `oem run` + `get_staged` (không dùng `xl` vì
`xl enter` cần ảnh U-Boot/Silicium và `getvar` dễ treo). Sau kernel treo phải **power-cycle**;
kernel trả về bình thường thì loader vẫn sống.

### Tiếp theo
`CONFIG_HAIKOS_UFS=y` + `system.apfs` trong RAM → launchd (bước 3 của guide).

## Trạng thái 2026-10-31 08:25 (sau khi test trên máy)

| Hạng mục | Kết quả |
|---|---|
| UEFI + loader trên picasso | ✅ `platform picasso`, `display 1080x2400 at a0000000`, USB `xnuloader` |
| Identity thật (DT Apple `DeviceTree.d22ap.im4p`) | ✅ loader 71.680 B (trước: 66.048 B placeholder) |
| UFS trong loader | ✅ `ufs: unit 5 ... 109 partitions and 6 units` |
| Kernel sm7250 chạy | ✅ in log, trả về loader sau `loader_return_after` |
| Kernel dừng ở | ❌ ngay sau `vm_page_bootstrap` / `VM boostrap: 0 maps...` (giống nhau với `cpus=1`, `cpus=8`, có/không `soc_*`) |
| `bootcpu=7` | ❌ PSCI: `core 700: the firmware answers 0 to the request to start it` |
| Checkpoints 10–21 (đều trong `arm_init`) | đã qua hết |

Kết luận: `arm_init` xong; dừng trong `machine_startup`/`kernel_bootstrap` trước printf kế tiếp.
Bước tiếp: thêm `loader_checkpoint()` vào các mốc sau `vm_page_bootstrap` (patch riêng) để khoanh vùng,
hoặc lấy log so sánh từ QEMU (`make qemu-open`, đã cài qemu-system-arm + AAVMF;
QEMU 6.2 không có `cortex-a76` → `QEMU_CPU=cortex-a72`).

Phụ thuộc host: `pip install lzfse` (mkidentity), `xnuloader/build.sh` dùng clang-15 (patch 0005).

## 2026-10-31 09:10 — khoanh vùng treo bằng checkpoint (kết quả cuối)

Thêm checkpoint Bring-up vào XNU (`patches/0006`): `kernel_bootstrap` 50–59, `kernel_bootstrap_thread` 60–62,
`bsd_init` 70–76, `load_init_program` 77–80.

| Checkpoint | Vị trí | Kết quả trên máy |
|---|---|---|
| 10–21 | `arm_init` | qua |
| 50–55 | `kernel_bootstrap` (vm_mem_bootstrap → clock_init) | qua |
| 56–59 | `kernel_thread_create` | qua |
| 60–63 | `kernel_bootstrap_thread` → trước `bsd_init` | qua |
| 70–73 | `bsd_init` (dev_kmem_init, bsd_bufferinit, **bsd_autoconf/IOKit**, inittodr) | qua |
| 74–75 | `setconf`, trước `vfs_mountroot` | qua |
| 76 | **sau `vfs_mountroot` — root mount THÀNH CÔNG** (ramdisk `prebuilt/userland/hello`) | qua |
| 77 | vào `load_init_program()` | qua |
| 79/80 | sau `load_init_program_at_path()` (exec được / exec lỗi) | **KHÔNG bắn** — treo bên trong exec |

⇒ Kernel picasso chạy hết bsd_init, mount root, và **treo trong `load_init_program`/exec** (không panic, không về).
Bước tiếp: checkpoint trong `load_init_program_at_path`/`mach_exec`/Mach-O loader, hoặc dùng userland thật
(`system.apfs` build từ IPSW 16.5 20F66 iPhone10,3/10,6) thay ramdisk `hello`.

## Nơi lưu bản "chuẩn" để gửi upstream (2026-10-31)

- Fork: `github.com/minhtritt1996/hakios-xnu-kernel`, nhánh **`picasso`**, 4 commit sạch:
  1. `xnuloader: platform picasso (SM7250, Redmi K30 5G)`
  2. `build: clang 15 on a Linux host, and QEMU_CPU for an older QEMU`
  3. `xnu: SETUP/replacecontents builds on a host whose libc has no strlcat`
  4. `devices/manifest.conf: picasso (Redmi K30 5G, SM7250)`
- Repo device: `github.com/minhtritt1996/hakios-device-picasso` (`main`), manifest trỏ tới revision
  `12a3f6695af1b380976f3ad090081c5ee3202042`.
- Quy trình publish theo `docs/porting/repositories.md#publishing-a-device`: `check` → `split` → push repo → PR chỉ
  sửa `devices/manifest.conf`.

## 2026-10-31 10:05 — kênh quan sát mới + điểm treo chính xác

**Vấn đề cũ**: log kernel ngừng vào buffer loader sau `console_init`, nên "treo ở vm_page_bootstrap"
chỉ là giới hạn quan sát. `loader_checkpoint()` cũng không dùng để bisect được (mỗi phiên loader chỉ chạy
kernel 1 lần; và tôi đã đọc nhầm dòng "kernel returned" nằm trong log cũ).

**Kênh mới (patch `0007`)**: `loader_cp_mark(n)` ghi `"CPnn "` trực tiếp vào ring log của loader
(`loader_log_putc`), nên **một lần chạy** thu được toàn bộ dấu mốc kernel đã đi qua. Đã đặt 31 mốc trong
`vm_map.c`, `vm_kern.c`, `vm_init.c`, `startup.c`, `bsd_init.c`.

**Chuỗi treo đo được (một lần chạy, kernel trả về đúng hạn):**

```
CP92 CP100 CP101 CP109 CP112 CP113 CP114 CP115 CP116        (rồi dừng)
```

| Mốc | Ý nghĩa |
|---|---|
| 92 | sau `printf("VM boostrap: …")` |
| 100 | sau `VM_PAGE_MOVE_STOLEN` |
| 101 | sau `PE_parse_boot_argn("malloc_no_cow")` |
| 109 | `vm_map_init()` trả về |
| 112/113 | qua `vm_map_will_allocate_early_map` |
| 114 | `pmap_kernel()` trả về |
| 115 | vào `vm_map_create_options()` |
| 116 | trước `zalloc_id(ZONE_ID_VM_MAP, Z_WAITOK \| Z_NOFAIL \| Z_ZERO)` |
| **117** | **không tới** ⇒ **treo trong `zalloc_id()` của zone allocator** |

⇒ `kmem_init()` → `vm_map_create_options()` → **`zalloc_id(ZONE_ID_VM_MAP, Z_WAITOK)` không trả về**.
Đây là điểm nghẽn cụ thể cần xử lý cho SM7250 (zone allocator phải cấp phát/mở rộng → phụ thuộc timer,
scheduler, hoặc vùng nhớ mà DT/loader dựng cho picasso khác redwood).

Cách chạy lại: `make kernel`, `fastboot reboot` (loader lên), rồi
`RAMDISK=prebuilt/userland/hello bash xl-run.sh 40 "-v debug=0x8 rd=md0 hv_wdt_pet=1 cpus=1 $(cat devices/picasso/soc.args|grep .|tr '\n' ' ')"`
→ đọc các `CPnn` trong log.

## 2026-10-31 11:25 — ROOT CAUSE: `redzone` không khởi tạo (UB) — kernel picasso BOOT TỚI DARWIN USERLAND

**Nguyên nhân gốc** (`osfmk/kern/zalloc.c`, `zone_create_ext()`):

```c
	uint16_t redzone;            /* zone_elem_adjust_size() chỉ ghi *redzone trong #if KASAN_CLASSIC */
	...
	size = zone_elem_adjust_size(name, size, flags, &redzone);
	...
		z->z_elem_size = (uint16_t)(size - redzone);   /* size - <rác>  ⇒ UB */
```

Với `KASAN_CLASSIC` tắt (bản build của ta), `redzone` **không được gán** ⇒ `size - redzone` là UB. clang 15 và clang 18
(Linux) xử lý UB này khác Apple clang: chúng **bỏ hẳn lệnh ghi `z->z_elem_size`** (disassembly: nhánh cấp phát chỉ có
`strh … [x23,#56]`, `strh … [x23,#58]`, không có `#52`). Hệ quả: zone VM không có `z_elem_size` ⇒ `zone_cram_early()`
không tạo được element (`VM boostrap: 0 maps`) ⇒ `zalloc_id(ZONE_ID_VM_MAP, Z_WAITOK)` đi nhánh blocking
`zone_expand_locked()` trong `kmem_init()` khi `kernel_map` đang được tạo ⇒ **deadlock/panic**.

**Fix (`patches/0012`, 1 dòng):**
```c
	uint16_t redzone = 0;   /* zone_elem_adjust_size() only sets it with KASAN_CLASSIC */
```

**Bằng chứng sau fix (QEMU `-cpu max`, kernel sm7250):**
```
resident image at pa fc000000 size 93a00000: it is the root (md0), the payload ramdisk is md1
VM boostrap: 102 maps, 204 entries and 512 holes available
launchd.development
launchd
SpringBoard.plist
SpringBoard;/redwood/lib/libhakios.media.dylib mediaserverd;... Sileo;... Preferences
launchd.darwin, pid 1): writing core to /private/var/cores/launchd.darwin.core   ← tầng sau (userland)
```
Tức kernel đi hết `kmem_init → bsd_init → vfs_mountroot (system.apfs) → load_init_program → launchd` và nạp job
SpringBoard — **giống bản prebuilt của họ**. (Trước fix: `VM boostrap: 0 maps` + panic.)

**Lưu ý công cụ**: QEMU 6.2 của Ubuntu không có `cortex-a76`; chạy `QEMU_CPU=max` (A72 thiếu feature ARMv8.2 nên kernel
không in gì). `make qemu-open`/`run.sh` dùng ảnh 2,3 GB: dùng `qemu/run-darwin.sh` (map ảnh vào `qemu/ram.img`,
`--rdres`), không dùng `QEMU_ESP=1` (FAT giới hạn 2 GB).

## 2026-10-31 11:30 — kernel ta == kernel tham chiếu (đo song song trong QEMU)

| | kernel ta (sau fix `redzone = 0`) | kernel `prebuilt` của họ |
|---|---|---|
| `VM boostrap` | `102 maps, 204 entries, 512 holes` | `102 maps, 204 entries, 512 holes` |
| userland init (`/redwood/etc/rc`) | `=== 36 checks, 0 failed ===` | như nhau |
| launchd (libignition) | `ignition blob not available` → `failed to query hardware target: 22` | **giống hệt** |
| kernel sau đó | `initproc failed to start ...` (panic theo thiết kế) | **giống hệt** |

⇒ Bản build của ta đã **tương đương** bản tham chiếu. Lỗi `hardware target` là giới hạn của QEMU (thiếu dữ liệu
phần cứng thật), nên bước kiểm tiếp theo **phải chạy trên picasso**:

```
bash phone-darwin.sh          # flash loader-only -> nạp system.apfs 2,3 GB -> chạy kernel fix + root.apfs
```

## 2026-10-31 12:05 — TRÊN MÁY THẬT: Darwin kernel chạy qua BSD init, framebuffer attach

Ảnh màn hình điện thoại (kernel `-v`) sau khi chạy kernel đã fix + `system.apfs` 2,3 GB resident:

```
Security policy loaded: Apple Mobile File Integrity (stand-in) (AMFI)
calling mpo_policy_init for Sandbox
Security policy loaded: Seatbelt sandbox policy (stand-in) (Sandbox)
No POST boot-arg set.
GenericARMPE: platform "device-tree"
GenericARMCPU: cpu 0 (phys id 0x0) registered
MAC Framework successfully initialized
using 2301 buffer headers and 2301 cluster IO buffer headers
ktriage_register_subsystem_strings: set subsystem 8 strings
mcache: 1 CPU(s), 64 bytes CPU cache line size
mbinit: done [96 MB total pool size, (64/32) split]
dlil_init: Waiting for all the create dlil kernel threads to get scheduled at least once.
dlil_init: All the created dlil kernel threads have been scheduled at least once. Proceeding.
ifnet_attach: All kernel threads created for interface lo0 have been scheduled at least once.
MiniIOMobileFramebuffer: display 1125x2436 on a …
```

⇒ Trên **picasso thật**: `bsd_init` xong phần MAC/AMFI/Sandbox, `mbinit`, `dlil_init`/`ifnet_attach` (lo0),
và **framebuffer (MiniIOMobileFramebuffer) đang attach panel 1125×2436** — vượt xa mốc "VM boostrap" trước đó.
Bước còn lại để tới GUI: `launchd` → `SpringBoard` (cần firmware GPU/`fw-*` cho phần vẽ).

## 2026-10-31 12:20 — loader identity thật: kernel vào IOKit, framebuffer theo panel thật

Ảnh màn hình máy (kernel `-v`), sau khi flash loader build bằng `mkidentity` (DT Apple thật):

```
[ 104.006945]: BANK subsystem is initialized
[ 104.013769]: Serial requested, consistent debug disabled or debug boot arg not present, configuring debugging over serial
iBoot version: iBoot-8422.100.650
iwNVRAMManager.cpp:serializeImage:475 - NVRAM : commonPartitionOffset = 0x30, … systemPartitionSize = 0
minicrypto: registered (md5, sha1, sha2, hmac, aes-cbc/ztr, hmac-drbg prng); no xts/gcm, no RSA
hakios-kernel: 8 drivers built in (.config): virtio_sound socinfo virtio_input virtio_net cpufreq thermal ufs virtio_blk
calling mpo_policy_init for vnguard  → Security policy loaded: Guarded vnode policy (vnguard)
calling mpo_policy_init for AMFI     → Apple Mobile File Integrity (stand-in)
calling mpo_policy_init for Sandbox  → Seatbelt sandbox policy (stand-in)
No POST boot-arg set.
GenericARMPE: platform "D22AP"        ← identity Apple có hiệu lực
GenericARMCPU: cpu 0 (phys id 0x0) registered
MAC Framework successfully initialized
mbinit: done [96 MB total pool size, (64/32) split]
dlil_init … ifnet_attach … lo0
MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2400 (4320 bytes per row)
```

So với lần trước (`platform "device-tree"`), identity thật đã đổi `GenericARMPE` sang `D22AP` và cung cấp
`iBoot-8422.100.650`, NVRAM/minicrypto; framebuffer giờ lấy đúng panel 1080×2400 (stride 4320 B).

## 2026-10-31 13:40 — TRÊN MÁY THẬT: Darwin userland CHẠY (init self-test đầy đủ, dyld + shared cache)

Hai ảnh màn hình (sau fix "bỏ chờ resource IOBSD khi root là RAM disk"):

```
[ ok ] walk our image (readdir, lstat, readlink)
[ ok ] the system image has the dynamic linker
[ ok ] read a 300 KB file and verify every byte
[ ok ] mmap the file and verify every byte (page-in)
[ ok ] create a file, write 100 KB, read it back ... rename, hard link, symlink
[ ok ] 64 MB of anonymous memory: touch every page, read it back
[ ok ] fork ... exec a second program from the image, collect its exit status
[ ok ] pipe from a child process
apfs: pagein asks for 32768 bytes at a time (page size 16384)
Hello from a dynamically linked program! pid 4, 3 argument(s)
uname: Darwin 22.5.0 (iPhone10,3)
heap: malloc works (0x1000058000), stack near 0x16f6d7d38, main at 0x10072c000, printf at 0x18bde000
[ ok ] run a program through dyld and the shared cache
...
crw------- 1 0 /dev/md0
brw------- 1 0 /dev/rdisk0
crw-rw---- 1 0 /dev/hakios-touch
crw-rw---- 1 0 /dev/hakios-audio
[ ok ] devfs is mounted on /dev
[ ok ] /dev/zero ... /dev/null
/dev/random: a1 ae 90 b6 0c b2 92 51 57 7b c3 c5 88 e3 89 13
[ ok ] /dev/random
```

⇒ Trên **picasso thật**: `system.apfs` (2,3 GB, resident) mount làm root, `/redwood` (md1) mount,
**dyld + dyld shared cache hoạt động**, fork/exec/pipe, devfs với `/dev/md0`, `/dev/hakios-touch`,
`/dev/hakios-audio`. Đây là toàn bộ self-test của `init` trước khi `exec /sbin/launchd.darwin`.

**Fix quan trọng của port** (`iokit/bsddev/IOKitBSDInit.cpp`): khi root là RAM disk (`rd=md*`),
`IOFindBSDRoot()` **không chờ** resource `IOBSD` nữa (trên picasso `waitForMatchingService` không bao giờ
trả về, làm kernel đứng ngay sau `setconf`). Với fix này kernel đi tiếp: `/chosen/memory-map` → `md0` →
mountroot → init.

## Trạng thái bàn giao — 2026-10-31 15:25

### Đã đạt trên phần cứng (picasso thật)
1. UEFI picasso (Mu-Silicium + XnuLoader của HakiOS) chạy, USB `product: xnuloader`, UFS survey OK.
2. Loader với **identity Apple thật** (`mkidentity.sh DeviceTree.d22ap.im4p`): kernel in `platform "D22AP"`,
   `iBoot-8422.100.650`, `Hardware Model: iPhone10,3`.
3. Kernel sm7250 tự build: `VM boostrap: 102 maps` → `bsd_init` (MAC/vnguard/AMFI/Sandbox, mbinit, dlil, lo0)
   → IOKit + framebuffer (`MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2400`).
4. **Darwin userland chạy**: `system.apfs` 2,3 GB (resident) mount làm root, `/redwood` (md1) mount,
   init self-test toàn bộ `[ ok ]` — file I/O, mmap/page-in, fork/exec/pipe, **dyld + dyld shared cache**,
   devfs (`/dev/md0`, `/dev/disk0`, `/dev/hakios-touch`, `/dev/hakios-audio`), `/dev/random` thật.

### Ba fix đã tìm ra (trong `patches/`)
| Patch | Nội dung |
|---|---|
| `0012-xnu-zone-redzone-init.patch` | `uint16_t redzone = 0;` trong `zone_create_ext()` — UB khiến clang Linux bỏ store `z_elem_size` ⇒ zone VM rỗng ⇒ deadlock `kmem_init` (`VM boostrap: 0 maps`). Fix: `102 maps`, kernel boot được. |
| `0013-IOKitBSDInit-iosbs-skip.patch` | Với root RAM disk (`rd=md*`), `IOFindBSDRoot()` **không chờ** resource `IOBSD` (trên picasso `waitForMatchingService` không trả về ⇒ kernel đứng ngay sau `setconf`). Kèm các `printf` chẩn đoán `HAKIOS: IOFindBSDRoot: …`. |
| `0014-MiniIOFB-mfb-gate.patch` | Gate driver hiển thị bằng boot-arg `mfb=1` (mặc định tắt) để khoanh vùng bring-up. |
| `0015-init-bringup-marks.patch` | 9 dấu `HAKIOSI:` trong `userland/src/init.c` (quanh `test_time`/`configure_*`/`run_rc`). |

### Chỗ đang dừng
Trên máy, sau `[ ok ] /dev/random` (hết `test_devices`), init **không in** `-- time --`/`HAKIOSI: time: …`
⇒ nghi `usleep()`/`gettimeofday()` (timer cho userland) hoặc console write bị chặn.

### Bước tiếp (khi có máy lại)
1. `SYSTEM=1 PREBUILT=1 userland/build.sh` (đã chạy, ảnh 15:23) rồi nạp + chạy: các dấu `HAKIOSI:` sẽ chỉ đúng syscall treo.
2. Nếu là `usleep`: kiểm tra timer/GIC cho userland trên picasso (kernel in `virtual timer on PPI 27` — cần
   xác nhận interrupt được bật cho core 0 sau `bsd_init`).
3. Sau khi qua `test_time` → `exec /sbin/launchd.darwin` → xem `ignition`/`hardware target` (trong QEMU nó báo
   `failed to query hardware target: 22`; trên máy có identity D22AP nên có thể khác).
4. Cho GUI: thêm `mfb=1` vào cmdline (driver hiển thị bật lại) và dùng `fw-gpu/` (7 firmware Adreno 620 đã
   trích từ `/vendor/firmware` của máy, `build.sh` đã nhúng vào `fw/`).

### Cách chạy lại nhanh (không cần đoán)
```
# 1. (máy ở loader) nạp userland + chạy
cd /home/tritm/hakios-u
fastboot oem ramdisk && fastboot stage userland/system.apfs      # 80 s
./xnuloader/mkpayload.py -o run-payload.bin kernel=hakios-kernel ramdisk=userland/root.apfs \
  --cmdline "loader_return_after=60 -v debug=0x8 rd=md0 hv_wdt_pet=1 cpus=1 $(grep -v '^#' devices/picasso/soc.args|tr '\n' ' ')"
fastboot stage run-payload.bin && fastboot oem run
# 2. nếu kernel panic hoặc trả về: fastboot oem log; fastboot get_staged out.log
# 3. nếu treo: tắt/bật → ABL → `fastboot reboot recovery` → dd logdump (loader đã ghi nếu có ufs=2)
```

## Bẫy build userland (quan trọng)

`userland/build.sh` với **`PREBUILT=1`** sẽ **dùng binary init có sẵn** và không biên dịch `userland/src/init.c`
⇒ mọi sửa đổi trong `init.c` (ví dụ các dấu `HAKIOSI:`) **không vào ảnh**. Build đúng:

```
cd /home/tritm/hakios-u
SYSTEM=1 userland/build.sh          # KHÔNG đặt PREBUILT -> biên dịch init + đóng cả root.apfs và system.apfs
strings -a userland/root/sbin/init | grep -c HAKIOSI   # phải > 0
```
`fw-*/` (firmware GPU lấy từ máy) được `build.sh` nhúng vào `fw/` của ảnh nhỏ (`root.apfs`).
Ảnh `system.apfs`/`root.apfs` được mkapfs nén file, nên `strings` trên cả ảnh không thấy nội dung —
kiểm tra qua `userland/root/<path>` (thư mục staging) thay vì quét ảnh.

## Phiên chạy tới (khi có máy)

```
cd /home/tritm/ios
bash quick.sh 60 --full --mfb        # nạp system.apfs 2,3GB, chạy kernel, có mfb=1
```
Sau đó:
- Nếu kernel panic/trả về: script tự `oem log` + in các dòng `HAKIOS/HAKIOSI/launchd/panic`.
- Nếu kernel treo (loader bị chiếm core): chụp màn hình, tìm dòng `HAKIOSI:` cuối cùng.

## 2026-10-31 16:20 — fix quyền plist: launchd spawn được cả bộ daemon (kiểm trong QEMU)

Trong QEMU, sau khi init/launchd chạy, launchd **từ chối mọi job** vì plist trong ảnh là **0664**
(group-writable) — repo giữ file 0664 và `mkapfs.py` lấy mode từ cây:

```
Caller specified a plist with bad ownership/permissions:
   path = /redwood/LaunchDaemons/com.apple.SpringBoard.plist … error = 122
```

Sửa trong `userland/build.sh` (chuẩn hoá staging trước khi đóng ảnh):
```sh
chmod 755 root root/LaunchDaemons
chmod 644 root/LaunchDaemons/*.plist
chmod 755 root/sbin/init root/bin/*
```
Kết quả trong QEMU (cùng ảnh, cùng kernel):
```
(user/501/com.apple.SpringBoard) <Notice>: service state: spawning
(user/501/com.apple.SpringBoard [7]) <Notice>: Successfully spawned SpringBoard[7] because system support
(user/501/com.apple.runningboardd [6]) <Notice>: xpcproxy spawned with pid 6
(user/501/com.apple.backboardd [11]) <Notice>: Successfully spawned backboardd[11] because speculative
(user/501/com.apple.powerd [8]) <Notice>: xpcproxy spawned with pid 8
SpringBoard[7] exited due to SIGTRAP | sent by exc handler[7]     ← cần GPU/CA (QEMU không có)
```
⇒ Tầng launchd/daemon đã thông; SpringBoard chỉ còn thiếu đường vẽ (GPU/CA).
Fix này đã đưa lên fork: commit `caa3c6e` (nhánh `picasso`).

## 2026-10-31 20:50 — font console 3/2, probe GIC gate, và SpringBoard chết ở đâu (QEMU)

**1. Console kernel to hơn 50%** (`osfmk/console/video_console.c`, commit `75c6dd1` trong cây kernel):
`VC_CW = 8*3/2 = 12`, `VC_CH = 16*3/2 = 24`; `vc_render_char()` phóng glyph 8×16 bằng nearest
neighbour; mọi chỗ dùng hình học cell (buffer rendered font, `linelongs`, paint 8/16/32bpp,
`vc_reverse_cursor`, `v_rows/v_columns`) đổi sang VC_CW/VC_CH. Chỉ ảnh hưởng chữ **do kernel vẽ**;
banner `hello hakios-kernel!` là UEFI ConOut của loader (font firmware) nên vẫn nhỏ.

**2. Probe GIC/timer chỉ chạy với `gicprobe=1`** (`bsd/kern/bsd_init.c`, commit `45d596f`), ghi số
đo bằng `loader_cp_hex` (tag 70..80) nên đọc qua logdump, và `panic("HAKIOS probe done")` để trả về.
Đo trên máy (logdump, lần chạy 20:0x): `ints_enabled=1 igrpen1=0x1 pmr=0xf0 sre=0x1 ctlr=0x8440
cntv_ctl=0x5 cntfrq=19200000`, counter chạy 200 ms thật, `IOSleep(100)` không trả về.
⇒ timer **đang bật, không mask, ISTATUS=1 (pending)**, CPU interface mở hết mà IRQ không được nhận.
Chưa đọc được HPPIR0/HPPIR1/RPR/GICR_ISENABLER/GICD_IGROUPR (bản probe mới) vì `oem log` chỉ trả
buffer của loader; phải đọc logdump qua recovery (1 lần power-cycle mỗi lần đo).

**3. SpringBoard chết ở đâu (QEMU, tái lập 100%)**: thêm crash reporter vào `libhakios.sb.dylib`
(`userland/src/inject_sb.c`, `crash_handler_install()` cho SIGTRAP/SEGV/BUS/ILL/ABRT): in signal,
`si_addr`, pc/lr/sp, thanh ghi, backtrace `dladdr`, và tên class của con trỏ trong x0/x2/x4/x6.
Mỗi boot QEMU: 28 lần, cùng một chỗ:
```
SIGTRAP pc=FrontBoardServices+0x58c20 (brk #0; brk #1)
  #1 libsystem_platform+0x22f4 (sigtramp)
  #2 FrontBoardServices+0x58c20
  #3 FrontBoard+0x50438        (một dispatch block)
  #4/#5 libdispatch            #6 FrontBoard+0x1a30  #7 FrontBoard+0x5c1b4
  #8 SpringBoard+0x9b9c8       #9 dyld+0x14344
```
x16 = BaseBoard+0x6cd58 (một getter `adrp/str/ret`), x0 = con trỏ tagged, LR == PC.
`lr == pc` ⇒ FrontBoard **gọi thẳng** vào `0x58c20`, và ở đó byte đầu tiên là `brk #0` ⇒ đây là
một **trap thunk** (không phải thân hàm bình thường).  Không có dòng nào của SpringBoard trước khi
chết (im lặng).  `sysimage/20F66__iPhone10,3_6/dyld_shared_cache_arm64*` là bản strip (không có
`.symbols`), nên tên symbol phải lấy từ export trie — parser đọc trie chưa đúng (chỉ ra 1 export),
cần sửa `va_of_foff` (offset file → VA trong cache nhiều subcache).

**Bẫy**: chạy payload lần thứ hai trong **cùng một session loader** thì hỏng (máy reset, logdump
không đổi) — luôn power-cycle → ABL → `fastboot reboot` → loader → `stage`+`oem run`.

## 2026-10-31 21:00 — nguyên nhân SpringBoard chết: "failed to find the main CADisplay"

Chuỗi lệnh ở `FrontBoardServices+0x58c1c` là `bl __bs_set_crash_log_message ; brk #0`
(`BaseBoard+0x6cd58` = `__bs_set_crash_log_message`, xác nhận bằng export trie của BaseBoard).
Tức FrontBoardServices **cố ý** crash qua macro fatal của BSService. Message nằm trong biến toàn cục
mà setter ghi (`adrp x8,#0x1dd147000; str x0,[x8,#0x900]`) — crash reporter đọc ra:

```
hakios: CRASH message: failed to find the main CADisplay
```

⇒ **QuartzCore không trả về `CADisplay` chính** cho FrontBoard/SpringBoard. Đây là gốc của vòng
crash 14–28 lần/boot, không phải lỗi của hook nào của ta:
boot với `sb_nohooks=1` (gate mới trong `inject_sb.c`: giữ crash reporter, tắt hết hook) vẫn cùng
message. backboardd thì báo window server ready và "acquired main display digitizer", nên display
có ở phía window server nhưng client (`[CADisplay mainDisplay]`) không thấy.

**Đọc message trực tiếp**: crash handler tính slide bằng cách quét `_dyld_image_count()` /
`_dyld_get_image_vmaddr_slide()` tìm image chứa pc, rồi `vm_read_overwrite` tại
`0x1dd147900 + slide` (con trỏ message) — không cần symbol cho cache.

## 2026-10-31 22:20 — ROOT CAUSE của timer: PPI là Group 0, NS không đổi được

Đo trên máy (probe `gicprobe=1`, ảnh màn hình với font 1,5×), tại bsd_init sau inittodr:

```
HAKIOS probe: counter 200000052 ns ints 1 igrpen1 0x1 pmr 0xf0 sre 0x1 cntv_ctl 0x5 cntfrq 0
HAKIOS gic: hppir1 0x3ff rpr 0xff ctlr 0x8440
HAKIOS gic: GICD ctlr 0x12 isenabler0 0 igroupr0 0 igrpmodr0 0 icactiver0 0
HAKIOS gic: GICR isenabler0 0x08020000 icpendr0 0x10000000 igroupr0 0 icactiver0 0 waker 0
HAKIOS gic: IGROUPR0 was 0, wrote ffffffff, reads 0
HAKIOS gic: GICD_CTLR was 0x12, tried EnableGrp0, reads 0x12
HAKIOS gic: after the writes hppir1 0x3ff
```

Đọc: PPI 27 (virtual timer) **đã enable** ở redistributor (bit 27 của ISENABLER0), PPI 28 pending,
`ICC_IGRPEN1=1`, PMR=0xf0, SRE=1, `CNTV_CTL=0x5` (enable, unmasked, ISTATUS=1) — tất cả đúng, nhưng
`ICC_HPPIR1 = 0x3ff` (spurious) và `GICR_IGROUPR0 = 0` ⇒ **mọi SGI/PPI là Group 0**, trong khi
`GICD_CTLR = 0x12` chỉ bật EnableGrp1NS (bit EnableGrp0 = 0) ⇒ interrupt bị bỏ im lặng.

Và NS **không sửa được**: ghi `GICR_IGROUPR0 = 0xffffffff` đọc lại vẫn 0; ghi `GICD_CTLR |= 1`
(EnableGrp0) đọc lại vẫn 0x12. Tức group/secure config do XBL/QTEE (hoặc hypervisor Gunyah) giữ, XNU ở
EL1 NS không với tới. Đây là gốc của "kernel chạy nhưng timer/usleep chết" — không phải lỗi code XNU.

Hướng đi tiếp (chưa làm):
1. **So với Linux trên cùng máy** (Droidian): kernel Linux GICv3 cũng ghi `GICR_IGROUPR0 = ~0` lúc boot;
   nếu Linux chạy được timer thì hoặc nó chạy ở chế độ DS=1, hoặc nó dùng **virtual CPU interface**
   (GICv3 virtualization của Gunyah: `ICH_*`/`ICV_*` với HCR_EL2.IMO=1). Phải biết cái nào trước khi sửa.
2. Nếu là virtualized: XNU phải dùng `ICV_*` thay `ICC_*` (thay đổi lớn ở `osfmk/arm64/gic_v3_irq.c`).
3. Nếu là Group 0 thật: cần secure side (patch UEFI/QTEE không khả thi) → dùng nguồn IRQ khác là **SPI**
   (Qualcomm QTIMER), vì group của SPI nằm ở `GICD_IGROUPRn` (NS được ghi nếu SPI không secure).

**Lưu ý công cụ (đã trả giá hôm nay):**
- `PE_parse_boot_argn(name, NULL, 0)` LUÔN trả FALSE: phải truyền `int flag` và đọc `flag`.
- Đừng đọc `ICC_HPPIR0_EL1` / `ICC_IGRPEN0_EL1`: group-0 register, đọc từ EL1 sẽ trap và kernel chết
  im lặng (không in gì, máy về loader).
- `loader_cp_hex` treo trên port này; dùng `printf` (log kernel).
- Kernel **không sống quá ~65 s** nếu không pet watchdog hypervisor: `smc #0` với x0=0x86000007
  (`hv_wdt_pet` chỉ chạy bằng thread_call ⇒ cần timer ⇒ không bao giờ chạy trên port này).
- Loader chỉ nhận `stage`/`oem run`/`oem log`/`get_staged`/`getvar`/`reboot`; **không có `boot`**.
- Font console 1,5× (`osfmk/console/video_console.c`, commit `b5209df` sau khi revert/restore) **chạy tốt
  trên máy thật**; các lần "không có gì trên màn hình" trước đó là do probe chết trước khi in.
