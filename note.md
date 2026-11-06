# Ghi Chú Kỹ Thuật & Nhật Ký Debug HakiOS Picasso (`note.md`)

Cập nhật lần cuối: 2026-11-06
Thiết bị: **Xiaomi Redmi K30 5G (`picasso` / Qualcomm Snapdragon 765G `sm7250` / Adreno 620)**
Hệ điều hành: **Apple Darwin Kernel 22.5.0 (`xnu-8796.121.2`) + iOS 16.5 (`20F66`) Userland**

---

## 1. Tổng Hợp Trạng Thái Hiện Tại

### Đã hoạt động (Verified trên phần cứng):
- **Bootloader & Kernel Core:** `xnuloader` nhận diện GOP framebuffer `1080x2400`, nạp kernel + ramdisk qua RAM (`fastboot oem ramdisk`, `fastboot stage`, `fastboot oem run`), khởi chạy SMP (up to 8 cores), vượt qua 36/36 bài test userland Darwin.
- **GPU Adreno 620 (`adreno=22 gpu=1 adreno_keep=1`):**
  - GMU (`a650_gmu.bin`), SQE (`a630_sqe.fw`), và TrustZone Zap Shader (`a620_zap.mdt`, PAS ID 13) khởi tạo hoàn chỉnh.
  - Step 22 vẽ 3 dải màu (Cam, Trắng, Xanh) bằng phần cứng 3D core lên framebuffer.
  - `HakiosGPU` (`SweetGPU`) đăng ký thành công; `backboardd` khởi tạo thành công ngữ cảnh OpenGL ES 3.2 Mesa 25.0.7 (`FD620`) qua `libhakios.gl.dylib`.
- **Display Subsystem (`MiniIOMobileFramebuffer`):**
  - Khởi tạo sớm tại `GenericARMPE::start()`, tạo nub `/arm-io/disp0` trong `gIODTPlane` và đăng ký dịch vụ `IOMobileFramebuffer` (`disp0`).
  - `backboardd` tìm thấy `FB Count = 1`, mở `IOMobileFramebufferUserClient` thành công (`newUserClient SUCCEEDED`), `StartWindowServer` nhận diện màn hình chính `CAWindowServerDisplay` kích thước `1125x2436` scale `3`.
- **Subsystems khác đã bringup:**
  - **Audio / ADSP (`adsp=1`):** PAS ID 1, SMP2P, GLINK, QRTR, APR (`apr_audio_svc`), TFA9874 I2C-2 (`0x34`), WCD938x SoundWire.
  - **Wi-Fi (`wifi=1`):** WCN3990 qua MPSS PAS ID 4, QMI WLFW, Copy Engine, WPA2-PSK + DHCP trên `en0`.
  - **Bluetooth (`bt=1`):** QUPV3 SE5 UART (`0x894000`), `/dev/hakios-bt`.
  - **Buttons (`buttons=1`):** Power key (PMIC PON) & Volume Up (GPIO 96) trong `drivers/input/hakios.buttons.c`.

---

## 2. Phân Tích Root-Cause Vấn Đề Chưa Lên Homescreen (3 Dải Màu + Console)

Qua đối chiếu nhật ký thực tế (`gpu_gui.log` lúc 15:28 và `gui_springboard.log` lúc 16:53) cùng kết quả dịch ngược `dyld_shared_cache_arm64` (`20F66` — `IOMobileFramebuffer`, `QuartzCore`, `CoreBrightness`):

### 2.1. Nguyên nhân trực tiếp khiến `backboardd` dừng ở `gpu_gui.log` (15:28)
Trong `gpu_gui.log`:
- Dòng 2149–2163: `backboardd[11]` gọi các selector `57, 15, 22, 8, 28, 29, 33, 79, 74` của `MiniIOMobileFramebuffer`.
- Dòng 2203–2370: Ngay sau selector 74, `backboardd` **không hề dừng lại vì selector 74** mà tiếp tục nạp `libhakios.gl.dylib`, khởi tạo ngữ cảnh OpenGL ES 3.2 Adreno 620 (`FD620`), và tạo `CAWindowServerDisplay:0x10200d970 size:{1125,2436} scale:3`.
- Dòng 2395:
  ```text
  1970-01-01 08:02:50.042414+0800 backboardd[11:226] [Backlight] instantiating BrightnessSystem
  ```
  Đây là dòng log cuối cùng của `backboardd[11]`!
- **Tại sao `backboardd[11]` treo tại `[Backlight] instantiating BrightnessSystem`?**
  - Khi `BrightnessSystem` (`CoreBrightness.framework`) khởi tạo, `-[BLControl start]` gọi `-[BLControl waitForALSArrival]` (`0x1af043734`).
  - Disassembly `0x1af043734..0x1af043e84` cho thấy `-[BLControl waitForALSArrival]` gọi `IOHIDEventSystemClientCreateWithType` và chờ cảm biến ánh sáng (ALS) qua `dispatch_sync` / `dispatch_semaphore_wait` ngay trên luồng khởi tạo của `backboardd` trước khi HID server sẵn sàng.
  - Ở bản chạy lúc 15:28 (`gpu_gui.log`), hàm hook `brightness_fixes()` (bỏ qua `-[BLControl waitForALSArrival]`) **chưa có** trong `userland/src/inject.c`.
  - Đến 16:24, `brightness_fixes()` đã được thêm vào `userland/src/inject.c` và `userland/root.apfs` được đóng gói lại lúc 17:07.

### 2.2. Tại sao bản build lúc 16:53 (`gui_springboard.log`) chưa kiểm chứng được GUI?
- Lúc 16:53, script `boot_gui_full.py` được chạy với đầy đủ các cờ `wifi=1 bt=1 adsp=1 sound=4 mic=4`.
- Kernel bị panic ở CPU 4 (`0xfffffff007ad9954`) ngay trong giai đoạn `init` trước khi `launchd` và `backboardd` kịp khởi chạy.
- Giải mã symbol từ `hakios-kernel` bằng `llvm-nm`:
  - `0xfffffff007ad9954` -> `___kmem_invalid_object_panic + 0x0`
  - `0xfffffff007454390` -> `_io_map + 0x164`
  - `0xfffffff0074dfa30` -> `_sound_thread + 0x2f4` (`drivers/audio/hakios.sound.c`)
- **Nguyên nhân:** Khi test GUI, bật đồng thời `sound=4 mic=4 wifi=1` làm `_sound_thread` gặp lỗi mapping/memory. Vì vậy, khi debug GUI (`backboardd` / `SpringBoard`), cần giữ cmdline tối giản (`wifi=0 modem=0 adsp=0 sound=0 mic=0 bt=0 adreno=22 gpu=1 adreno_keep=1 ca_vbl=1`).

### 2.3. Phân tích chi tiết các Selector của `MiniIOMobileFramebuffer` (từ Disassembly `20F66`)
Kết quả dịch ngược `IOMobileFramebuffer` (`dyld_shared_cache_arm64.13`) và `QuartzCore` (`dyld_shared_cache_arm64.03`):

1. **IORegistry Properties trên `IOMobileFramebuffer` (`0x1af1c3868`):**
   - `IOMobileFramebufferOpen` đọc 2 thuộc tính số nguyên 32-bit `"CanvasWidth"` và `"CanvasHeight"` từ IORegistry entry vào `obj+0x4f4` và `obj+0x4f8`.
   - Cần bổ sung trong `IOMobileFramebuffer::start`:
     ```cpp
     setProperty("CanvasWidth", fWidth, 32);
     setProperty("CanvasHeight", fHeight, 32);
     ```
2. **Selector 15 (`0x0f`, `0x1af1c5710`):**
   - Input: 2 scalars (`in[0]`, `in[1]`). Output: 1 scalar (`out[0] = 0`, `args->scalarOutputCount = 1`). Return `kIOReturnSuccess`.
3. **Selector 22 (`0x16`, `0x1af1c597c`):**
   - Input: 1 scalar (`in[0]`). Output: 0 scalar, 0 struct. Return `kIOReturnSuccess`.
4. **Selector 29 (`0x1d`, `kFBMethodGetDisplayArea`, `0x1af1c5eec` -> `0x1af1c1ee4`):**
   - Input: 0 scalar. Output: 8-byte `structureOutput` (`uint32_t area[2] = { fOwner->fWidth, fOwner->fHeight }`).
5. **Selector 33 (`0x21`, `0x1af1c19b4`):**
   - Input: 1 scalar (`in[0]`). Output: 0 scalar, 0 struct. Return `kIOReturnSuccess`.
6. **Selector 57 (`0x39`, `kFBMethodGetColorRemapMode`, `0x1af1c5608`):**
   - Input: 0 scalar. Output: 1 scalar (`out[0] = 0`, `args->scalarOutputCount = 1`). Return `kIOReturnSuccess`.
7. **Selector 79 (`0x4f`, `kFBMethodGetBlock`, `0x1af1c1cd0`):**
   - Với `in[0] == 0x40` (12 bytes): ghi `{ kFBColorID, kFBTimingID }` tại offset `+4` (đã có trong `MiniIOMobileFramebuffer.cpp`).
   - Với `in[0] == 0x4d` (1580 bytes = `0x62c`): `QuartzCore` (`0x1883a4754`) kiểm tra `kr == 0` để bật cờ capability bit 28. Zero-fill và trả `kIOReturnSuccess` là hợp lệ.
8. **Selector 74 (`0x4a`, `kFBMethodSupportedFrameInfo`, `0x1af1c4c60` -> `0x1af1c3384`):**
   - **ABI thực tế trong `IOMobileFramebufferSupportedFrameInfo` (`0x1af1c3384`):**
     - `in[0]` = chỉ số tag (`0 .. count - 1`).
     - `out[0]` = tổng số tag hỗ trợ (`count`).
     - `structureOutput` (64 bytes = `0x40`): chuỗi ASCII C-string kết thúc bằng NUL (`strndup((const char *)structureOutput, 0x40)` tại `0x1af1c3438`).
     - Các chuỗi tag chuẩn mà `QuartzCore` (`0x1883d1784`) tìm kiếm:
       `"Presentation_time"`, `"Presentation_delta"`, `"Requested_presentation"`, `"Performance_headroom"`, `"Thermal_throttle"`, `"Min_FrameTime"`, `"Max_FrameTime"`, `"Vbl_FrameTime"`.
   - **CẢNH BÁO QUAN TRỌNG VỀ SELECTOR 74 (`windowserver-apple-display.cpp` `0x1885246dc`):**
     - Khi Selector 74 trả `out[0] = 0`, `IOMobileFramebufferSupportedFrameInfo` trả `NULL`, `QuartzCore` (`0x1883d177c: cbz x0, #0x1883d1890`) bỏ qua đăng ký `kFBNotifyFrameInfo` (port type 3) và giữ bit `#0x23` (`frame_info_enabled` tại `display+0x9f2`) bằng `0`. Khi đó mỗi lần swap chỉ dùng `kFBNotifyVSync` (port type 5).
     - Ngược lại, nếu Selector 74 trả `out[0] > 0`, `QuartzCore` gọi `0x188534ae0(display, 1)` bật bit `#0x23` (`frame_info_enabled = 1`). Khi bit này bằng `1`, tại mỗi lần swap (`0x1885246dc`), `QuartzCore` sẽ cấp phát một đối tượng `0xc0` bytes đẩy vào `std::vector` tại `display+0x448..0x450` để đợi thông báo Mach bất đồng bộ `kFBNotifyFrameInfo` (port type 3). Nếu kernel không gửi thông báo `kFBNotifyFrameInfo` (type 3), hàng đợi `display+0x448` sẽ phình to vô hạn và không giải phóng callback!
     - Do đó: Mặc định giữ `out[0] = 0` (như thiết kế gốc của `MiniIOMobileFramebuffer`), hoặc chỉ bật `out[0] > 0` qua boot-arg `fb_frameinfo=1` khi đồng thời gửi notification type 3.

### 2.4. Vấn đề `lsd[25]` spam `denied attempt to escape CPU monitor`
- Trong `gpu_gui.log`, `lsd[25]` gây bão log console 701 lần với thông báo:
  `lsd[25] corp: denied attempt to escape CPU monitor (missing required entitlement)`
- **Nguyên nhân:** `minisandbox_priv_grant()` trong `xnu/files/bsd/kern/kern_minimac.c` (dòng 325) chỉ mới cấp tự động `PRIV_WORK_INTERVAL`.
- **Cách khắc phục (Đã áp dụng trong patch `0001`):** Cấp thêm `PRIV_PROC_CPUMON_OVERRIDE` (`10012`) tại `minisandbox_priv_grant()`:
  ```c
  if (priv == PRIV_WORK_INTERVAL || priv == PRIV_PROC_CPUMON_OVERRIDE) {
      return 0;
  }
  ```

---

## 3. Trạng Thái Refactor & Build Kiểm Chứng Từ `upstream/` (2026-11-04)

1. **Đã chuẩn hóa cấu trúc `hakios-picasso` & Bộ 6 Patch (`patches/series`):**
   - `0001-xnu-picasso-smp-timer-iomap-display.patch`: XNU SMP, `sweettick` (đã xóa console debug spam `.`, `T%u `, `W%d `), `IO_MAP_SIZE` 32MB, `PRIV_PROC_CPUMON_OVERRIDE`, `MiniIOMobileFramebuffer` (`CanvasWidth`/`CanvasHeight` + selectors 15, 22, 29, 33, 57, 74, 79).
   - `0002-xnu-picasso-usb-cdc-console-driver.patch`: Tách riêng driver `xnu/files/bsd/dev/picasso_usb.c` (+3155 dòng).
   - `0003-drivers-picasso-adreno620-audio-ufs-mpss-buttons.patch`: Common drivers cho Adreno 620, Audio (`tlmm.size = 0x5d000`), UFS 2.1, MPSS PAS.
   - `0004-userland-picasso-init-inject-springboard.patch`: Userland `init.c`, `inject.c` (`BrightnessSystem` & `BKSDisplayServicesStart` hooks, dọn gọn `setup_primary_display_descriptor()`).
   - `0005-loader-configs-and-tools-picasso.patch`: `xnuloader` (`oem paniclog`), `manifest.conf`, `common_defconfig`, `boot-args.md`.
   - `0006-hakios-metal-clang15-constexpr-signbit.patch`: Sửa lỗi biên dịch `constexpr signbit` trên Clang 15 cho `hakios-metal`.
2. **Đã thiết lập `upstream/` (Read-Only) và kiểm chứng bằng `./build.sh all --clean`:**
   - Toàn bộ 6 patch apply sạch 100% lên bản clone mới từ `upstream/`.
   - Biên dịch thành công toàn bộ tạo tác tại `build/out/`: `hakios-kernel` (15 MB), `loader.efi` (70 KB), `root.apfs` (5.8 MB), `system.apfs` (2.4 GB), `SILICIUM_UEFI.fd` (2.0 MB), `payload.bin` (21 MB) và `boot-picasso.img` (12 MB).
3. **Kết quả kiểm thử thực tế bộ tạo tác `build/out/` trên thiết bị (2026-11-04 21:11):**
   - Nạp `build/out/system.apfs` và `build/out/payload.bin` qua `tools/boot_gui_full.py` (cmdline cô lập GUI).
   - **Đã khắc phục hoàn toàn 3 vấn đề cũ:**
     1. Không còn bão log `lsd` (`denied attempt to escape CPU monitor` = 0 lần).
     2. Không còn spam ký tự `.`, `T%u `, `W0 `/`W1 ` từ `sweettick` lên màn hình console.
     3. Các selector `15, 22, 29, 33, 57, 74, 79` của `MiniIOMobileFramebuffer` đều trả thành công.
   - **`backboardd[11]` đã vượt qua điểm treo `[Backlight] instantiating BrightnessSystem`** và hoàn tất toàn bộ chuỗi khởi tạo (`HID event processing ready`, `ALS sync ready`, `display MiG server ready`, `HID server ready`, `system shell servers ready`, `Data Migration`).
   - **`SpringBoard[6]` đã kết nối và check-in thành công với `backboardd[11]`:**
     ```text
     backboardd[11:309] [SystemShell] checkInAndWaitForDataMigration: Telling "com.apple.springboard" that it can start immediately
     backboardd[11:309] [SystemShell] START WATCHDOG for com.apple.SpringBoard
     backboardd[11:309] [SystemShell] Updating primary system app to: <BKSystemShellClient: 0x1009bb280; bundleIdentifier: com.apple.springboard; ... pid: 6; ... checkedIn: YES>
     ```
4. **Điểm cần xử lý tiếp theo để hiển thị Homescreen (Next Step):**
   - Ngay sau khi `SpringBoard[6]` check-in (`10:35:23`), log ghi nhận:
     ```text
     backboardd[11:309] [Display] Set Blanked: YES
     backboardd[11:371] [Backlight] Setting CoreBrightness backlight factor:0.000000 with fade duration:0.000000
     hakios: -[CAWindowServerDisplay setBlanked:1] on 0x10097dc10 -> FORCING 0 (unblank)
     backboardd[11:371] [Backlight] Set BrightnessSystem property:DisplayBrightnessFactor to:0
     backboardd[11:371] [Backlight] Update last requested backlight factor: 0 permanently:YES
     backboardd[11:371] [TouchEvents] canceling events for suppression reason: backlight dim/off
     ```
   - Mặc dù `-[CAWindowServerDisplay setBlanked:]` đã được ép về `0` ở tầng `QuartzCore`, tầng quản lý đèn nền của `backboardd` / `SpringBoard` (`BKSDisplayServicesSetScreenBlanked` / `DisplayBrightnessFactor = 0` / `SBBacklightController`) vẫn chuyển trạng thái logic của hệ thống sang `backlight dim/off` (tắt màn hình chờ sự kiện nút nguồn đánh thức và hủy sự kiện cảm ứng).
   - **Giải pháp ở phiên tới (thực hiện trong `build/hakios-xnu-kernel/userland/src/inject.c` và `inject_sb.c`, sau đó xuất patch bằng `./build.sh export-patches`):**
     1. Trong `inject.c` (`backboardd`): Hook phương thức đặt `DisplayBrightnessFactor` / `setProperty:forKey:` của `BrightnessSystem` (hoặc `BKDisplayController`) để ép `DisplayBrightnessFactor` luôn bằng `1.0` (không bao giờ về `0`) và chặn `BKSDisplayServicesSetScreenBlanked(1)`.
     2. Trong `inject_sb.c` (`SpringBoard`): Đảm bảo gọi đánh thức màn hình (`-[SBBacklightController turnOnScreenFullyWithBacklightSource:]` / gửi sự kiện nút Home/Power giả lập) ngay sau khi `SpringBoard` hoàn tất `applicationDidFinishLaunching:`.

## Session: drivers on, root shell, brightness hook (hardware test of ./build.sh output)

- `configs/picasso.config`: BUTTONS, BATTERY, PMIC, NOVATEK, PICASSO_WLAN on. `soc.args`: `thermal_tm0=0xc222000 thermal_tm1=0xc223000`.
- Verified on the phone: buttons driver reaches step 5 (PM8150 pon + PM8150L GPIO5), `pmic` lists 277 peripherals + RTC,
  `thermal` sees 27 sensors in the two TSENS blocks. NOVATEK touch still fails: SPI error rc=-5 in all 4 modes
  (suspect: the driver votes the wrap1 clocks 0x18004/0x18008, bits 20/21 of 0x52008, while SE 0x984000 is wrapper 0).
- The kernel keeps only ~600 bytes of boot-args: `tools/boot_gui_full.py` puts what the test needs before the `soc_*` addresses.
- Root shell: the default `rc.jobs` (svc_test, surf_test, ...) stalls and init never reaches `interactive_shell()`.
  `userland/rc.jobs.picasso` (no tests, no lock-button press) is built in via `RC_JOBS`; use `tools/console.py "id" "ls /redwood/bin"`.
  One `ps -A` / `id` sequence once ended with the phone resetting to xnuloader: not understood yet.
- inject.c: hook on `-[BrightnessSystemClient setProperty:forKey:]` turns DisplayBrightnessFactor 0 into 1.0. It never logged a hit:
  backboardd sets the factor through another path (BKDisplayBrightnessController). Screen state after check-in is NOT confirmed yet.
- TODO: confirm screen state, press_power effect, NOVATEK clocks, thermal temperatures (thermal_log=N).

## Session: WCN3990 Wi-Fi bring-up — T1/T5 fixes, payload repack (2026-11-06, evening)

State: kernel is built and patches exported, but the phone has NOT been booted yet. It is
wedged in the 12:40 kernel at `dlil_init` (USB `1209:0001`, CDC ttyACM0 silent); `fastboot
devices` is empty and nothing on the host can reset it.

### Crashing / hanging, and what was fixed

1. **`dlil_init` hang (T1).** The kernel stops at
   `dlil_init: Waiting for all the create dlil kernel threads to get scheduled at least once.`
   `tools/boot_wifi_scan.py`'s `CMDLINE` had no `sweettick=1`. On this port PPI 27 is a group-0
   interrupt owned by the secure side, so there is no timer interrupt at all: `sweettick_start()`
   is what pends the group-1 SPI 700 that runs `rtclock_intr()`. Without the boot-arg
   `sweettick_start()` returns at its `PE_parse_boot_argn` guard, `assert_wait_timeout` pends
   nothing, and the `msleep` in `dlil_init` never wakes. `tools/boot_wifi_scan.py` now writes
   `sweettick=1` in the kernel cmdline.
2. **The kernel cmdline was being truncated (found while fixing T1).** `xnuloader/loader.c`
   writes its own words (`debug=0x8`, `cpus=1`, `rd=md0`, `hv_wdt_pet=1`, `hakios_kver=`) BEFORE
   the payload's arguments, and `BOOT_LINE_LENGTH` is 608. The old `boot_wifi_scan.py` line
   (`press_power=40 loader_return_after=86400` + all of `soc.args`) came to ~808 characters, so
   ~187 characters of the tail (the `soc_*` MMIO addresses) were dropped silently. The tool now
   drops the two loader-only xl words, refuses to stage when the line would overflow, and prints
   the count (`405 of 607`). `loader.c`'s own writes now stop one byte short of the limit instead
   of overrunning it.
3. **CE register reads were pointers 3 MiB past the mapping (T2).** `picasso_wlan_ce_dump()` and
   `picasso_wlan_ce_poll()` indexed CEs as `s_wlan_ce_base + WLAN_CE0_PA`. The mapping starts AT
   `WLAN_CE_BASE_PA = 0x18800000`, so that is `0x18800000 + 0x480000 = 0x18c80000`, three
   megabytes past the end of the 3 MiB mapping: the first read faults. A new
   `ce_off_of(ce) = ce_pa_of(ce) - WLAN_CE_BASE_PA` is used by the dump, the poll and
   `ce_kick_src`; the CE MMIO reads in the dump now stay inside the mapping.
4. **A panic never reset the phone (T5).** `PE_halt_restart` is NULL on this port (nothing
   assigns it), so `PEHaltRestart(kPEPanicRestartCPU)` in `debugger_collect_diagnostics` returns
   -1 and control falls through to `panic_spin_forever()` -> `loader_return()`. The CDC console
   went silent and the device stayed wedged. `panic_spin_forever()` now calls
   `picasso_usb_panic_reset()` first; that drains the panic stream, arms the IMEM fastboot reason
   (`picasso_usb_arm_fastboot_reason`, with a readback check), prints `PANIC FLUSH COMPLETE:
   FASTBOOT REASON ARMED`, and resets through `picasso_usb_system_reset()` (PSCI
   `SYSTEM_RESET` -> APSS watchdog -> `pshold_reset` -> `loader_return` last). Verified by
   reading the code, not on hardware yet.

### Unchanged, deliberate
- CE init is still READ-ONLY (`picasso_wlan_ce_dump()`, no ring writes); `WLFW_WLAN_MODE_REQ`
  is still not sent unless `g_ce_bus_init`, which nothing sets, so the WLAN FD stays unarmed.
  So the CE dump on this run only shows what the target brought up, which is exactly what T2 needs.

### Patch/build evidence (no hardware yet)
- `./build.sh export-patches`: all 6 `[OK]`; `0002-xnu-picasso-usb-cdc-console-driver.patch`
  is 101,196 bytes (non-empty); `0001` 109 KB, `0003` 145 KB, `0004` 98 KB, `0005` 54 KB.
- `./build.sh check`: `provenance: 632 tracked files, 29 explained binaries, 11 warnings,
  0 problems`; all 6 upstream repos clean at their expected commits.
- `./build.sh kernel`: `hakios-kernel` (15,670,648 B) and `loader.efi` (71,680 B) both
  rebuilt at 13:26 (known `exit status 2` is the kext symbol-set check, as always).
- `payload.bin` repacked from that kernel at 13:28 (21,919,648 B) and its bytes contain
  `...cpus=8 small_cores_after=0 sweettick=1 mfb=0 ...`, so the staged image really carries the
  tick boot-arg.

### Next step (needs the phone)
RAM-boot `tools/boot_wifi_scan.py` when `fastboot devices` shows `xnuloader` again. Evidence to
look for in `build/wifi_scan.log`:
1. `sweettick: SPI 700 pended every 1 ms` and then `dlil_init: All the created dlil kernel
   threads have been scheduled at least once. Proceeding.` -> T1 is fixed.
2. `root@hakios-picasso` on the CDC console -> the kernel reached userland with `wifi=1`.
3. `picasso_ce: control = <hex>` plus the `CE*.SR_BASE_LO/SR_SIZE/DR_*/CURRENT_*RRI` lines.
   CONTROL == 0 means the target never armed the CEs (then look at WLAN_MSA / `modem_wlansmmu=`
   and whether the FD handshake needs `WLFW_WLAN_INI`/`host_version`); CONTROL != 0 with sane
   bases means T3 (arm CE0 alone behind `ce_arm=1`) is the next experiment.

---

## 3. Wi-Fi bring-up — session 2026-11-06, branch muse/wifi-bringup-t1-t5 (RAM-boot only)

All boots below are RAM-only (fastboot stage + fastboot oem run, never flash/erase).
Working boot combination that reaches a root shell (found 14:11):
mfb=1 ca_vbl=1 adreno=22 gpu=1 adreno_keep=1 wifi=1 modem_wlansmmu=2 wifi_scan=1 launchdsuffix=release
(cmdline 454/607), via tools/boot_wifi_gui_test.py (copy of boot_wifi_scan.py with GUI flags).
reboot-fastboot via tools/console.py works and returns the phone to xnuloader alone.

### 3.1 What was already verified (PASS)
- T1 FIXED (13:48 boot): sweettick: SPI 700 pended every 1 ms, dlil_init: All the created dlil kernel threads have been scheduled at least once. Proceeding., en0 attached on Picasso WCN3990 Wi-Fi, Darwin Wi-Fi interface en0 ATTACHED SUCCESSFULLY.
- Root shell reachable: === init: process 1, started as /sbin/launchd ===, 22 checks 1 failed, root@hakios-picasso prompt, wifi status = WCN3990 DISCONNECTED (IDLE).
- Modem up: MODEM READY, WLFW FW_READY_IND (0x21) RECEIVED, about 4.2-4.4 s after modem start.
- wifi scan runs through apple80211 to the driver (SCAN_REQ answered, SCAN_RESULT error 5).

### 3.2 Experiments that FAILED (each with its evidence)
1. Missing system.apfs hypothesis — FAIL (14:01-14:05). Staged system.apfs first (fastboot oem ramdisk, 2,427,872 KB OKAY in 95.5 s), then payload. Hang IDENTICAL at load_init_program: attempting to load /usr/appleinternal/sbin/launchd.development. Only difference: vm_swap_create_file failed at 198 secs (was at 88 s), free pages dropped. Conclusion: missing system.apfs was NOT the (only) cause.
2. launchdsuffix=release — FAIL to reach init (14:06-14:08). Cmdline 427/607. Kernel now attempts /sbin/launchd instead of launchd.development, but still stalls there with NO init output at all (no === init: process ... ===). Init is static Mach-O (NOUNDEFS, 85 KB, /sbin/launchd -> init); its main() first opens /dev/console then prints. Conclusion: hang is in exec of init itself or in open("/dev/console"), not in which launchd path is chosen.
3. Wi-Fi cmdline mfb=0 ca_vbl=0 adreno=0 gpu=0 — FAIL, root cause of the init hang. With those flags init never runs; with GUI flags (mfb=1 ca_vbl=1 adreno=22 gpu=1 adreno_keep=1) init runs immediately. Same kernel, same root.apfs. Conclusion: the Wi-Fi cmdline display/GPU flags broke init, not launchd choice or system.apfs.
4. modem_wlansmmu sweep — FAIL, all three identical (14:31-14:36). =1 (FW_READY 4365 ms), =0 (FW_READY 4320 ms), =2 (FW_READY 4174 ms): all reach root shell + FW_READY_IND, all hang at CE dump with GLINK loop stalled, no picasso_ce, no RADIO. Conclusion: SMMU setting is NOT the cause.
5. CE mapping fix — FAIL (14:37-14:42). In drivers/wifi/hakios.picasso_wlan.cpp: enlarged WLAN_CE_SIZE 0x300000 -> 0x500000; ce_off_of() now returns the offset directly instead of ce_pa_of(ce) - WLAN_CE_BASE_PA (which underflows as uint32); dump control read moved from base+0x0 (not a CE register) to CE0 CE_SR_BASE_LO; picasso_wlan_wait_ready() now maps WLAN_CE_BASE_PA + WLAN_CE0_PA (0x18c80000) instead of ml_io_map(0x480000); g_ce_bus_init is now set true after a successful dump (it was NEVER set anywhere before — only declared line 813 and checked line 2732 — so WLFW step 9 would always abort RADIO STAYS DOWN even if CE init had succeeded). Kernel rebuilt 14:37 (./build.sh kernel exit 0, known Error 2 kext symbol-set only, upstream clean) and booted (Darwin Kernel 14:37:34). Result: STILL NO picasso_ce dump. CE init logs Mapped WLAN CE MMIO at 0xfffffffa46b68000 (PA 0x18800000) (new VA confirms the larger mapping), then hangs again, GLINK stalled beat 4331. Conclusion: mapping-size/offset fix alone does NOT make hardware respond at 0x18c80000.
6. Direction 1 — alt CE offset 0x240000 — FAIL (14:46-14:48). ce_pa_of() changed to return WLAN_CE*_OFFSET (0x240000/0x241000/0x242000/0x243000) and wait_ready to map base+OFFSET. Kernel built 14:47, booted (Darwin Kernel 14:46:58), FW_READY_IND 4355 ms. Result: STILL NO picasso_ce dump, same hang after Mapped WLAN CE MMIO. Conclusion: hardware does not respond at 0x18a40000 either.
7. Direction 2 — CE init after WLAN_CFG — FAIL, with a NEW upstream finding (14:52-14:58). Removed the picasso_wlan_ce_init() call from the FW_READY_IND handler (line 4031); CE init now only runs in WLFW step 9, after WLAN_CFG (step 8). Reverted alt offset to conventional 0x480000. Kernel built 14:52, booted (Darwin Kernel 14:52:13). First boot: FW_READY_IND never arrived, phone auto-reset to xnuloader on its own (flaky — no panic text in log). Retry: FW_READY_IND received 4184 ms, step 8 ran, then WLFW resp 23 result 1 error 1 and WLFW step 8 refused (msg 23 error 1); stopping — the firmware REFUSED WLAN_CFG (msg 0x23), so wlfw_state = 0, step 9 never runs, CE init never runs, no dump, no RADIO. Conclusion: WLAN_CFG itself is refused by the firmware, upstream of any CE MMIO read. The CE dump hang in earlier boots masked this: CE init hung on FW_READY_IND before step 8 could run.

### 3.3 Current state (14:58)
- No picasso_ce dump has ever been obtained on any boot. wifi scan with wifi_scan=1: picasso_sta_scan: Copy engines not initialised, nothing to scan with, 0 networks. Without wifi_scan=1: OTA scan is off (boot-arg wifi_scan=1 enables it); no BSS heard.
- Phone is up on the direction-2 retry kernel (root shell, ttyACM0, GLINK stalled beat 8006 action 259), capture tools/boot_wifi_gui_test.py still running at last check.

### 3.4 Next steps
- Investigate the WLAN_CFG refusal: contents of wlfw_ce_tbl (12 CEs, CE 7 is {7, 0, 0, 0, 0}), wlfw_svc_tbl (21 entries + {0,0,0} terminator), wlfw_shadow_tbl, and the TLV lengths/sizes sent in step 8 — compare against Linux ath10k qmi_wlan_cfg / WCN3990 firmware expectations. A zero CE entry or a wrong table size is the prime suspect for error 1.
- If WLAN_CFG is fixed and step 9 runs, re-test the CE dump: if it still hangs at the first CE read, the CE block is clock/power gated or at a different PA on SM7250 — check the WLAN clock/power enable sequence that runs before CE init, and whether the firmware arms CEs only after MODE_REQ (chicken-and-egg with the g_ce_bus_init gate).
- Do NOT arm CE0 (ce_arm=1) before a read-only dump works.

### 3.5 WLAN_CFG refusal investigation (15:03-15:20)
Steps 1-6 of WLFW all succeed (IND_REGISTER, MSA_INFO with 2 memory regions, MSA_READY, CAP, BDF download 26328 bytes in 7 segments, CAL_REPORT — all resp result 0 error 0). Only step 8 (WLAN_CFG, msg 0x23) is refused: WLFW resp 23 result 1 error 1, WLFW step 8 refused; stopping. Modem bring-up itself is flaky: on some boots FW_READY_IND never arrives and the phone auto-resets to xnuloader (no panic text); on retry it arrives at about 4.2-4.4 s. One retry boot also stalled at RPMh rail voting (mpss: about to ask RPMh for cx.lvl/mss.lvl level 384) with no further modem lines before reset.
8. WLAN_CFG shadow/svc fixes — FAIL (15:05-15:12). Two defects found by comparing with Linux ath10k (qmi.h, snoc.c, qmi_wlfw_v01.h): (a) wlfw_shadow_cfg was 2x u32 (8 bytes) but step 8 copied only 4 bytes per entry, so reg_offset was never sent (always 0 instead of 0x3C SRC / 0x40 DST); fixed struct to u16 ce_id + u16 reg_offset (4 bytes, matching ath10k_shadow_reg_cfg). (b) wlfw_svc_tbl had a {0,0,0} terminator that was sent as a 21st service entry (service_id 0); removed it, buffer 21*12 -> 20*12. Kernel built 15:05, booted; first boot FW_READY never arrived and phone reset to fastboot; clean retry 2: FW_READY 4351 ms, step 8 STILL refused (result 1 error 1). Conclusion: shadow reg_offset and svc terminator were real defects but NOT the cause of the refusal.
9. WLAN_CFG svc field-order fix — FAIL (15:15-15:19). QMI wlfw_ce_svc_pipe_cfg_s_v01 wire order is service_id, pipe_dir, pipe_num, but our struct was service_id, pipe_num, pipe_dir, so every service entry was sent with pipe_num and pipe_dir swapped (e.g. service 0x0104 was sent as pipe_dir 3 INOUT / pipe 2 instead of pipe 3 OUT). Verified against ath10k snoc.c target_service_to_ce_map_wlan (20 entries, same pipes/directions/order as our table intent) and qmi_wlfw_v01.h. Fixed struct order and rewrote all 20 svc entries as {service_id, pipe_dir, pipe_num}. Kernel built 15:15, booted (Darwin Kernel 15:15:12), FW_READY_IND 4208 ms, step 8 STILL refused (result 1 error 1). Conclusion: svc field order was a real defect but NOT the cause of the refusal either.
Remaining WLAN_CFG suspects (not yet tested): tgt_cfg CE flags (ours 0 / 8, ath10k target_ce_config uses CE_ATTR_FLAGS 0x07 base, so CE0-3,6,9,10,11 should be 0x07 and CE4,5,7 should be 0x0F), CE7 pipedir (ours 0, ath10k has 4, host-only diag window — note 4 is not a valid QMI pipedir, so sending it is risky), missing host_version TLV (0x10) in the WLAN_CFG request, missing shadow_reg_v2 TLV (0x14), or a prerequisite message the FW wants first (HOST_CAP msg 0x34). Next diagnostic: hex-dump the actual WLAN_CFG request bytes the driver sends (qm_send / step 8) and compare byte-for-byte with the ath10k QMI encoding before changing more tables.

### 3.6 WLAN_CFG hex-dump diagnostic (15:26-15:35)
Added a temporary hex-dump in wlfw_step case 8 (WLFW_CFG_DUMP lines, 32 bytes each, remove after diagnosis). Note: the Muse VM was replaced at 15:24 and /tmp/tunnel.py + /tmp/askpass.sh were lost; recreated them under ~/workspace (tunnel.py with select timeout 300 / dynamic fd list, askpass.sh prints SSHPASS) so they survive VM replacement. First kernel build of the dump failed (BUILD EXIT=1): the heredoc had stripped the double quotes from the C string literals — fixed by sending the patch via stdin instead of a shell heredoc. Rebuilt kernel 15:30, booted; FW_READY_IND 4502 ms, step 8 STILL refused (result 1 error 1).
Decoded the 544-byte WLAN_CFG request from the dump (log build/wifi_scan_gui.log, WLFW_CFG_DUMP off 0..512):
- Header: QMI_REQ (0x00), txn 13, msg 0x23, payload length 537 (filled by qm_send, was 0 in the dump).
- TLV 0x11 tgt_cfg, 240 bytes, 12 CEs: every entry matches ath10k target_ce_config_wlan (pipe_num, pipe_dir, nentries, nbytes_max) except flags (ours 0 / 8 for CE4-5, ath10k uses CE_ATTR_FLAGS 0x07 base so 0x07 / 0x0F) and CE7 pipedir (ours 0, ath10k 4).
- TLV 0x12 svc_cfg, 240 bytes, 20 services: every entry matches ath10k target_service_to_ce_map_wlan with the correct field order (service_id, pipe_dir, pipe_num) after the fix in 3.5.
- TLV 0x13 shadow_reg, 48 bytes, 12 entries: ce_id + reg_offset all correct (0/3/4/5/7 at 0x3C, 1/2/7/8/9/10/11 at 0x40).
Conclusion: the WLAN_CFG request is byte-correct per QMI and ath10k (apart from the two flag/pipedir questions above), yet the firmware still refuses it with error 1. So the refusal is NOT in the table contents — it must be a missing TLV or a missing prerequisite step. Prime suspects now: missing host_version TLV (0x10, ath10k always sends it), missing shadow_reg_v2 TLV (0x14), or a prerequisite message the FW wants first (HOST_CAP msg 0x34). Next test proposed: add host_version TLV 0x10 to the WLAN_CFG request.

### 3.7 Hai hướng WLAN_CFG đều FAIL (15:50-16:02) — request giờ khớp ath10k nhưng FW vẫn từ chối
User: "Thử lần lượt 2 hướng".
Hướng 1 — host_version TLV 0x10: thêm `qm_tlv(&m, 0x10, "ATH10K", 6)` trước tgt_cfg trong step 8 (ath10k luôn gửi host_version). Kernel 15:52, boot: FW_READY 4529 ms, dump xác nhận TLV 0x10 đã lên dây (`10 06 00 41 54 48 31 30 4b`), nhưng step 8 VẪN `WLFW resp 23 result 1 error 1` và refused. FAIL.
Hướng 2 — flags CE + CE7 pipedir: sửa wlfw_ce_tbl theo ath10k target_ce_config_wlan: flags 0 -> 7 (NO_SNOOP|BYTE_SWAP|SWIZZLE) cho CE0-3,6,8-11; 8 -> 15 (7|DIS_INTR) cho CE4,5,7; CE7 {7,0,0,0,0} -> {7,4,0,0,15} (pipedir 4 = host-only diag). Kernel 15:56, boot: FW_READY 4770 ms, dump xác nhận flags mới đã lên dây (CE0/CE1/CE2 flags 07, CE7 pipedir 04 flags 0f), nhưng step 8 VẪN `WLFW resp 23 result 1 error 1` và refused. FAIL.
Kết luận: request WLAN_CFG giờ đã khớp ath10k gần như hoàn toàn (host_version + tgt_cfg flags + svc_cfg + shadow_reg) mà FW vẫn báo error 1 (QMI_ERR_MALFORMED_MSG). Vậy lỗi KHÔNG nằm trong nội dung WLAN_CFG — phải là một bước điều kiện trước đó bị thiếu. Nghi phạm mạnh nhất còn lại: HOST_CAP (msg 0x34). Bằng chứng: comment trong driver tự ghi sequence icnss là `ind_register -> msa_info -> hyp_assign -> msa_ready -> cap -> host_cap`, handler response có sẵn case HOST_CAP ("not supported by FW, continuing"), nhưng grep cho thấy WLFW_HOST_CAP KHÔNG BAO GIỜ được gửi làm request (chỉ có #define và handler) — wlfw_step chỉ có case 1-9, không có case nào gửi 0x34. ath10k cũng gửi host_cap ngay khi server arrive, trước wlan_cfg. Hướng tiếp theo đề xuất: thêm bước gửi HOST_CAP (daemon_support=0, bản 8-bit cho FW cũ) sau CAP, trước BDF. Các nghi phạm khác: shadow_reg_v2 TLV 0x14, hoặc BDF/board_id không khớp khiến FW từ chối config.

### 3.8 Hai hướng WLAN_CFG tiếp theo đều FAIL (16:01-16:22) — HOST_CAP và shadow_reg_v2
User: "thử lần lượt cả 2 đi".
Hướng 1 — HOST_CAP (msg 0x34): thêm step 5 mới gửi HOST_CAP sau CAP (daemon_support=0, bản 8-bit cho FW cũ, TLV 0x10 len 1), đánh lại số step 1..10 (BDF 6, CAL_REPORT 7, chờ FW_READY 8, WLAN_CFG 9, CE/MODE 10) và cập nhật mọi chỗ tham chiếu wlfw_state. Kernel 16:04, boot: `WLFW req 34 (step 5)`, FW trả `WLFW HOST_CAP not supported by FW (error 1), continuing` (driver tiếp tục như thiết kế), FW_READY 4478 ms, WLAN_CFG (step 9) VẪN `WLFW resp 23 result 1 error 1` và refused. FAIL. Kết luận phụ: FW này không hỗ trợ HOST_CAP (khớp quirk ath10k cho FW Xiaomi Poco F1, skip host_cap), nên gửi HOST_CAP không mở được WLAN_CFG.
Hướng 2 — shadow_reg_v2 (TLV 0x14): thêm TLV 0x14 với 12 địa chỉ tuyệt đối (u32) = 0x18800000 + CE offset + reg_offset. Lần 1 bị lỗi: `ce_pa_of()` chỉ xử lý CE0-3, trả 0 cho CE4+ nên 8/12 địa chỉ sai (0x1880003c/0x18800040) — build lỗi trước đó còn do WLAN_CE_BASE_PA/ce_pa_of chưa được khai báo ở vị trí step 9 (đã sửa bằng forward declaration + literal 0x18800000). Sửa `ce_pa_of()` cho đủ CE0-11 (thêm define WLAN_CE4_PA..CE11_PA 0x484000..0x48b000 theo stride 0x1000) và sửa CE8 flags về 0 (ath10k target config có CE8 flags=0, bản trước để 7). Kernel 16:18, boot: dump xác nhận cả 12 địa chỉ đúng trên dây (0x18c8003c, 0x18c8303c, 0x18c8403c, 0x18c8503c, 0x18c8703c, 0x18c81040, 0x18c82040, 0x18c87040, 0x18c88040, 0x18c89040, 0x18c8a040, 0x18c8b040), FW_READY 4516 ms, WLAN_CFG VẪN `WLFW resp 23 result 1 error 1` và refused. FAIL. Lưu ý đã xác minh: ath10k KHÔNG gửi shadow_reg_v2 cho WCN3990 (không có v2 map trong snoc.c, không có field v2 trong ath10k_qmi_wlan_enable_cfg) — nên đây vốn là long shot, giờ đã loại trừ.
Tổng kết: request WLAN_CFG giờ chứa NHIỀU hơn ath10k (host_version + flags ath10k + shadow_reg + shadow_reg_v2) mà FW vẫn từ chối error 1. Lỗi không nằm trong nội dung WLAN_CFG và không phải do thiếu HOST_CAP/v2. Nghi phạm mới từ log: `WLFW CAP: chip_id=0x320 chip_family=0x4001 board_id=0xff soc_id=0x400b0000` — board_id=0xff (unknown) nên driver chọn BDF generic 'bdwlan.bin' (26328 bytes); các candidate bdf_g7a/g7ac/j9/j9a đều cùng 26328 bytes. Hướng tiếp theo đề xuất: kiểm tra wlfw_select_bdf và thử các BDF variant khác, vì BDF/calibration sai có thể khiến FW từ chối WLAN_CFG dù BDF download trả result 0.

## 3.9 BDF / board_id investigation (2026-11-06, on k60) — ALL 4 named variants FAIL

Hypothesis: CAP reports board_id=0xff (unknown), so wlfw_select_bdf falls back to generic
'bdwlan.bin' (26328 bytes). A wrong BDF/calibration could make FW refuse WLAN_CFG even
though BDF download returns result 0.

- Modem image/ holds 84 BDF files, all 26328 bytes: bdf_g7a.bin, bdf_g7ac.bin, bdf_j9.bin,
  bdf_j9a.bin, bdwlan.bin, bdwlan.102/104/105/106/107/108/109/10e/10f/202, and 24+ bdwlan.bXX
  (b04 b07 b09 b0a b0b b0d b0e b0f b14 b15 b30 b31 b32 b33 b34 b35 b36 b37 b38 b4c b4d b53 b58 b59 b5c b5f).
- wlfw_select_bdf (line ~2489) tries bdwlan.b<board_id> then bdwlan.bin/b00/b30/b01; it never
  tries the bdf_<name>.bin files. Added a numeric boot-arg override `bdf_sel` (PE_parse_boot_argn):
  1=bdf_g7a.bin, 2=bdf_g7ac.bin, 3=bdf_j9.bin, 4=bdf_j9a.bin (driver md5 f75b496fdf19649ff40606ab848a180d).
- Baseline (k60, v2 kernel, bdwlan.bin): FW_READY 4434 ms, WLAN_CFG (step 9) refused result 1 error 1.
  Confirms the k60 boot pipeline works and reproduces the refusal.
- k60 build fixes needed (repo was rsynced from id-it-tritm-2, absolute paths broke the build):
  * build/hakios-xnu-kernel/devices/picasso was a symlink to /home/tritm/hakios-picasso (broken on
    k60) -> re-pointed to /root/hakios-picasso; hakios-device.py check picasso then passes.
  * Incremental build failed: BUILD/obj Makefiles embed /home/tritm paths ("No rule to make target
    '/home/tritm/.../fasttrap_isa.c'") -> ran ./build.sh kernel --clean (BUILD-EXIT:0, kernel 18:24).
  * --clean deleted build/hakios-xnu-kernel/userland/root.apfs -> ran ./build.sh userland
    (BUILD-EXIT:0, root.apfs 4.0M + payload.bin 19.9M regenerated 18:28; system.apfs 2.4G intact).
  * Note: clean payload lacks fw/ files ("fw/: 0 files ... absent: gpu wifi haptic camera audio")
    because fw-audio/fw-gpu symlinks are broken/empty; Wi-Fi firmware comes from the modem
    partition, not from payload fw/, so this does not block the Wi-Fi test.
- Results (all with wifi_scan=1, GUI flags, launchdsuffix=release; FW_READY then step 9 refused):
  * bdf_sel=1 bdf_g7a.bin : FW_READY 6631 ms, resp 23 result 1 error 1 — FAIL
  * bdf_sel=2 bdf_g7ac.bin: FW_READY 6623 ms, resp 23 result 1 error 1 — FAIL
  * bdf_sel=3 bdf_j9.bin  : FW_READY 6893 ms, resp 23 result 1 error 1 — FAIL
  * bdf_sel=4 bdf_j9a.bin : FW_READY 6556 ms, resp 23 result 1 error 1 — FAIL
- Conclusion: BDF variant does NOT affect the WLAN_CFG refusal. All 5 BDFs (incl. generic
  bdwlan.bin) give the identical refusal (result 1 error 1, QMI_ERR_MALFORMED_MSG). The
  BDF/calibration hypothesis is RULED OUT for the named variants. The refusal must come from
  the WLAN_CFG message itself or a missing prerequisite step, not from which BDF was downloaded.
- Still open: HOST_CAP u32 variant (low confidence); the 24+ bdwlan.bXX board-specific files
  were NOT tried (only the 4 named Xiaomi ones); a deeper QMI comparison (e.g. exact TLV order
  or a missing TLV the FW expects beyond what ath10k sends) is the remaining direction.

## 3.10 Deep QMI comparison vs ath10k (2026-11-06, no hardware test yet)

Sources: qmi_wlfw_v01.h + qmi.h + snoc.c (ath10k, almalinux kernel), qmi.c
(ath10k_qmi_wlan_enable / ath10k_snoc_wlan_enable).

- WLAN_CFG (msg 0x23) content VERIFIED byte-for-byte against ath10k:
  * CE tgt_cfg (TLV 0x11, 12x20B): matches target_ce_config_wlan exactly
    (CE0-3,6,9,10,11 flags 7; CE4,5,7 flags 15; CE7 pipedir 4 host-only diag;
    CE8 flags 0; CE4 {256,256}, CE5 {1024,64}, CE6 {32,16384}).
  * SVC cfg (TLV 0x12, 20x12B): matches target_service_to_ce_map_wlan exactly
    (WMI VO/BK/BE/VI/CONTROL, RSVD_CTRL, HTT_DATA/DATA2/DATA3/LOG).
  * Shadow reg (TLV 0x13, 12x4B): matches target_shadow_reg_cfg_map exactly
    (SRC 0x3C: CE0,3,4,5,7; DST 0x40: CE1,2,7,8,9,10,11; CE7 twice, no CE6).
  * QMI header correct (QMI_REQ=0, txn u16, msg 0x23, length = payload).
  * qm_send length = m->n - 7, correct. QRTR transport carries larger BDF
    segments fine, so no truncation.
- Deviations from ath10k found (WLAN_CFG is NOT byte-identical yet):
  1. host_version TLV 0x10 ("ATH10K", 6B): ath10k passes version=NULL to
     ath10k_qmi_wlan_enable, so it OMITS host_version. We send it. (Note:
     the pre-15:50 request without host_version also failed, so this alone
     is not the root cause.)
  2. shadow_reg_v2 TLV 0x14 (12x u32): ath10k does NOT send it for WCN3990
     (no v2 field in ath10k_qmi_wlan_enable_cfg). We send it. (Pre-v2 request
     also failed, so not the root cause alone.)
  3. svc count: ath10k sends 21 entries INCLUDING the {0,0,0} terminator
     (num_ce_svc_pipe_cfg = sizeof(map)/sizeof(entry) = 21). We removed the
     terminator and send 20. (Original with terminator also failed, so not
     the root cause alone.)
- HOST_CAP quirk CONFIRMED by a 2026 ath10k patch (lkml, device-tree quirk
  to skip host_cap): some FW (e.g. WLAN.HL.2.0.c3-00257 on Xiaomi Poco F1)
  returns QMI_ERR_MALFORMED_MSG for HOST_CAP, and it is NON-FATAL — Wi-Fi
  still comes up if the request is skipped. Our FW behaves the same
  (HOST_CAP refused error 1, we continue). So HOST_CAP is not the blocker.
- MSA: ours 0x8BA00000 size 2MB (qcom,wlan-msa-fixed-region) vs ath10k
  msa_size 1MB; FW accepts MSA_INFO/MSA_READY and FW_READY arrives, so MSA
  handshake completes. Size difference unlikely to cause WLAN_CFG refusal.
- Conclusion: WLAN_CFG content is essentially ath10k-correct, yet FW still
  returns MALFORMED_MSG. The remaining candidates are: (a) the three
  deviations above combined (make WLAN_CFG byte-identical to ath10k and
  re-test); (b) a missing prerequisite step or wrong value in an earlier
  message (MSA/BDF/CAL_REPORT); (c) this Xiaomi FW version expects a
  different WLAN_CFG than the ath10k reference (need raw CAP dump for
  fw_version / fw_build_id to identify it).
- Next step proposed: make WLAN_CFG byte-identical to ath10k (drop TLV 0x10
  and 0x14, restore svc terminator -> 21 entries), rebuild, RAM-boot once.
  If still refused, add a raw CAP hex-dump (msg 0x24) to read fw_version.

## 3.11 Byte-identical WLAN_CFG test (2026-11-06 19:28-19:32) — FAILED

- Applied the byte-identical fix (driver md5 33adede308803ada2e7aabd191d9befc):
  dropped host_version TLV 0x10, dropped shadow_reg_v2 TLV 0x14, restored
  svc terminator {0,0,0} -> 21 entries. Kernel rebuilt (hakios-kernel
  15,654,456 B, 19:28), RAM-booted 19:31 (Darwin Kernel 19:27:59).
- WLFW_CFG_DUMP confirms the request is byte-identical to ath10k:
  556 bytes = header 7 + TLV 0x11 240B (12 CE) + TLV 0x12 252B (21 svc,
  incl. terminator) + TLV 0x13 48B (12 shadow). No 0x10, no 0x14.
- RESULT: FW_READY_IND t=4332ms, then WLFW resp 23 result 1 error 1,
  step 9 refused. STILL MALFORMED_MSG. No picasso_ce, GLINK stalled.
- CONCLUSION (decisive): WLAN_CFG content is NOT the problem. The request
  is byte-identical to ath10k's and the FW still refuses it. The cause must
  be a prerequisite (earlier message / value the FW validated before
  WLAN_CFG) or this Xiaomi FW version expects a different WLAN_CFG than
  the ath10k reference. Next step: raw CAP hex-dump (msg 0x24) to read
  fw_version / fw_build_id and identify the FW.
- Sweep bXX stopped at user request after 10/25 done (b04 b07 b09 b0a b0b
  b0d b0e b0f b14 b15), all refused identically; the remaining 15 bXX are
  untested but the BDF hypothesis is already ruled out by the 15 BDFs
  tried (4 named + generic + 10 bXX).

- Browser task (ath10k qmi.c, commit 8765967) CONFIRMED the cfg_send_sync_msg
  details: host_version_valid = 0 (version param unused), tgt_cfg/svc_cfg
  copied field-by-field (pipe_num, pipe_dir, nentries, nbytes_max, flags /
  service_id, pipe_dir, pipe_num), shadow_reg via memcpy, shadow_reg_v2
  NEVER set anywhere in qmi.c. The byte-identical fix in 3.11 therefore
  matches ath10k exactly, and its failure is not a request-content issue.

## 3.12 CAP dump — firmware identified (2026-11-06 19:38-19:42)

- Added WLFW_CAP_DUMP (driver md5 299ab1cf60ab8d6f722a26b2d33ddae1), kernel
  rebuilt (15,670,840 B, 19:38), RAM-booted 19:41. CAP response (129 bytes)
  decoded:
  * QMI_RESP txn 4 msg 0x24, result 0 (SUCCESS).
  * TLV 0x10: chip_id=0x320 chip_family=0x4001.
  * TLV 0x12: soc_id=0x400b0000.
  * TLV 0x11 (board_id) ABSENT from the response — the FW does not report
    a board_id, so wlfw_board_id stays at its default 0xff (unknown) and
    the driver picks the generic bdwlan.bin. This explains why no BDF
    variant changed anything.
  * TLV 0x13 (fw_version_info, 21B): fw_version=0x321b002a,
    fw_build_timestamp="2021-04-22 04:40".
  * TLV 0x14 (fw_build_id, 70B): "QC_IMAGE_VERSION_STRING=
    WLAN.HL.3.2.1.c2-00042-QCAHLSWMTPLZ-1.395128.1".
- FIRMWARE: WLAN.HL.3.2.1.c2-00042-QCAHLSWMTPLZ-1.395128.1 (built
  2021-04-22). This is NEWER than the HL.2.0.c3 firmware on the Xiaomi Poco
  F1 that the ath10k HOST_CAP quirk targets. The HL.3.2.1 FW may expect a
  different WLAN_CFG than the ath10k reference (which was written against
  older FW). Next step: research what HL.3.2.1 requires for WLAN_CFG.
- WLAN_CFG still refused (FW_READY 4250ms, step 9 error 1) with the
  byte-identical request, as before.

## 3.13 HL.3.2 workarounds patch — NOT a WLAN_CFG fix (2026-11-06 19:44-19:47)

- Found the patch: "[PATCH 0/2] ath10k: WCN3990 firmware workarounds for
  WLAN.HL.3.2", author Malte Schababerle (NOT Loic Poulain — he only
  commented), 2026-03-22, lore.kernel.org/linux-wireless/20260322124822.
  Status: NOT MERGED (Patchwork "New" as of 2026-11-06).
- It does NOT touch QMI: only thermal.c (skip quiet mode — HL.3.2 advertises
  THERM_THROT but crashes on quiet mode) and mac.c (force passive scan on
  5GHz — active scan doesn't tune radio on 5GHz non-DFS). Both are WMI/
  post-bring-up issues, not the QMI handshake. No change to WLAN_CFG,
  HOST_CAP, MSA, BDF, CAL_REPORT, CE config, or QMI sequence.
- CONCLUSION: this patch is a dead end for the WLAN_CFG refusal. The
  refusal must come from something else — most likely a prerequisite the
  vendor driver (qcacld-3.0, what Xiaomi ships on Android) sets differently
  from ath10k, or a QMI service-version difference. Next candidate: compare
  MSA/CAL_REPORT/BDF against qcacld.

## 3.14 ios-redfin (Pixel 5, SM7250) — Wi-Fi đã chạy được, và so sánh qcacld/icnss (2026-11-06 ~20:00-20:03)
- User chỉ project https://github.com/DenialOfGravity/ios-redfin (iOS port cho Pixel 5 redfin, cùng SM7250). README: "Wi-Fi | The radio is driven from userland (wland); Settings -> Wi-Fi works: wifid joins at boot".
- Code Wi-Fi userland: phone/wifi/wland.c, wlfw.c/h (QMI WLFW), qmi.c/h, glink.c, radio.c (CE), ath.c, pdmapper.c, scm.c, smem.c.
- Browser task qcacld/cnss (19:53-20:03) đã về: driver vendor thật trên SM7250/WCN3990 là **icnss** (drivers/soc/qcom/icnss_qmi.c + icnss.c, LineageOS android_kernel_xiaomi_sm7250 lineage-18.1), KHÔNG phải cnss2 (cnss2 là biến thể PCIe cho QCA6174/6290/6390/6490).
  - MSA vendor: pil_wlan_fw_mem @ 0x8ba00000 size 0x200000 (2 MiB) — KHỚP driver ta (0x8BA00000, 2MB). ath10k dùng 1MB là sai cho SM7250.
  - Vendor icnss KHÔNG gửi HOST_CAP, BDF download, CAL_REPORT qua QMI (firmware+BDF đã trong ảnh PIL-MSA wlan.mdt). Ta gửi 3 message này và đều result 0 nên không phải blocker.
  - WLAN_CFG vendor: 4 TLV host_version (valid=1, chuỗi QWLAN_VERSIONSTR) + tgt_cfg + svc_cfg + shadow_reg v1; KHÔNG gửi shadow_reg_v2. Bảng CE vendor flags 0/8 (CE_ATTR_FLAGS=0, DISABLE_INTR=8), KHÔNG phải 7/15 của ath10k — khớp ios-redfin. svc entry 17 vendor là WDI_IPA_TX 0x0500 (ath10k dùng TEST_RAW_STREAMS 0xFE00).
  - Trình tự vendor icnss: IND_REGISTER -> MSA_INFO -> MSA_READY -> CAP -> DYNAMIC_FEATURE_MASK -> FW_READY_IND -> WLAN_CFG -> MODE_REQ.
  - Ứng viên #1 của báo cáo cho MALFORMED_MSG là host_version vắng mặt. NHƯNG thí nghiệm 3.15 chứng minh nguyên nhân thật là count byte (xem dưới) — host_version không cần thiết.

## 3.15 Count byte trong QMI array TLV — WLAN_CFG ĐƯỢC CHẤP NHẬN (2026-11-06 ~20:03-20:08) — BREAKTHROUGH
- Phát hiện từ ios-redfin phone/wifi/wlfw.c wlan_enable(): mỗi TLV array có 1 byte count ở đầu value (định dạng wire của Linux qmi_encdec.c):
  - TLV 0x11 tgt_cfg: [count u8=12][12x20B] = 241B (ta gửi 240B, thiếu count).
  - TLV 0x12 svc_cfg: [count u8=21][21x12B] = 253B (ta gửi 252B).
  - TLV 0x13 shadow_reg: [count u8=12][12x4B] = 49B (ta gửi 48B).
  - (IDL qmi_wlfw_v01.h khai tgt_cfg_len/svc_cfg_len/shadow_reg_len là u32, nhưng trên dây ios-redfin dùng 1 byte count và FW chấp nhận; CE0.pipe_num=0 nên 3 byte sau count đều 0, u32 read cũng ra 12.)
- Fix: case 9 WLAN_CFG, prepend buf[0]=(uint8_t)nce/nsv/nsh, o=1 rồi copy entries; TLV lengths thành 241/253/49. Driver md5 380a5b655b3772370d0057364509ecea. Build kernel 20:04 OK (15,670,840B).
- Boot 20:07 (wifi_scan=1): **WLFW resp 23 result 0 error 0** — WLAN_CFG ĐƯỢC CHẤP NHẬN lần đầu tiên. FW_READY t=4214ms. Hex dump xác nhận: TLV 0x11 len 0xf1=241, byte đầu 0x0c=12, rồi CE0 {0,2,32,2048,7}.
- Nhưng sau đó CE init (chạy TRƯỚC MODE_REQ ở case 10) vẫn hang ở lần đọc CE đầu tiên, GLINK stalled x5, không picasso_ce, không RADIO. Kết luận: CE block chưa được clock cho tới sau MODE_REQ.

## 3.16 MODE_REQ trước CE init + CE offset 0x240000 — CE dump ĐƯỢC, en0 lên (2026-11-06 ~20:09-20:20) — BREAKTHROUGH
- Fix 1 (MODE-first): xoá picasso_wlan_ce_init() + check g_ce_bus_init khỏi case 10; case 10 chỉ gửi MODE_REQ. Response handler đã có sẵn: khi MODE_REQ ack (state 10->11) thì gọi picasso_wlan_ce_init() + picasso_wlan_net_attach(). ios-redfin cũng làm WLAN_CFG -> MODE_REQ -> CE init. Driver md5 24c4e5c715d07485adf5d25b93349ade. Build 20:10 OK.
- Boot 20:13: WLAN_CFG result 0, **MODE_REQ resp 22 result 0**, "MISSION MODE ATTACH COMPLETE! WLAN RADIO IS ACTIVE!", en0 attached ("net: en0 attached on Picasso WCN3990 Wi-Fi (a radio), link up", "Darwin Wi-Fi interface en0 ATTACHED SUCCESSFULLY!"). Nhưng CE init vẫn hang (Mapped CE MMIO rồi GLINK stalled x5, picasso_ce=0).
- Fix 2 (CE offset): ios-redfin phone/wifi/radio.c: CE_PA = 0x18800000 + 0x240000 = 0x18a40000; CEi = CE_PA + i*0x1000 (mỗi CE 4KB). Register offsets trong CE: SR_BASE_LO 0x00, SR_BASE_HI 0x04, SR_SIZE 0x08, DR_BASE_LO 0x0c, DR_BASE_HI 0x10, DR_SIZE 0x14, CTRL1 0x18, SR_WR_IDX 0x3c, DR_WR_IDX 0x40, CUR_DRRI 0x48, SRC_WM 0x4c, DST_WM 0x50. CE wrapper (RRI) ở 0x18a4c000 (WLAN_PA+0x24c000). SHADOW_PA = 0x18800000+0x32000 = 0x18832000.
  - Driver ta dùng CE0 offset 0x480000 (0x18c80000) là SAI. Offset 0x240000 đã thử lúc 14:46 nhưng trong điều kiện sai (WLAN_CFG bị từ chối + CE init trước MODE_REQ) nên chưa từng được test đúng.
  - Fix: đổi WLAN_CE0..11_PA từ 0x480000..0x48b000 thành 0x240000..0x24b000 (stride 0x1000). wait_ready() dùng define nên tự đổi sang 0x18a40000. Driver md5 18f2616cadc504fcc00a8e06c284c690. Build 20:16 OK (15,670,760B).
- Boot 20:19 (wifi_scan=1): WLAN_CFG result 0 (t=4259ms MODE_REQ result 0), "MISSION MODE ATTACH COMPLETE", en0 attached, rồi **picasso_ce dump ĐƯỢC — 12 dòng** (lần đầu tiên có dump):
  - "picasso_ce: control = 00000000 (0 = the target never brought the CEs up)"
  - CE0.SR_BASE_LO @0x000 = 0, CE0.SR_SIZE @0x008 = 0, CE0.CURRENT_SRRI @0x044 = 0; CE1.DR_BASE_LO/SIZE/CURRENT_DRRI = 0; CE2 tương tự; CE3.SR_BASE_LO/SIZE = 0.
  - "picasso_wlan_ce_init: copy engines read only; no ring armed this boot"
  - **GLINK loop stalled = 0** (hết stall). Tất cả register đọc về 0 — FW chưa program ring; host phải program (xem bước tiếp theo).
- Trạng thái hiện tại: WLAN_CFG ✓, MODE_REQ ✓, CE readable ✓, en0 ✓. Còn lại: program CE rings (SR/DR base+size, CTRL1, RRI wrapper, post RX buffers) theo ios-redfin radio.c ce_start(), rồi WMI/HTC để scan/join. `wifi status`/`wifi scan` qua console lần này không lấy được output rõ (console chỉ bắt USB/rmtfs noise) — cần test lại sau khi program rings.
- Code test/debug tạm trong driver (cần xoá sau khi WLAN_CFG/CE xong): WLFW_CFG_DUMP, WLFW_CAP_DUMP, bdf_sel, bdf_bid, count-byte fix (giữ lại — đây là fix thật), MODE-first (giữ lại), CE offset 0x240000 (giữ lại).
