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

## 3.17 CE ring programming + bisect (2026-11-06 ~20:41-23:25, k60) — CHƯA XONG

Mục tiêu: program CE rings theo ios-redfin radio.c ce_start() để FW gửi HTC_READY,
rồi HTC handshake + WMI scan. Driver md5 cuối mục: aaf4433a155dce21caf319b7f4c83281
(chưa commit; bản đã push 6e90d23 dừng ở 18f2616c).

### 3.17.1 Code đã viết (giữ lại, đúng theo ios-redfin/ath10k)
- Descriptor đổi 8B -> 16B: {addr_lo u32, addr_hi u32, nbytes u16, flags u16,
  reserved u32} (nbytes offset 8, flags offset 10 — khớp ios-redfin desc_put).
- Thêm defines: CE_SR_BASE_HI 0x04, CE_DR_BASE_HI 0x10, CE_CTRL1 0x18,
  CE_SRC_WM 0x4c, CE_DST_WM 0x50, CTRL1_DMAX 0xffff, SRC_SWAP 1<<17,
  DST_SWAP 1<<18, RRI_UPD 1<<19, ADDR_HI_MASK 0x1f, WRAP_RRI_LO 0xc004,
  WRAP_RRI_HI 0xc008 (offset từ CE0; tuyệt đối 0x18a4c004).
- picasso_wlan_ce_program_rings() mới: (a) RRI: ghi WRAP_RRI_LO/HI = rri_pa
  (pool + 0x3c000) + CTRL1_RRI_UPD cho CE0-3; (b) loop CE0-3: dir OUT/INOUT ghi
  SR_BASE_LO/HI, SR_SIZE=nent, CTRL1 = (old & ~(DMAX|SRC_SWAP)) | nbytes,
  SRC_WM=nent; dir IN/INOUT ghi DR_BASE_LO/HI, DR_SIZE=nent,
  CTRL1 &= ~DST_SWAP, DST_WM=nent; (c) RX post: điền descriptor (buf PA,
  nbytes, flags=HOST_ADDR), flush, ghi CE_DST_WR_INDEX = posted.
- ce_kick_src() nay ghi source descriptor (addr_lo/hi, nbytes, flags=0) +
  FlushPoC trước khi bump SR_WR_INDEX (trước đây chỉ bump index, FW không
  biết buffer ở đâu).
- ce_init() nối: g_ce_ring_nent/nbytes = g_ce_pipe_nent/nbytes,
  g_ce_target_cur_srri = ce_rd(CURRENT_SRRI), gọi program_rings(),
  g_ce_armed_ce = 0xfu, g_ce_target_ready = true, LOG "CE0..CE3 armed".
- Pool: rmtfs PA 0x9b800000 (2MB), CE pool 256KB tại 0x9b900000. Ring offsets:
  CE0 0x00000, CE1 0x01000, CE2 0x02000, CE3 0x03000. Buf offsets: CE0 0x08000,
  CE1 0x0C000, CE2 0x1C000, CE3 0x28000. (Lưu ý: CE0/CE1 bufs overlap nhau
  0x0C000-0x18000, CE2/CE3 bufs overlap 0x28000-0x38000 trong layout — chưa gây
  lỗi thực tế vì chỉ dùng phần đầu mỗi vùng; cần dọn layout khi xong.)
- wlfw_ce_tbl nent/nbytes: CE0 32/2048, CE1 32/2048, CE2 64/2048, CE3 32/2048,
  CE4 256/256, CE5 1024/64, CE6 32/16384, CE7 0/0, CE8-11 32/2048.

### 3.17.2 BUG poll mask — ĐÃ FIX (nguyên nhân HTC_READY không được thấy)
- ce_poll() dùng mask SAI: `hw_drri & n` thay vì `hw_drri & (n-1)`.
  Với n=32: `DRRI & 32` chỉ giữ bit 5 → DRRI=1 cho ra 0, poll không bao giờ
  chạy vòng lặp cho message đầu tiên. Tương tự sw_index advance
  `(slot+1) & n` → slot 0 -> 0, không bao giờ tiến.
- Fix 4 chỗ (CE1 + CE2): `& n1` -> `& (n1-1)`, `& n2` -> `& (n2-1)`.
  (Driver md5 sau fix: 190571ac.)
- Thêm DRRI debug log trong ce_poll: in mỗi khi CE1 DRRI đổi. Xác nhận poll
  CÓ chạy, và DRRI = 0 (FW chưa từng gửi HTC_READY, xem 3.17.3).

### 3.17.3 Kết quả bisect — CE1 sống, CE2 crash, HTC_READY không tới
Mỗi boot: build kernel -> reboot phone -> boot_wifi_gui_test.py + wifi_scan=1.
Dấu hiệu sống: log qua "CE0..CE3 armed" + /dev/ttyACM0 còn. Dấu hiệu crash:
log dừng ở "rings programmed" + ttyACM0 mất + phone về fastboot.
- ring1 (RRI + program + post 32): "rings programmed" ✓, GLINK 0, nhưng log
  dừng sau đó — phone đã reset (lúc đó chưa nhận ra).
- ring2 (fix DR_WR post=32 thay vì mask): vẫn không HTC_READY.
- ring3 (thêm read-back SR/DR_WR_IDX): reset ngay sau "rings programmed".
- ring4 (read-back chỉ BASE/SIZE/DRRI): vẫn reset — read-back KHÔNG phải thủ phạm.
- ring5 (bỏ hẳn read-back): vẫn reset — thủ phạm là FW DMA sau program_rings.
- ring6 (bỏ CẢ RRI lẫn RX post, giữ program base/size/CTRL1/WM): SỐNG, "armed" ✓.
  → program ring an toàn; crash nằm ở RRI và/hoặc RX post.
- ring7 (RX post 32, RRI tắt): RESET → RX post là thủ phạm (ít nhất một phần).
- ring8 (RX post 1 buffer, chỉ CE1): SỐNG ✓, HTC_READY = 0.
- ring9 (RRI bật + 1 buffer CE1): SỐNG ✓, HTC_READY = 0 → RRI an toàn.
- ring10 (RRI + 8 buffer CE1+CE2): RESET → ngưỡng crash giữa 1 và 8, hoặc CE2.
- ring11 (RRI + 8 buffer CHỈ CE1): SỐNG ✓ (898 dòng, GLINK 0, ttyACM0 còn),
  HTC_READY vẫn = 0 sau 2.5 phút → 8 buffer không phải vấn đề; CE2 mới là vấn đề.
- ring12 (1 buffer CHỈ CE2): RESET → CE2 crash ngay cả với 1 buffer.
- ring13 (1 buffer CE2, descriptor nbytes=0/flags=0 như ios-redfin ce_post):
  RESET → nội dung descriptor KHÔNG phải nguyên nhân.
- ring14 (poll mask fix + CE1-only 8 buffer): SỐNG ✓, DRRI log xác nhận poll
  chạy, nhưng HTC_READY vẫn không tới.
- ring15 (như ring14 + DRRI log): DRRI = 0 sau 5 phút. FW KHÔNG gửi HTC_READY
  trên CE1. Console `wifi scan` không tới được shell (console tool chỉ bắt
  USB noise) — không lấy được DRRI qua console, phải dựa vào log tự động.
- ring16 (thêm SMMU bypass stream 0x640, CE1-only): SMMU log
  "allocated SMR[8]=80010640 S2CR=bypass (0x640)" ✓, nhưng DRRI vẫn 0,
  HTC_READY vẫn không tới → 0x640 không chữa được CE1.
- ring17 (SMMU 0x640 + CE1+CE2 8 buffer): RESET → 0x640 không chữa được CE2.
- ring18 (DR_BASE_HI ghi trực tiếp thay vì read-modify-write + CE1+CE2):
  build lần 1 fail "make: Bus error" (k60 load ~728, transient) — build lại
  OK; boot vẫn RESET → BASE_HI RMW không phải nguyên nhân.

### 3.17.4 SMMU — phân tích (apps_smmu 0x15000000)
- Driver ta: stream 0xc0, mask 0x1 (bao 0xc0/0xc1 = WLAN/WLAN_CE), S2CR bypass.
  (Comment code: DT iommus = <&apps_smmu 0xc0 0x1>.)
- ios-redfin: WLAN_SID 0x640, mask 0x1; S2CR type 0 (translate) qua context bank
  với CB_SCTLR = 0xe0 (CFCFG|CFIE|CFRE, M=0: no translation) — tức thực chất
  cũng là bypass/identity, chỉ khác SID. Driver ta nay bypass cả 0xc0 lẫn
  0x640 (SMR[7] 0x800100c0, SMR[8] 0x80010640) — superset của ios-redfin —
  mà CE2 vẫn crash, CE1 vẫn không có HTC_READY → apps_smmu có lẽ KHÔNG phải
  blocker. Nghi vấn: CE DMA đi qua SMMU khác (modem SMMU) hoặc một SID khác,
  hoặc vấn đề không nằm ở DMA path.
- Giả thuyết còn lại cho CE2 crash: FW DMA vào CE2 fault ở tầng khác
  (modem SMMU / secure SMMU / stream chưa biết), hoặc FW kỳ vọng CE2 khác
  (vd: FW dùng ring size nội bộ khác với WLAN_CFG), hoặc FW gửi dữ liệu lên
  CE2 ngay (vd: WMI event/log) và DMA đó fault — trong khi CE1 chưa có dữ
  liệu nên "sống" một cách giả tạo (DRRI=0 vì FW chưa DMA gì lên CE1).
- Giả thuyết cho HTC_READY không tới trên CE1: FW cần tất cả RX CE armed
  (kẹt với CE2 crash), hoặc FW cần một trigger khác (host gửi gì đó trước),
  hoặc DR_WR_IDX/ring program có lỗi tinh vi khiến FW không thấy buffer.

### 3.17.5 Việc tiếp theo
1. Tìm CE DMA path thật: dump toàn bộ SMR (xem SID nào đã được bootloader/
   firmware cấu hình sẵn), kiểm tra modem SMMU (không phải apps_smmu),
   đối chiếu DT lito.dtsi icnss iommus.
2. Thử CE2 với DR_SIZE/DST_WM khác (vd: 32 thay vì 64) hoặc post CE2 nhưng
   DR_WR_IDX=0 (chỉ program ring, không cho FW DMA) để tách biến "FW DMA"
   khỏi "ring program".
3. Khi HTC_READY tới được trên CE1: HTC handshake (CONNECT_SERVICE ->
   SETUP_COMPLETE) rồi WMI scan; CE2/HTT tính sau.
4. Dọn code debug/tạm sau khi xong: WLFW_CFG_DUMP, WLFW_CAP_DUMP, bdf_sel,
   bdf_bid, các comment "DIAGNOSTIC step N", DRRI log, layout buf overlap
   (CE0/CE1, CE2/CE3), rồi commit + push (qua id-it-tritm-2), export-patches,
   check, clean build verify theo AGENT.md.

### 3.18 SMR dump treo modem; CE2 nent=32 vẫn crash; RRI/nbytes=0 xác nhận FW không gửi HTC_READY (ring19-ring23, 2026-11-06 23:27 — 2026-11-07 02:0x)

**ring19 — SMR dump TREO MODEM (5 boot kẹt):** Thêm block dump toàn bộ SMR vào
code SMMU (sau khi đọc num_smr, trước vòng quét 0xc0) để tìm SID thật của CE DMA.
Kết quả: 5 boot liên tiếp (23:32, 23:38, 23:44, 01:08 sau rút cáp USB, 01:20 sau
tắt nguồn hẳn) đều kẹt ở cùng một chỗ — log dừng ~486-500 dòng sau
"modem corners: cx.lvl rc 0, mss.lvl rc 0", KHÔNG có MODEM READY, FW_READY,
SMR dump hay CE nào. Rút cáp USB KHÔNG reset được modem (pin vẫn cấp điện);
tắt nguồn hẳn cũng KHÔNG chữa. Nguyên nhân: code dump SMR treo thread SMMU
setup (chạy TRƯỚC modem PIL), chặn luôn modem bring-up — KHÔNG phải modem hỏng.
**ring20 — Bỏ SMR dump, modem TRỞ LẠI (01:28):** Xoá block dump, driver md5
b382a4dc. Boot: MODEM READY, FW_READY_IND t=4617ms, MISSION MODE t=4660ms,
SMR[7]=0xc0 + SMR[8]=0x640 bypass, "rings programmed", rồi reset ở CE2 như cũ
(885 dòng). Kết luận: SMR dump là thủ phạm; modem không hỏng. Hướng dump SMR
kiểu này BỎ (không thử lại). Lưu ý: bootloader KHÔNG cấu hình sẵn SMR WLAN nào
(driver phải tự cấp SMR[7]/SMR[8]) nên cũng không có SID ẩn để tìm bằng cách này.
**ring21 — CE2 nent 64→32 VẪN CRASH (01:48, driver 0b2813d9):** Đổi CE2 trong
wlfw_ce_tbl từ {2,PD_IN,64,2048,7} thành {2,PD_IN,32,2048,7} (WLAN_CFG + ring
đều dùng 32). Boot: FW_READY t=4567ms, MISSION MODE t=4610ms, dump picasso_ce
xong thì log dừng (887 dòng, không có "rings programmed"), phone về fastboot.
→ Kích thước ring (DR_SIZE/DST_WM) KHÔNG phải nguyên nhân CE2 crash. Đã loại:
số buffer, nội dung descriptor, SMMU 0xc0/0x640, DR_BASE_HI RMW, DR_SIZE 64/32.
**ring22 — RRI poll: DRRI/RRI=25169 là rác, FW không gửi HTC hợp lệ (01:57,
driver b27a60af):** CE1-only post + đọc RRI từ host memory (pool+0x3c000, dst
bits[31:16], dùng max(DRRI reg, RRI)) theo ios-redfin. Boot SỐNG (940 dòng,
ttyACM0 còn): DRRI/RRI nhảy 0→25169 (cả hai khớp nhau), poll xử lý slots 0-31
nhưng TẤT CẢ đều rác (nbytes 63703/26257/27521..., data ngẫu nhiên), không có
HTC_MSG_READY. 25169=0x6251 không phải ring index hợp lệ (0-31). Kết luận: FW
không viết HTC message thật vào ring CE1; giá trị DRRI/RRI là rác/không đáng tin.
**ring23 — ios-redfin-style post nbytes=0 + poll theo nbytes: SẠCH nhưng FW vẫn
không gửi (02:04, driver 406752c3):** Post descriptor nbytes=0/flags=0 (như
ios-redfin ce_post); poll break khi nbytes==0 (chưa có msg) hoặc >2048 (rác);
replenish nbytes=0/flags=0. Boot SỐNG (906 dòng, ttyACM0 còn): DRRI=RRI=0,
KHÔNG có dòng "CE1 slot" nào (poll break đúng, không xử lý rác). Nhưng
descriptor nbytes vẫn 0 mãi — FW thực sự KHÔNG gửi HTC_READY lên CE1 dù CE1
đã program + 8 buffer + SMMU bypass + MODE_REQ ack. Kết luận: FW cần thứ khác
trước khi gửi HTC_READY; ứng viên mạnh nhất là nó cần TẤT CẢ 12 CE được program
(ios-redfin program cả 12, driver ta chỉ program CE0-3), hoặc cần CE2 (WMI)
được arm — mà CE2 thì crash khi post (catch-22).
**Việc tiếp theo:** Program cả 12 CE (base/size/CTRL1/WM) nhưng chỉ post RX
buffer trên CE1 (tránh CE2 crash); xem FW có gửi HTC_READY không. Sau đó xử lý
CE2 crash (có thể do buffer pool overlap CE2/CE3 hoặc CE2 cần cấu hình khác).

### 3.19 Program cả 12 CE không chữa; gửi CONNECT_SERVICE trên CE0 cũng crash — CE DMA/SMMU hỏng ở tầng cơ bản (ring24-ring25, 2026-11-07 02:09 — 02:17)

**ring24 — Program cả 12 CE, FW vẫn không gửi HTC_READY (02:09, driver
21d45d3f):** Mở rộng program_rings + ce_init sang cả 12 CE (ring offsets mới:
CE4-7 ở 0x04000-0x07000, CE8-11 ở 0x38000-0x3b000; armed mask 0xfff; chỉ post
RX buffer trên CE1). Boot SỐNG (918 dòng, ttyACM0 còn): FW_READY t=4596ms,
MISSION MODE t=4639ms, rings programmed, armed. Nhưng DRRI/RRI lại nhảy lên
giá trị rác khác (37229=0x916D, lần trước là 25169), descriptor nbytes vẫn 0,
không có HTC_READY. → Program cả 12 CE KHÔNG phải thứ FW thiếu.
**ring25 — Poke FW bằng CONNECT_SERVICE trên CE0: PHONE CRASH (02:17, driver
fd739b93):** Thêm poke trong ce_poll: nếu chưa handshake thì gửi một lần
CONNECT_SERVICE (WMI 0x0100) lên CE0 qua ce_kick_src. Log cho thấy poke ĐÃ gửi
("Sent CONNECT_SERVICE to CE0 [slot 0]"), rồi phone reset về fastboot (897
dòng, ttyACM0 mất). Đây là crash MỚI: TX trên CE0 cũng crash, không chỉ RX
trên CE2. → Đường CE DMA hỏng cả hai chiều (host→FW và FW→host), không phải
chỉ riêng RX. Nghi ngờ mạnh: CE DMA đi qua SMMU stream KHÁC với 0xc0/0x640 đã
bypass (có thể là modem SMMU thay vì apps_smmu, hoặc một SID khác), nên FW
đọc/ghi ring/buffer bị fault. Cần tìm SID thật của CE DMA bằng cách khác (dump
SMR kiểu cũ treo SMMU setup nên không dùng được; xem DT lito.dtsi node icnss
và modem SMMU).
**Đã loại thêm:** program 12 CE (không chữa), poke CONNECT_SERVICE (gây crash
mới, chứng tỏ TX cũng hỏng). **Chưa loại:** CE DMA SID thật (modem SMMU / SID
khác 0xc0, 0x640), CE base PA sai cho DMA (dù MMIO đọc được), ring/buffer PA
mà FW thấy khác với host (mapping VA-PA của pool sai).
**Việc tiếp theo:** Tìm CE DMA SID thật — đọc DT lito.dtsi (icnss iommus,
smmu nodes), kiểm tra modem SMMU base, đối chiếu ath10k snoc.c xem CE dùng
stream nào. Không gửi thêm CE0/CE2 cho đến khi rõ SID.

### 3.20 S2CR bypass quirk XÁC NHẬN: hardware đổi BYPASS thành FAULT — CE DMA fault mọi giao dịch (ring26, 2026-11-07 02:25)

**ring26 — Readback S2CR là bằng chứng quyết định (driver 1a447d20):** Thêm log
đọc lại S2CR sau khi ghi bypass. Kết quả:
`wlan smmu: allocated SMR[7]=800100c0 S2CR=bypass (readback 00020000 type=2)`
Ghi 0x00010000 (type 1 = BYPASS) nhưng đọc lại được 0x00020000 (type 2 =
FAULT). Hardware âm thầm đổi BYPASS thành FAULT — đúng quirk của QCOM SMMU
ghi trong arm-smmu-qcom.c ("writing BYPASS will end up written as FAULT...
reserve a context bank to emulate bypass streams"). Đây là NGUYÊN NHÂN GỐC
của mọi vụ CE DMA: mọi giao dịch DMA của FW qua CE đều FAULT (không bypass),
nên CE2 post crash, CE0 TX crash (ring25 poke), CE1 RX không bao giờ nhận
HTC_READY. SMMU bypass bằng S2CR type 1 KHÔNG hoạt động trên SMMU này.
**Cách chữa (đã có sẵn trong ios-redfin smmu_wlan_identity):** Dùng context
bank để "giả bypass": cấu hình một CB trống với SCTLR=0xe0 (M=0, không dịch),
CBAR=0x0001f000 (S1_TRANS_S2_BYPASS), CBA2R=0x1, rồi đặt S2CR type 0
(translate) trỏ tới CB đó. Đây chính là cách Linux arm-smmu-qcom xử lý quirk
này, và ios-redfin dùng nó cho WLAN_SID 0x640 trên cùng SoC SM7250.
**Việc tiếp theo:** Thay S2CR bypass bằng CB-based bypass cho cả 0xc0 và
0x640; bỏ poke CONNECT_SERVICE (ring25) vì nó crash cho đến khi SMMU đúng;
sau đó test lại CE2 post và HTC_READY.

### 3.21 S2CR bypass quirk + CB-bypass bị kẹt ở truy cập thanh ghi CB (ring26-ring30, 2026-11-07 02:25 — 03:06)

**ring26 — S2CR bypass quirk XÁC NHẬN (driver 1a447d20):** Đọc lại S2CR sau
khi ghi bypass: ghi 0x00010000 (type 1 = BYPASS) nhưng đọc lại 0x00020000
(type 2 = FAULT). Hardware âm thầm đổi BYPASS thành FAULT — đúng quirk QCOM
SMMU trong arm-smmu-qcom.c. Đây là nguyên nhân gốc CE DMA fault.
**ring27-29 — CB-bypass bị kẹt ở map/truy cập CB (driver aeeb405f ->
69747b64 -> 294fecdf):** Thử dùng context bank để "giả bypass" như
ios-redfin (SCTLR=0xe0 M=0, CBAR=0x0001f000, S2CR type 0 trỏ CB). Gặp 2 vấn
đề: (1) ml_io_map kích thước >0x2000 treo modem_thread (0x4000/0x10000/0x80000
đều treo, chỉ 0x2000 chạy); (2) CB nằm ở npage*psize = 0x80000 (IDR1:
psize=0x1000, npage=128, ncb=62, nsmr=81), xa ngoài map GR0. Map riêng vùng CB
0x15080000 (0x1000) THÀNH CÔNG (VA 0xfffffff761a6c000) nhưng đọc SCTLR của CB0
qua map đó TREO modem_thread (không log tiếp). Nghi CB registers bị clock-gate
hoặc CB0 đặc biệt. Đã revert về S2CR bypass tạm để lấy IDR1 (ring28).
**ring30 — S2CR=CB0 không chữa (driver 7079e1e4):** Thử trỏ S2CR type 0 về
CB0 (hy vọng ABL đã cấu hình CB0 translation-off). Modem lên bình thường,
CE program + armed, phone sống, nhưng FW vẫn không gửi HTC_READY. CB0 không
phải translation-off, hoặc SMMU vẫn không bypass.
**Trạng thái:** SMMU bypass quirk đã rõ, nhưng cách chữa bằng CB bị kẹt ở
truy cập thanh ghi CB (treo bus). CE DMA vẫn fault. Cần tìm cách khác để
truy cập CB (vd: bật clock SMMU, dùng CB khác, hoặc xem ABL cấu hình CB nào
sẵn) hoặc xác nhận CE DMA có thực sự qua apps SMMU không (có thể qua modem
SMMU). Đừng thử lại S2CR bypass (đã chứng minh thành FAULT) hay map lớn
>0x2000 (treo modem_thread).

### 3.22 CB-bypass bị kẹt ở đọc SCTLR; thử S2CR=CB0/CB61 đều không chữa (ring31-ring33, 2026-11-07 03:10 — 03:19)

**ring31 — CB1 cũng treo (driver 9796afe7):** Thử CB1 (map 0x15081000) thay
CB0. Map thành công (VA 0xfffffff803e65000) nhưng đọc SCTLR vẫn treo sau đó.
Kết luận: mọi CB SCTLR read đều treo, không riêng CB0 — thanh ghi CB không
truy cập được từ AP (có thể clock-gate hoặc secure-only).
**ring32 — CB0 ở 0x2000 cũng treo (driver d7f7c81a):** Thử giả thuyết CB
nằm ở 0x2000 (GR0+GR1=2 pages) thay vì npage*psize=0x80000. Map 0x15002000
thành công (VA 0xfffffff13a3aa000) nhưng đọc SCTLR vẫn treo. Loại giả thuyết
CB ở 0x2000.
**ring33 — S2CR=CB61 không chữa (driver 2cde18fd):** Thử trỏ S2CR type 0 về
CB61 (CB cuối, ncb=62, hy vọng SCTLR=0 chưa cấu hình => M=0 bypass). Modem
lên bình thường, CE armed, phone sống, nhưng FW vẫn không gửi HTC_READY.
CB61 không phải translation-off.
**Đã thử S2CR:** bypass type 1 (thành FAULT, ring26), CB0 (ring30), CB61
(ring33) — tất cả đều không cho CE DMA hoạt động. **Đã loại:** CB0/CB1/CB61
làm S2CR target; CB ở 0x2000 và 0x80000 đều không đọc được SCTLR.
**Việc tiếp theo:** Cần cách khác để SMMU bypass — vd: tìm/bật clock cho CB,
xem ABL cấu hình sẵn CB nào (đọc CBAR qua GR1 map 0x2000 — CBAR ở 0x1000+4c,
trong tầm map GR0, không cần map CB), hoặc kiểm tra xem CE DMA có qua modem
SMMU thay vì apps SMMU không. Cũng cần xem lại giả thuyết: DRRI rác
25169/37229 có thể không phải garbage mà là định dạng khác (vd: byte count),
hoặc FW đang viết vào sai địa chỉ do SMMU fault.

### 3.23 CBAR scan: CB0-4 đã là bypass-type; CB5 write-only config THÀNH CÔNG nhưng HTC vẫn không tới (ring34-ring36, 2026-11-07 03:22 — 03:30)

**ring34 — CBAR scan (driver 297ec1b6):** Đọc CBAR qua GR1 map (không treo).
Kết quả: CBAR[0..4]=0x0001f000 (S1_TRANS_S2_BYPASS, do ABL cấu hình sẵn),
CBAR[5..]=0x00000000 (chưa cấu hình). Phát hiện quan trọng: ABL đã cấu hình
sẵn CB0-4 là bypass-type. Nhưng S2CR=CB0 (ring30) không chữa — có thể CB0-4
đang dùng cho thiết bị khác (USB...) với SCTLR M=1 (dịch qua page table của
thiết bị đó), hoặc SCTLR của chúng không phải M=0.
**ring35 — CB5 write-only config không treo (driver 807bc854):** Cấu hình CB5
bằng cách CHỈ GHI (không đọc SCTLR — đọc treo nhưng ghi có thể qua). Ghi
FSR/TTBR0/TCR/TCR2/MAIR=0, CBA2R=1, CBAR=0x1f000, SCTLR=0xe0 (M=0), S2CR
type 0 -> CB5, SMR valid. Kết quả: helper chạy xong cho cả 0xc0 và 0x640,
KHÔNG treo, modem lên, CE armed, phone sống. Nhưng FW vẫn không gửi
HTC_READY; DRRI lại rác (60032 = 0xEA80, khác 25169/37229 trước).
**ring36 — Xác minh CBAR[5] đã đổi (driver 76a324ce):** Đọc lại CBAR[5] sau
config qua GR0 map: **CBAR[5]=0x0001f000** — ghi CB đã có hiệu lực. S2CR[7]=5
và S2CR[8]=5 (type 0 -> CB5). SMR[7]=0x800100c0 đúng. (SMR[8] đọc lại =0,
nghi ngờ chỉ số f640 — cần kiểm tra, nhưng S2CR[8]=5 cho thấy đã ghi.)
Modem lên, CE armed, phone sống, nhưng **vẫn không có HTC_READY**.
**Kết luận:** Ghi CB đã chứng minh hiệu quả (CBAR đổi), nhưng CE DMA vẫn
không hoạt động. Khả năng: (a) ghi SCTLR (M=0) không có hiệu lực — SCTLR
nằm trong vùng CB (0x15085000) khác với CBAR (GR1, 0x15001000), có thể
clock-gate riêng; không đọc được SCTLR để xác minh. (b) CE DMA không qua
apps SMMU mà qua modem SMMU. (c) DRRI rác cho thấy FW đang ghi garbage,
không phải message hợp lệ — có thể offset thanh ghi CUR_DRRI (0x48) sai,
hoặc FW DMA fault làm hỏng dữ liệu. Bước tiếp: xác minh SCTLR bằng cách
khác (vd: thử S2CR=CB5 với SCTLR mặc định, hoặc đọc SCTLR qua một đường
khác), hoặc kiểm tra lại offset CE register từ ios-redfin.

### 3.24 ĐỘT PHÁ: CB5 sửa được CE2 crash; CE2 nhận message đầu tiên (ring37-ring38, 2026-11-07 03:35 — 03:40)

**ring37 — S2CR=CB1 không chữa (driver b3552c25):** Trỏ S2CR về CB1 (ABL
bypass-type, CBAR=0x1f000). Modem lên, CE armed, phone sống, nhưng không có
HTC_READY. CB1 (như CB0) có thể đang dùng cho thiết bị khác với SCTLR M=1.
**ring38 — CB5 + CE2 post: PHONE SỐNG, CE2 NHẬN MESSAGE (driver 5025dc4c):**
Khôi phục CB5 write-only config (CBAR[5]=0x1f000 verified, S2CR[7]=5,
S2CR[8]=5) và bật lại post CE2 (bỏ skip CE2). Kết quả: **phone SỐNG** (không
crash như mọi lần CE2 post trước đây) — **CB5 SMMU bypass ĐÃ HOẠT ĐỘNG**,
CE DMA không còn fault. Và quan trọng hơn: **CE2 nhận message đầu tiên**:
`[CE2 slot 0] DRRI=1 nbytes=20 flags=0xffff data: 00 0c 00 00 00 00 00 00`.
DRRI=1 là index hợp lệ (không còn rác 25169/37229/60032/12255), nbytes=20 là
độ dài message thật. Đây là message CE đầu tiên không phải garbage.
**Vấn đề còn lại:** Message tới trên CE2 (WMI RX), không phải CE1 (HTC RX).
CE1 DRRI vẫn rác (12255). Data `00 0c 00 00...` chưa rõ là HTC_READY lạc
đường hay WMI event. Cần đọc full 20 bytes và xem ce_poll xử lý CE2 thế nào
(có gọi htc_rx không), và tại sao CE1 vẫn không có HTC_READY dù CE DMA đã
chạy. Có thể FW gửi message đầu tiên trên CE2, hoặc CE1 cần cấu hình khác.

### 3.25 ring39: CE0 CONNECT_SERVICE vẫn crash dù CB5 đã chạy (2026-11-07 03:45 — 03:48)

**ring39 (driver e973d058):** Bật lại poke CE0 CONNECT_SERVICE (ring25)
sau khi CB5 đã chứng minh sửa được CE2 crash. Kết quả: poke ĐÃ gửi
(`Sent CONNECT_SERVICE (WMI 0x0100) to CE0 [slot 0]`), CE2 lại nhận message
`[CE2 slot 0] DRRI=1 nbytes=20 data: 00 0c 00 00...` như ring38, rồi **phone
CRASH** (ttyACM0 mất, về fastboot xnuloader).
**Ý nghĩa:** CE0 TX vẫn crash dù CB5 đã chạy. CE2 RX (FW->host) sống được
với CB5, nhưng CE0 TX (host->FW, FW đọc descriptor/buffer) vẫn fault.
Khả năng: (a) CE0 dùng SMMU stream khác 0xc0/0x640 chưa được CB5 cover;
(b) địa chỉ source ring/buffer CE0 sai (FW đọc sai chỗ và fault); (c) format
descriptor/message CONNECT_SERVICE sai khiến FW assert (không phải SMMU
fault); (d) CB5 bypass chỉ chạy cho DMA write (FW->host) mà không cho DMA
read (FW đọc host memory). Cần phân biệt (a)/(b) với (c)/(d): kiểm tra xem
FW có đọc được descriptor CE0 không (vd: SRRI có tăng không trước khi
crash), hoặc thử gửi message rỗng/đơn giản hơn trên CE0 để loại (c).

### 3.26 ring40-41: SHADOW không chữa, nhưng sửa format HTC thì CE0 hết crash và FW trả lời (2026-11-07 03:53 — 04:00)

**ring40 — SHADOW không chữa (driver e82e46da):** Đổi ce_kick_src sang ghi
SHADOW register (WLAN_PA+0x32000+4*ce) thay vì SR_WR_IDX trực tiếp (theo
ios-redfin ce_send). Kết quả: phone VẪN CRASH sau khi gửi CONNECT_SERVICE.
Loại giả thuyết SHADOW-vs-SR_WR_IDX.
**ring41 — Sửa format HTC: CE0 HẾT CRASH, FW TRẢ LỜI (driver a75232f0):**
So sánh với ios-redfin htc_send/htc_connect: header HTC thật là 8 bytes
(eid, flags, len u16, trailer, seq, pad u16), payload CONNECT_SERVICE 8
bytes (msg_id u16=2, svc u16=0x0100, flags u16=0x0100), tổng 16 bytes. Driver
ta cũ gửi 12 bytes với header 4 bytes (sai) và flags=0 (sai). Sửa thành
đúng 16 bytes. Kết quả: **phone SỐNG sau khi gửi CONNECT_SERVICE** (không
crash), và **CE2 nhận THÊM message thứ hai**: `[CE2 slot 0] DRRI=2 nbytes=20
data: 00 0c 00 00 00 00 00 00` và `[CE2 slot 1] DRRI=2 nbytes=20 data: 00 0c
00 00 00 00 01 00`. DRRI tăng 1->2, có 2 slot, message thứ hai khác byte 6
(0x01). FW đang trả lời CONNECT_SERVICE. Chưa thấy HTC_MSG_READY hay
CONNECT_RESP trong log — cần đọc full 20 bytes của message CE2 và xem nó có
phải HTC CONNECT_RESP lạc sang CE2 không, hoặc FW gửi gì khác.

### 3.27 ring42-43: HTC_READY tới trên CE2 (không phải CE1), gửi lặp lại; cần handshake HTT trước WMI (2026-11-07 04:04 — 04:09)

**ring42 — Full dump 20 bytes (driver 0b278455):** CE2 slot 0:
`00 0c 00 00 00 00 00 00 00 02 00 01 00 16 08 88 00 00 00 01`, slot 1:
`00 0c 00 00 00 00 01 00 01 00 00 03 07 f8 01 00 00 00 00 00`. Hai message
tương tự, khác byte 6 (0x00/0x01) và bytes 8-15. Chưa parse được ngay.
**ring43 — HTC_READY trên CE2, gửi LẶP LẠI (driver 75d7c0d5):** Thêm parse
HTC vào handler CE2 (theo ios-redfin: mọi message CE đều qua htc_rx). Kết
quả: **`[CE2 HTC parse] eid=0 hlen=0 msg=1` — msg=1 là HTC_MSG_READY!**
FW gửi HTC_READY trên CE2 (không phải CE1 như ath10k). Nhưng FW gửi LẶP
LẠI: slot 2,3,4,5,6,7... DRRI tăng 3,4,5,6,7,8..., mỗi message có sequence
tăng dần (byte 6: 02,03,04...; byte 14: 02,03,04...). Mỗi lần READY, handler
ta gửi CONNECT_SERVICE (WMI) — nhưng FW vẫn tiếp tục gửi READY, không trả
CONNECT_RESP. **Phone CRASH sau nhiều lần READY** (ttyACM0 mất).
**Chẩn đoán:** FW không chấp nhận CONNECT_SERVICE (WMI) của ta, nên cứ gửi
lại READY. Nguyên nhân có thể: (a) thứ tự handshake sai — ios-redfin làm
READY -> connect HTT (SVC_HTT_DATA) -> CONNECT_RESP (HTT) -> connect WMI
(SVC_WMI_CONTROL) -> CONNECT_RESP (WMI) -> SETUP_COMPLETE; driver ta chỉ
connect WMI, bỏ qua HTT, FW có thể đợi HTT trước nên bỏ qua WMI connect.
(b) CONNECT_SERVICE vẫn sai format/dích đến sai CE. (c) FW gửi READY
định kỳ cho tới khi handshake xong, và crash do quá nhiều CONNECT_SERVICE
gửi lặp. Bước tiếp: sửa handshake theo đúng thứ tự ios-redfin (HTT trước,
WMI sau), chỉ gửi CONNECT_SERVICE một lần cho mỗi service (không gửi lặp
mỗi lần thấy READY), và xử lý CONNECT_RESP để lấy eid.

### 3.28 ring44-45: HTT-first bị FW từ chối (status 7); cần thử WMI trước (2026-11-07 04:18 — 04:22)

**ring44 — Chỉ có slot 0, không READY (driver 89fda447):** Bỏ poke ring39,
chỉ gửi CONNECT khi thấy READY. Kết quả: chỉ 1 message CE2 (slot 0, hmsg=512,
không phải READY), KHÔNG có READY, KHÔNG có CONNECT nào được gửi. Bài học:
FW gửi slot 0 rồi ĐỢI host gửi CONNECT trước, nó mới gửi READY. Không thể
ngồi chờ READY mà không gửi gì.
**ring45 — Poke HTT một lần: FW TRẢ LỜI CONNECT_RESP (driver 77da5e9d):**
Gửi CONNECT(HTT 0x0300) một lần ở poll đầu. FW trả lời:
`*** HTC CONNECT_RESP on CE2: svc=0x300 status=7 eid=208 ***` và CE2 slot 1:
`00 0c 00 00 00 00 01 00 03 00 00 03 07 d0 01 00 00 00 00 00` (msg=3,
svc=0x0300, status=7, eid=0xd0=208, max=1). **FW ĐÃ nhận và parse được
CONNECT_SERVICE của ta (svc đúng 0x0300), nhưng TỪ CHỐI với status=7.**
eid=208 không hợp lệ (endpoint phải nhỏ), gợi ý parse có thể lệch 1 byte
hoặc FW trả lỗi. Phone SỐNG. Giả thuyết: FW muốn WMI connect trước (không
phải HTT), hoặc flags HTT (0x0008) sai. Bước tiếp: đổi poke thành CONNECT
(WMI 0x0100, flags 0x0100) như ring41 (khi đó FW đã gửi READY), xem FW có
trả CONNECT_RESP status=0 cho WMI không.

### 3.29 ring46-48: FW trả CONNECT_RESP (HTT, status 7) dù ta gửi WMI; SETUP_COMPLETE sai format làm crash (2026-11-07 04:26 — 04:34)

**ring46 — WMI poke: FW vẫn trả HTT RESP (driver d6a4ba6a):** Gửi CONNECT
(WMI 0x0100). FW trả: READY (slot 1, msg=1) rồi CONNECT_RESP (slot 2,
svc=0x300 HTT, status=7, eid=208). **FW LUÔN trả svc=0x300 dù ta gửi WMI.**
Gợi ý: FW có chuỗi trả lời cố định, hoặc parse status/eid của ta lệch.
**ring47 — Nới lỏng status check: handshake VẪN kẹt (driver 9152083c):**
Bỏ yêu cầu status==0, nhưng handler không chạy vì poke đã set g_htc_wmi_sent
=true nên nhánh HTT RESP bị guard chặn. Chỉ có 3 message CE2, không tiến triển.
**ring48 — SETUP_COMPLETE trực tiếp sau HTT RESP: driver báo HANDSHAKE
COMPLETE nhưng phone CRASH (driver 462be9a9):** Gửi SETUP_COMPLETE_EX sau HTT
RESP (status=7), log "HTC HANDSHAKE COMPLETE (via CE2, SETUP after HTT RESP)!".
Nhưng phone CRASH ngay sau đó (ttyACM0 mất). **Nguyên nhân nghi ngờ nhất:
picasso_htc_send_setup_complete vẫn dùng header 4 bytes cũ (chưa sửa như
CONNECT_SERVICE ở ring41) — gửi 16 bytes thay vì 20 bytes (8-byte header +
12-byte payload) nên FW parse sai và crash.** Bước tiếp: sửa setup_complete
sang đúng format ios-redfin (8-byte header, tổng 20 bytes) rồi boot lại.

### 3.30 ring49: ĐỘT PHÁ — HTC HANDSHAKE HOÀN TẤT, phone SỐNG, FW gửi WMI/HTT traffic (2026-11-07 04:37 — 04:41)

**Sửa SETUP_COMPLETE sang 8-byte header (driver 6e0432ac):** payload 12 bytes
(msg_id=5 + 10 zero), tổng 20 bytes (thay vì 16 bytes header 4 bytes cũ).
**Kết quả boot ring49:**
- `HTT resp (status=7) -> SETUP_COMPLETE directly`
- `Sent SETUP_COMPLETE_EX to CE0 [slot 2]`
- `*** HTC HANDSHAKE COMPLETE (via CE2, SETUP after HTT RESP)! ***`
- **PHONE SỐNG (ttyACM0 còn).** SETUP_COMPLETE đúng format không crash nữa.
- **CE2 nhận 7 messages** (trước đó chỉ 3): slot 3 (nbytes=36), slot 4
  (nbytes=340, chứa `32 1b 00 2a` = fw_version), slot 5 (nbytes=988), slot 6
  (nbytes=28). Đây là **WMI/HTT traffic thật từ FW sau khi handshake xong**
  (không còn là message HTC control 20 bytes nữa). Byte sequence tăng dần
  (03, 04, 05, 06).
- `wifi scan` qua console đã gửi nhưng **chưa thấy kết quả scan trong log**
  (không có sta_scan/SCAN_RESULT/beacon/BSS). Console tool không bắt được
  output lệnh wifi (đã biết từ trước — chỉ đáng tin cho reboot-fastboot).
**Trạng thái:** HTC handshake hoàn tất (phía driver), CE DMA hai chiều chạy,
FW gửi traffic. **Việc còn lại: kích hoạt và xác minh wifi scan** — cần cách
trigger/đọc kết quả scan đáng tin hơn (vd: auto-scan khi boot với wifi_scan=1,
hoặc cải thiện console capture), rồi WMI join.

### 3.31 ring50: Auto-scan chạy, WMI scan dispatch tới CE3, nhưng phone CRASH sau đó (2026-11-07 04:52 — 04:56)

**Sửa WMI scan (driver c5eaaade):** (1) HTC header 4 -> 8 bytes (WMI TLV sang
buf+8, total 8+wmi_len=140 bytes); (2) g_wmi_eid mặc định 1 -> 3 (WMI TX là
CE3 theo ath10k; 1 là HTC RX nên send_wmi_scan từ chối); (3) auto-scan một lần
sau handshake trong CE2 handler (console `wifi scan` không bắt được output).
Build gặp 3 lỗi forward-declaration (globals là `static bool`, picasso_sta_scan
trả `int`, và insert làm vỡ khai báo `static void`/`static unsigned int` tách
dòng) — sửa bằng cách ghi script ra file rồi chạy (pipe python qua stdin của
SSH tương tác không đáng tin).
**Kết quả boot ring50:**
- `DIAGNOSTIC ring50 — auto-scan after handshake`
- `picasso_sta_scan: WCN3990 scanning over-the-air 2.4GHz & 5GHz channels...`
- `CE1 DRRI=0 sw=0 | CE2 DRRI=3 sw=2 (htc_ready=1)`
- `Dispatched WMI_START_SCAN_CMDID (140 bytes) to CE3 [slot 0]` — scan command
  đã gửi, WMI pipe CE3 chấp nhận (không còn refuse).
- **Phone CRASH ngay sau đó** (ttyACM0 mất, `=== PICASSO CDC PANIC BEGIN ===`,
  không có dòng "scan complete", không có CE2 slot 7, không có SCAN_RESULT).
**Chẩn đoán:** Crash xảy ra sau khi dispatch WMI scan lên CE3, trước khi có
kết quả scan. Nghi ngờ: (a) nội dung WMI START_SCAN TLV sai (tag/len/field)
khiến FW crash khi parse; (b) CE3 ring/buffer cấu hình sai (khác CE0 vốn chạy
được); (c) WMI cần HTC header khác (eid/flags). Bước tiếp: đối chiếu WMI scan
command với ios-redfin/ath10k (tag 0x0017, len, các field scan_id/req_id/vdev/
dwell/channels) và kiểm tra CE3 ring/buffer layout.

### 3.32 ring51: WMI scan đúng format ios-redfin nhưng phone VẪN CRASH — thiếu WMI_INIT (2026-11-07 05:03 — 05:07)

**Sửa WMI scan (driver 73fd746e):** Viết lại picasso_wlan_send_wmi_scan theo
đúng ios-redfin scan_send_start: thêm 4-byte WMI cmd ID (0x3001) trước TLV,
tag 0x4d (thay 0x0017), len 100, field values ios-redfin (events 0x11f,
num_bssids=1, probe delay 5, scan_ctrl_flags 0x31), mac/mask 16 bytes,
channels tag 0x10 (thay 0x13), trailing TLVs (0x13/0, 0x13/8 bssid broadcast,
0x11/0). Build lần đầu bị kẹt (SSH background treo, kernel vẫn 04:52) — kill
session rồi build lại foreground thì OK (kernel 05:03).
**Kết quả boot ring51:**
- Auto-scan trigger, scan start, htc_ready=1.
- `Dispatched WMI_START_SCAN_CMDID (184 bytes) to CE3 [slot 0]` (184 = 8 header
  + 4 cmd ID + 172 TLV).
- **Phone CRASH ngay sau đó** (PANIC, về fastboot, không có "scan complete",
  không có CE2 slot 7, không có SCAN_RESULT). Y hệt ring50.
**Chẩn đoán mới:** ios-redfin gửi **WMI_INIT (CMD 0x1)** TRƯỚC khi scan
(wmi_send_init: TAG_INIT_CMD 0x4a + TAG_RESOURCE_CONFIG 0x4b 44 u32 +
TAG_ARRAY_STRUCT host mem chunks). Driver ta KHÔNG BAO GIỜ gửi WMI_INIT —
FW có thể crash khi nhận scan command mà WMI layer chưa được init. Bước tiếp:
gửi WMI_INIT tối thiểu (n=0 host mem chunks, resource config theo ath10k
wcn3990) ngay sau HTC handshake, trước khi auto-scan.

### 3.33 ring52-53: WMI_INIT gửi 1 lần nhưng phone VẪN CRASH sau scan — thiếu VDEV_CREATE (2026-11-07 05:15 — 05:24)

**ring52 — WMI_INIT (driver bf0ac0a7):** Thêm picasso_wlan_send_wmi_init()
theo ios-redfin wmi_send_init (n=0 host mem chunks): CMD 0x1, TAG_INIT_CMD
0x4a (ABI 0x01000000/53/"QCA_"/"ML"), TAG_RESOURCE_CONFIG 0x4b (44 u32,
NUM_VDEVS=4/NUM_PEERS=33), TAG_ARRAY_STRUCT 0x12 len 0. Gửi trên CE3 ngay sau
HTC handshake. Build gặp lỗi chèn hàm giữa `static void` và tên hàm (đã sửa).
**Kết quả boot ring52:** WMI_INIT (228 bytes) dispatch tới CE3 [slot 0],
scan dispatch [slot 1], nhưng **WMI_INIT bị gửi LẦN 2** [slot 2] — CE2
re-process slot 2 (cùng CONNECT_RESP 2 lần) nên handler HTT-RESP chạy lại,
gửi SETUP_COMPLETE + WMI_INIT lần nữa. Phone CRASH.
**ring53 — Guard chống gửi trùng (driver b7dbae1d):** Thêm static bool
setup_sent trong nhánh HTT-RESP. **Kết quả boot ring53:** WMI_INIT chỉ gửi
1 lần, scan dispatch [slot 1], CE2 8 messages — **phone VẪN CRASH** (ttyACM0
mất, về fastboot, không có "scan complete").
**Chẩn đoán mới:** ios-redfin gửi **VDEV_CREATE (CMD 0x5001, TAG 0x56)** và
**SCAN_CHAN_LIST (CMD 0x3003)** TRƯỚC khi scan. Driver ta không tạo VDEV —
FW có thể crash khi scan trên vdev=0 chưa tồn tại. Bước tiếp: gửi VDEV_CREATE
(vdev 0, station=2, MAC) trên CE3 sau WMI_INIT, trước scan.

### 3.34 ring54: VDEV_CREATE sạch nhưng phone VẪN CRASH sau scan (2026-11-07 05:32 — 05:44)

**ring54a — VDEV_CREATE bị lỗi truyền (driver 84f85c67):** Thêm
picasso_wlan_send_vdev_create() (CMD 0x5001, TAG 0x56, vdev 0, station=2,
MAC 02:00:00:00:00:01). Nhưng file truyền qua base64 bị lỗi nhiều chỗ
('wmi'->'wim', 'VDEV_CREATE'->'VDEVCREATE', '[slot %u]'->'[slot %1]',
'WMI pipe'->'WM oe % wipe') — log in ra "VDEVCREATE (36 bytes) to CE3
[slot ]". Phone crash sau scan như cũ. Bài học: truyền file lớn qua
stdin của SSH tương tác hay base64 dài đều không đáng tin; dùng scp khi
.bashrc hợp lệ, hoặc chia nhỏ heredoc (~700 bytes/chunk).
**ring54b — VDEV_CREATE sạch (driver hiện tại):** Xoá hàm lỗi, viết lại bằng
3 chunk heredoc nhỏ (đã verify: 0 lỗi 'wim'/'VDEVCREATE', 'VDEV_CREATE'=4,
'wmi_len'=15). **Kết quả boot ring54b:** WMI_INIT (228B) [slot 0],
VDEV_CREATE (36B) [slot 1] — log sạch, SCAN (184B) [slot 2], CE2 slots 3-6
(WMI/HTT traffic) — **phone VẪN CRASH** (PANIC, về fastboot, không có "scan
complete", không có beacon/SSID).
**Trạng thái:** Chuỗi WMI_INIT -> VDEV_CREATE -> SCAN đã dispatch sạch, đúng
thứ tự, đúng format ios-redfin, nhưng phone vẫn crash sau khi scan dispatch.
Giả thuyết còn lại: (a) thiếu SCAN_CHAN_LIST (CMD 0x3003, ios-redfin gửi
trước VDEV_CREATE); (b) cần chờ WMI_READY event (0x2) trên CE2 sau WMI_INIT
trước khi gửi VDEV/SCAN — hiện gửi ngay không chờ; (c) CE2 poll trong dwell
loop crash khi xử lý message scan-related; (d) nội dung scan command còn sai
chi tiết (vd MAC, channel flags). Bước tiếp: thử chờ WMI_READY rồi mới
scan, và/hoặc thêm SCAN_CHAN_LIST.

### 3.35 ring55: Đủ chuỗi WMI_INIT -> CHAN_LIST -> VDEV -> SCAN nhưng VẪN CRASH (2026-11-07 05:49 — 05:55)

**ring55 — SCAN_CHAN_LIST (driver hiện tại):** Thêm
picasso_wlan_send_scan_chan_list() theo ios-redfin scan_start (CMD 0x3003,
TAG 0x4f + TAG_ARRAY_STRUCT 0x12 + 11x TAG_CHANNEL 0x50, 332 bytes). Gửi trên
CE3 sau WMI_INIT, trước VDEV_CREATE.
**Kết quả boot ring55:** WMI_INIT (228B) [slot 0], SCAN_CHAN_LIST (332B)
[slot 1], VDEV_CREATE (36B) [slot 2], SCAN (184B) [slot 3] — tất cả dispatch
sạch, đúng thứ tự ios-redfin — **phone VẪN CRASH** (PANIC, không có "scan
complete", không có beacon).
**Chẩn đoán mới:** ios-redfin chạy WMI theo trình tự SERVICE_READY -> INIT ->
READY (event 0x2) rồi mới scan. Driver ta gửi INIT, CHAN_LIST, VDEV, SCAN
dồn dập ngay sau handshake, KHÔNG chờ FW gửi WMI_READY — FW có thể crash vì
nhận lệnh khi chưa init xong. Bước tiếp: tách CHAN_LIST/VDEV/SCAN ra khỏi
handler, chờ FW xử lý INIT (poll CE2 ~1s để nhận READY) rồi mới gửi các
lệnh tiếp theo.

### 3.36 ring56-59: Crash không phải do lệnh scan mà do poll/scan chạy sai ngữ cảnh (2026-11-07 05:56 — 06:35)

**ring56 — delay 1s sau WMI_INIT (driver ring55 + delay):** Chuyển CHAN_LIST/
VDEV/SCAN sang picasso_sta_scan, chờ ~1s (50x ce_poll + IOSleep 20ms) sau
WMI_INIT để FW kịp gửi WMI_READY. **Kết quả:** phone crash TRƯỚC khi scan
dispatch (không có SCAN_CHAN_LIST trong log). Chẩn đoán: crash xảy ra trong
vòng poll CE2, không phải do lệnh scan.
**ring57 — tắt beacon scan loop:** Nghi vòng quét beacon trong ce_poll (quét
mọi byte offset tìm 0x80/0x50/0x40 + 0x00 rồi gọi parser trên dữ liệu WMI
ngẫu nhiên) gây crash. Tắt vòng này (if (0 && ...)). **Kết quả:** phone VẪN
crash trước scan dispatch. Beacon loop không phải nguyên nhân.
**ring58 — re-entrancy guard cho ce_poll:** Phát hiện picasso_sta_scan được
gọi TỪ TRONG ce_poll (auto-scan trigger) và tự gọi ce_poll trong delay/dwell
— poll lồng nhau làm hỏng g_ce2_sw_index/DRRI. Thêm `static bool poll_active`
guard (trả về ngay nếu đang poll, clear ở mọi early return). **Kết quả:**
phone VẪN crash trước scan dispatch. Re-entrancy không phải (chỉ) nguyên
nhân — delay 1s làm modem thread bị block, FW timeout/crash.
**ring59 — async auto-scan (driver hiện tại):** Chuyển auto-scan ra khỏi
ce_poll: CE2 handler chỉ đặt cờ g_auto_scan_pending; picasso_wlan_poll
(.poll callback, gọi sau ce_poll) chạy sta_scan ngoài ce_poll; bỏ delay 1s,
gửi CHAN_LIST+VDEV+SCAN ngay (thứ tự ring55). Sửa nhiều lỗi forward
declaration (globals auto_scan_done/g_auto_scan_pending/g_wlan_hw_scan phải
đặt TRƯỚC picasso_wlan_poll; forward decl picasso_sta_scan). **Kết quả:**
"running auto-scan" xuất hiện (async trigger HOẠT ĐỘNG), sta_scan bắt đầu
("scanning over-the-air") — **nhưng phone VẪN PANIC ngay sau đó, trước
CHAN_LIST**. Chẩn đoán: picasso_wlan_poll (.poll callback của modem) chạy
trong ngữ cảnh không an toàn để gọi sta_scan (block/sleep/I/O) — hoặc
ce_rd trong sta_scan fault. **Hướng tiếp:** bỏ auto-scan hoàn toàn; boot
không scan (phone sống như ring49), rồi kích scan bằng lệnh console
`wifi scan` (chạy ở user context, an toàn) và đọc kết quả từ kernel log.

### 3.37 ring60-61: BREAKTHROUGH — WMI_INIT chính là thứ làm crash phone (2026-11-07 06:38 — 06:50)

**ring60 — bỏ auto-scan (driver ring59 trừ poll trigger):** Xoá block auto-scan
khỏi picasso_wlan_poll. **Kết quả:** HANDSHAKE COMPLETE, không có auto-scan —
**nhưng phone VẪN CRASH** sau handshake. Chứng tỏ crash KHÔNG phải do
auto-scan. Nghi phạm chuyển sang WMI_INIT: nó được gửi tự động từ CE2 handler
sau handshake (từ ring52), là TX lớn duy nhất chạy ngay cả khi không scan.
**ring61 — tắt WMI_INIT (driver hiện tại):** Comment out lệnh gọi
picasso_wlan_send_wmi_init() trong CE2 handler. **Kết quả: PHONE SỐNG!**
HANDSHAKE COMPLETE, /dev/ttyACM0 tồn tại, phone ở root shell, không crash.
**Chẩn đoán xác định: WMI_INIT (228 bytes trên CE3) là thứ làm crash phone.**
HTC 16/20B trên CE0 chạy tốt; WMI_INIT 228B trên CE3 crash. Nguyên nhân có
thể: (a) CE3 TX descriptor sai cho message lớn (nbytes/flags); (b) nội dung
WMI_INIT sai (TLV/length) làm FW crash khi parse; (c) CE3 ring/buffer cấu
hình sai (SR/DR base/size). Bước tiếp: so sánh CE3 TX path với CE0 (đang
chạy tốt) — descriptor, buffer PA, ring nent/nbytes — và kiểm tra lại nội
dung WMI_INIT theo ios-redfin wmi_send_init từng byte.

### 3.38 ring62: eid=1 KHÔNG sửa được crash WMI_INIT (2026-11-07 06:52 — 06:58)

**ring62 — HTC eid fix (driver hiện tại):** Phát hiện buf[0] của HTC header là
ENDPOINT ID, không phải CE/pipe. WMI control endpoint là eid=1 (ath10k), chạy
trên CE3 cho TX. Driver đã dùng buf[0]=pipe (3) — sai. Sửa cả 4 hàm WMI
(WMI_INIT, SCAN_CHAN_LIST, VDEV_CREATE, WMI_SCAN) sang buf[0]=1, bật lại
WMI_INIT. **Kết quả:** WMI_INIT (228B) dispatch [slot 0], HANDSHAKE COMPLETE
— **phone VẪN CRASH** (ttyACM0 mất). eid không phải (chỉ) nguyên nhân.
**Chẩn đoán:** Crash vẫn do WMI_INIT 228B trên CE3, nhưng không phải do eid.
Khả năng còn lại: (a) CE3 ring cấu hình sai (SR/DR base/size/CTRL1) — FW đọc
descriptor rác; (b) CE3 TX buffer/SMMU mapping sai — DMA tới địa chỉ xấu;
(c) nội dung WMI_INIT sai ở chi tiết nào đó làm FW crash khi parse (dù
resource config đã khớp ios-redfin). Bước tiếp: kiểm tra program_rings() cho
CE3 so với CE0 (đang chạy tốt), và/hoặc thử gửi message NHỎ trên CE3
(VDEV_CREATE 36B, không có WMI_INIT) để tách riêng lỗi CE3 vs lỗi nội dung
WMI_INIT.

### 3.39 ring63: CE3 TX hỏng hoàn toàn — message nhỏ 36B cũng crash (2026-11-07 07:00 — 07:05)

**ring63 — test CE3 TX nhỏ (driver hiện tại):** Tắt WMI_INIT, chỉ gửi
VDEV_CREATE (36B) trên CE3 sau handshake, để tách "CE3 TX hỏng" khỏi
"WMI_INIT hỏng". **Kết quả:** VDEV_CREATE (36B) dispatch [slot 0],
HANDSHAKE COMPLETE — **phone VẪN CRASH** (ttyACM0 mất).
**Chẩn đoán xác định: CE3 TX hỏng hoàn toàn.** Message nhỏ 36B cũng crash,
không phải do size 228B hay nội dung WMI_INIT. CE0 TX (HTC 16/20B) chạy tốt;
CE3 TX (bất kỳ size nào) crash. Khả năng: (a) CE3 dùng SMMU stream ID khác
với CE0, không nằm trong CB5 bypass (hiện chỉ cover SMR[7]=0xc0 và
SMR[8]=0x640) — DMA của CE3 fault; (b) CE3 ring cấu hình sai (SR/DR/CTRL1);
(c) CE3 buffer PA không nằm trong pool đã map SMMU. Bước tiếp: tìm tất cả
stream ID của WLAN (wlfw CE table / ath10k snoc), kiểm tra SMR nào cover CE3,
thêm bypass cho stream ID đó (CB5 hoặc S2CR tương ứng).

### 3.40 ring64: Delay WMI_INIT cũng không cứu được — CE3 TX hỏng ở mức DMA (2026-11-07 07:10 — 07:15)

**ring64 — WMI_INIT gửi muộn (driver hiện tại):** Chuyển WMI_INIT sang
picasso_wlan_poll (gửi ở poll sau handshake, để FW kịp gửi SERVICE_READY
event 0x1 như ios-redfin). **Kết quả:** HANDSHAKE COMPLETE, WMI_INIT (228B)
dispatch [slot 0] — **phone VẪN CRASH** (ttyACM0 mất).
**Chẩn đoán xác định: CE3 TX hỏng ở mức DMA, không phải timing/eid/nội
dung/size.** Mọi message trên CE3 đều crash (WMI_INIT 228B, VDEV_CREATE
36B), dù gửi ngay sau handshake hay gửi muộn; CE0 TX (HTC 16/20B) chạy tốt.
CE0 và CE3 có config giống hệt nhau (PD_OUT, 32, 2048, 7) nên ring config
không phải nguyên nhân. Nghi phạm mạnh nhất: **CE3 TX DMA dùng SMMU stream
ID khác với CE0** — hiện chỉ bypass SMR[7]=0xc0 và SMR[8]=0x640; nếu CE3 TX
dùng SID thứ ba (vd 0xc1/0xc3/0x641...), DMA của nó fault. Bước tiếp: tìm tất
cả stream ID của WLAN WCN3990 (device tree iommus / ath10k snoc / qcom
SMMU), kiểm tra SMR nào cover CE3 TX, thêm bypass cho SID đó.

### 3.41 ring65: Sửa handshake theo ios-redfin — nhưng CONNECT(WMI) bị flag cũ chặn (2026-11-07 07:23 — 07:28)

**ring65 — handshake đúng thứ tự (driver hiện tại):** Trên HTT RESP, gửi
CONNECT(WMI) thay vì SETUP_COMPLETE (ring48 đã gửi SETUP ngay, không bao
giờ CONNECT WMI sau HTT RESP — CONNECT(WMI) duy nhất là poke ring46 gửi
TRƯỚC READY, FW bỏ qua). Nhánh WMI RESP đã có sẵn sẽ gửi SETUP_COMPLETE.
**Kết quả:** Phone SỐNG (ttyACM0 tồn tại) — nhưng log cho thấy CONNECT(WMI)
KHÔNG được gửi ("HTT RESP -> CONNECT WMI" vắng), không có WMI RESP, không
có WMI_INIT. Phone sống chỉ vì WMI_INIT chưa bao giờ được gửi. **Nguyên
nhân:** poke ring46 đã đặt g_htc_wmi_sent=true, chặn CONNECT(WMI) sau HTT
RESP. **Fix ring66:** dùng flag riêng g_htc_wmi_post_htt_sent cho
CONNECT(WMI) sau HTT RESP.

### 3.42 ring66: CONNECT(WMI) đã gửi sau HTT RESP — FW vẫn không trả WMI RESP (2026-11-07 07:27 — 07:33)

**ring66 — CONNECT(WMI) sau HTT RESP (driver hiện tại):** Dùng flag riêng
g_htc_wmi_post_htt_sent (ring65 bị poke ring46 chặn). **Kết quả:** "HTT RESP
-> CONNECT WMI" xuất hiện (CONNECT(WMI) ĐÃ GỬI), phone SỐNG (ttyACM0 tồn
tại) — nhưng FW KHÔNG trả WMI CONNECT_RESP (chỉ 1 CONNECT_RESP, của HTT
svc=0x300 status=7 eid=208). Không có WMI RESP → không handshake_done →
không WMI_INIT → phone sống vì chưa gửi gì trên CE3.
**Chẩn đoán:** FW nhận CONNECT(WMI) (format khớp ios-redfin: msg 2, svc
0x0100, flags 0x0100) nhưng không trả RESP. FW trả HTT RESP (status=7) nhưng
bỏ qua WMI CONNECT. Khả năng: (a) FW muốn WMI CONNECT TRƯỚC HTT (thứ tự
ngược ios-redfin); (b) HTT status=7 nghĩa là HTT connect thất bại, FW ở
trạng thái không nhận WMI; (c) parse CONNECT_RESP sai offset nên status/eid
thực khác. Bước tiếp: thử đảo thứ tự — trên READY gửi CONNECT(WMI) trước,
trên WMI RESP gửi CONNECT(HTT), trên HTT RESP gửi SETUP_COMPLETE.

### 3.43 ring67: WMI-first cũng không được — FW không trả WMI RESP dù thứ tự nào (2026-11-07 07:32 — 07:38)

**ring67 — WMI CONNECT trước (driver hiện tại):** Trên READY gửi
CONNECT(WMI) trước; trên WMI RESP gửi CONNECT(HTT); trên HTT RESP gửi
SETUP_COMPLETE. **Kết quả:** "CONNECT WMI (ring67)" xuất hiện, phone SỐNG
(ttyACM0 tồn tại) — nhưng FW KHÔNG trả CONNECT_RESP nào (không WMI RESP,
không HTT RESP). Không handshake, không WMI_INIT.
**Chẩn đoán:** FW trả HTT RESP (status=7) khi HTT CONNECT gửi sau READY
(ring65/66), nhưng KHÔNG BAO GIỜ trả WMI RESP, dù WMI CONNECT gửi trước
(ring67) hay sau HTT RESP (ring66). Format CONNECT(WMI) khớp ios-redfin
(msg 2, svc 0x0100, flags 0x0100) nhưng FW bỏ qua. Ngoài ra, parse CE2
không đáng tin: các message 20B có prefix `00 0c 00 00 00 00 <seq> 00`,
hlen đọc được = 0, msg = 512/1 — không phải HTC header chuẩn. "CONNECT_RESP
svc=0x300 status=7 eid=208" có thể là misparse của READY hoặc message khác.
**Bước tiếp:** parse lại CE2 message thô theo ath10k HTC RX (xác định đúng
offset của eid/len/msg_id), dump toàn bộ CE2 traffic sau CONNECT(WMI) để xem
FW có trả gì không; kiểm tra xem FW có yêu cầu CREDIT (HTC_FLAG_NEED_CREDIT)
hay endpoint khác cho WMI.

### 3.44 ring68: BREAKTHROUGH — CE RX data bị byte-swap theo từng u32 (2026-11-07 07:40 — 07:45)

**Phân tích lại CE2 message thô (ring67):** Các message 20B có prefix
`00 0c 00 00 00 00 <seq> 00` và parse ra hlen=0, msg=512/1 — vì data bị
**byte-swap theo từng từ 32-bit**. Đảo ngược từng u32:
- slot 0 → eid=0 len=12 seq=0 msg=1 (READY). ✓
- slot 1 → eid=0 len=12 seq=1 msg=3 (CONNECT_RESP) svc=0x100 (WMI_CONTROL)
  status=0 assigned_eid=1 max=2040. ✓✓✓
- slot 2 → msg=3 svc=0x100 status=0 assigned_eid=2 max=2040. ✓
**Kết luận: FW CÓ trả WMI CONNECT_RESP (status=0, eid=1)!** Driver chỉ không
parse được vì không unswap. Nguyên nhân: wlfw_ce_tbl flags=7 gồm BYTE_SWAP
(2) — FW swap data RX; descriptor (nbytes=20) không bị swap nên vẫn đọc
đúng. TX trên CE0 chạy tốt nên chỉ cần unswap phía RX (CE1/CE2).
**Fix ring68:** trong ce_poll, sau khi copy message từ CE RX buffer, đảo
ngược từng u32 trước khi parse (HTC header, WMI events). Sau fix này, WMI
CONNECT_RESP sẽ được nhận đúng, handshake hoàn tất theo thứ tự ios-redfin,
và WMI_INIT trên CE3 có thể không còn crash (vì WMI endpoint đã được FW
thiết lập đúng).

### 3.45 ring68 test: byte-swap fix WORKS — handshake completes status=0, phone survives (2026-11-07 07:53 — 08:00)
- Build: unswap code in ce_poll (ring68), kernel 07:43, md5 9dba881b. First boot attempt 07:53 never ran (setsid/SSH hiccup, no ring68-run.log, gui log stayed ring67 07:36). User manually rebooted phone to xnuloader; second boot 07:58 ran clean (ring68-run.log 08:00:03).
- CE2 parse now CORRECT: slot 0 eid=0 hlen=12 msg=1 (READY); slot 1 msg=3 CONNECT_RESP svc=0x100 status=0 eid=1 (WMI); slot 2 msg=3 HTT RESP svc=0x300 status=0 eid=3. HANDSHAKE COMPLETE, en0 attached, MISSION MODE ATTACH COMPLETE, phone alive (ttyACM0, log to t=86s). FW sends real WMI traffic: slot 5 nbytes=340 (SERVICE_READY, fw_version 0x321b002a), slot 6 nbytes=988.
- BUT WMI_INIT was NOT sent: log line 985 "picasso_wlan_send_wmi_init: WMI pipe 2 is not a FW TX pipe (dir=1), refusing". Root cause: g_wmi_eid (CE pipe) is overwritten by CONNECT_RESP handler with FW-assigned HTC eid (1, 2). WMI senders used pipe=g_wmi_eid, so pipe became 2 (an RX pipe). Fix in ring69.

### 3.46 ring69: WMI TX pipe hardcoded to CE3 — WMI_INIT dispatched, phone PANICS immediately (2026-11-07 08:02 — 08:05)
- Fix: all 4 WMI senders now use pipe=3 (CE3, ath10k) instead of pipe=g_wmi_eid; HTC header eid buf[0]=1 already hardcoded (ring62). Driver md5 335272ec, kernel 08:02.
- Boot 08:04: handshake completes again (WMI status=0 eid=1/2, HTT status=0 eid=3). Log: "Dispatched WMI_INIT (228 bytes) to CE3 [slot 0]" — fix works. Then "auto-scan pending" (ring59 diag; picasso_wlan_poll returns 0, auto-scan does NOT run) and "=== PICASSO CDC PANIC BEGIN ===" with no panic detail; phone back in fastboot.
- So WMI_INIT on CE3 crashes even with a CORRECT handshake (status=0). Suspect: WMI_INIT sent from picasso_wlan_poll right after handshake, BEFORE FW sends SERVICE_READY (event 0x1). Standard WMI flow is SERVICE_READY -> WMI_INIT -> WMI_READY. Next (ring70): gate WMI_INIT on SERVICE_READY (CE2 WMI event 0x1, 340B) before sending.

### 3.47 ring70: WMI_INIT gated on SERVICE_READY — still PANICS, timing hypothesis RULED OUT (2026-11-07 08:21 — 08:24)
- Fix: g_wmi_service_ready flag, set in CE2 handler when WMI event 0x1 (SERVICE_READY, eid 1/2, nbytes>=100) arrives after unswap; picasso_wlan_poll only sends WMI_INIT when flag true. Driver md5 ec068e6e, kernel 08:22.
- Boot 08:23: handshake completes (WMI status=0, HTT status=0). Log: "*** WMI SERVICE_READY received on CE2 (eid=2, nbytes=340) ***" — detection works. "Dispatched WMI_INIT (228 bytes) to CE3 [slot 0]" — sent AFTER SERVICE_READY. Then "=== PICASSO CDC PANIC BEGIN ===", phone back in fastboot. No panic detail.
- Conclusion: timing is NOT the cause. WMI_INIT on CE3 crashes even after SERVICE_READY. Remaining suspects: (a) WMI_INIT content subtly wrong despite byte-match with ios-redfin; (b) CE3 TX path broken (descriptor/ring base/size/buffer offset) — CE0 TX works but CE3 TX (WMI_INIT 228B, VDEV_CREATE 36B ring63) always crashes. Next: compare CE3 ring/descriptor setup against working CE0.

### 3.48 CE3 deep dive: chain mask 0x7 vs FW 2 chains is the prime suspect; buffer overlap is a real latent bug (2026-11-07 08:26 — 08:31, analysis only, no boot)
- Service table (wlfw_svc_tbl) is CORRECT: WMI 0x100 OUT->CE3, IN->CE2; HTT 0x300 OUT->CE4, IN->CE1; control 0x1 OUT->CE0, IN->CE2. FW does expect WMI on CE3.
- ce_kick_src (CE0/CE3 only) and the 16B descriptor format are correct; WMI_INIT payload is built directly into the CE3 buffer (wmi = buf+8), HTC header eid=1, total 228B. No missing memcpy.
- PRIME SUSPECT — chain mask: WMI_INIT TAG_RESOURCE_CONFIG (44 u32s) contains 0x7, 0x7 (tx/rx chain mask = 3 chains). ws-n-wifi (ios-redfin) notes INIT says 0x7=3 chains but FW has 2 — FW indexes per-chain state past 2 without narrowing. FW is WCN3990 HL.3.2.1; SERVICE_READY (340B) carries rf_chains, but driver hardcodes 0x7 and never reads it. FW likely crashes processing WMI_INIT when it tries to init a 3rd chain that does not exist.
- LATENT BUG — buffer overlap: pool 256KB; CE bufs CE0 0x08000, CE1 0x0C000, CE2 0x1C000, CE3 0x28000, each 32x2048B=64KB. CE0 (0x08000-0x18000) overlaps CE1 (0x0C000-0x1C000); CE3 slot 0 (0x28000) == CE2 slot 24 exactly. CE2 had only used slots 0-7 when WMI_INIT was sent, so slot 24 had not been written yet — overlap is probably NOT the immediate crash cause, but it must be fixed (relocate buffers / reduce nent) before sustained scanning.
- Proposed ring71: change rc_init chain mask 0x7, 0x7 -> 0x3, 0x3 (2 chains) and boot. If phone survives WMI_INIT, that is the cause; if it still crashes, fix buffer overlap and parse SERVICE_READY for the true rf_chains.

### 3.49 ring71: chain mask 0x3 also crashes — chain mask RULED OUT (2026-11-07 08:37 — 08:40)
- Fix: rc_init chain mask 0x7, 0x7 -> 0x3, 0x3 (2 chains). Driver md5 7a6aa3eb, kernel 08:38.
- Boot 08:39: handshake completes, SERVICE_READY received (eid=2, nbytes=340), WMI_INIT (228B) dispatched to CE3 [slot 0] after SERVICE_READY — phone PANICKED immediately after, back in fastboot. Log stops at "=== PICASSO CDC PANIC BEGIN ===".
- Ruled out so far for the WMI_INIT crash: timing (ring70, waited for SERVICE_READY), chain mask (ring71, 0x3). Remaining: another resource-config value exceeds FW limits, or CE3 TX path (buffer overlap: CE3 slot 0 == CE2 slot 24; CE0 buf overlaps CE1 buf), or WMI_INIT format subtly wrong.
- Next (ring72, proposed): parse SERVICE_READY (340B) in the driver and log the FW's true limits (rf_chains, num_vdevs, num_peers, etc.) — SERVICE_READY arrives before WMI_INIT is sent, so the log will carry the FW's capabilities even though the phone crashes afterwards. Compare against WMI_INIT rc_init and fix any value that exceeds FW limits. Needs ios-redfin radio.c SERVICE_READY parse offsets (ws-n-wifi).

### 3.50 TLV order breakthrough: picasso writes {tag, len}, FW expects {len, tag} (2026-11-07 08:41 — 08:42, analysis only, no boot)
- ios-redfin radio.c put_tlv(tag, len) writes LEN first then TAG: put_tlv(0x4a, 28) -> bytes 1c 00 4a 00. Its wmi_svc_ready also parses SERVICE_READY TLVs as {len u16 @0, tag u16 @2}. ios-redfin works on redfin (same SM7250/WCN3990), so the FW uses {len, tag} TLVs.
- Picasso driver writes TAG first then LEN in all 4 WMI senders: WMI_INIT 4a 00 1c 00; SCAN_CHAN_LIST 4f 00 04 00; same pattern in VDEV_CREATE and WMI_SCAN. FW reads the first WMI_INIT TLV as len=74 (0x4a), tag=28 (0x1c) — does not recognise INIT_CMD, misparses the rest of the 228B message, and crashes. This explains the immediate panic after WMI_INIT dispatch in rings 69-71 regardless of timing (ring70) or chain mask (ring71).
- The earlier "WMI_INIT byte-identical" verification was wrong — it compared tag/len VALUES, not byte order. The SERVICE_READY parse (wmi_svc_ready) also gives rf_chains at TAG_SERVICE_READY(0x20) value+36 and ht_cap_info at +40, eeprom regdomain at tag 0x21 value+0; chain mask should be (1<<rf_chains)-1, not hardcoded (ring71's 0x3 was a guess).
- Proposed ring72 (replaces the SERVICE_READY-parse-only plan): swap TLV order to {len u16, tag u16} in all 4 WMI senders (match put_tlv), and parse SERVICE_READY for rf_chains to set the chain mask properly. If the phone survives WMI_INIT, this is the root cause.

### 3.51 ring72 TESTED (2026-11-07 08:45-08:48, user approved "Ok"): TLV order fix did NOT fix the crash
- Driver md5 b5b81dd0, kernel 15672080 B built 08:45, C++ hakios.picasso_wlan.cpo recompiled. 12 TLV pairs swapped to {len, tag} in all 4 WMI senders (CHAN_LIST 0x4f/4, 0x12/308, 0x50/24; VDEV 0x56/20; INIT 0x4a/28, 0x4b/176, 0x12/0; SCAN 0x4d/100, 0x10/44, 0x13/0, 0x13/8, 0x11/0); cmd-ID writes NOT swapped. SERVICE_READY TLV parse added (buf+12, {len,tag}).
- Boot: handshake completes, SERVICE_READY parse WORKS — log line 985: "*** SERVICE_READY fw=321b002a abi=01000000 rf_chains=2 ht_caps=0x381b ***" (rf_chains=2 CONFIRMED, so chain mask 0x3 was correct), line 986 eeprom_regdomain=0. Line 991 WMI_INIT (228B) dispatched to CE3 [slot 0] — phone PANICKED immediately after (back in xnuloader fastboot, USB 18d1:d00d; log ends at line 991, 0 PANIC lines). TLV-ORDER HYPOTHESIS RULED OUT (at least as the sole cause).
- Ruled out so far for the WMI_INIT crash: timing (ring70), chain mask (ring71), TLV order (ring72). Remaining: (a) host memory — ios-redfin allocates pool memory for the FW's SERVICE_READY mem_req TLVs (TAG_ARRAY_STRUCT) and sends n_chunks; picasso always sends n=0 and never parses mem_req; if FW requires host memory, WMI_INIT n=0 could crash it; (b) another rc_init value exceeds FW limits (NUM_VDEVS=4, NUM_PEERS=33 — FW's true limits unknown, not in TAG_SERVICE_READY); (c) CE3 TX path / buffer overlap (CE3 slot 0 == CE2 slot 24); (d) WMI_INIT format still wrong another way (ABI version, total length).
- Proposed ring73 (awaiting approval): parse the SERVICE_READY mem_req TLVs (TAG_ARRAY_STRUCT, wmi_req: req_id/unit_size/num_unit_info/num_units) and log what host memory the FW wants; then decide whether to allocate it from the pool and send n_chunks.

### 3.52 ring73 TESTED (2026-11-07 08:53-08:56, blanket approval): mem_req parse — NO mem_req logged
- Driver md5 ba7e7ef9, kernel 15672080 B built 08:53. Added TAG_ARRAY_STRUCT (0x12) inner-TLV walk for TAG_HOST_MEM_REQ (0x22, 16B wmi_mem_req) in the SERVICE_READY parse.
- Boot: handshake completes, SERVICE_READY received (fw=321b002a rf_chains=2 ht_caps=0x381b, eeprom_rd=0), WMI_INIT dispatched to CE3 — phone PANICKED immediately after (back in fastboot). NO mem_req lines in the log. Either the 340B SERVICE_READY has no TAG_ARRAY_STRUCT/mem_req TLVs, or the parse loop broke before reaching them.
- SSH sessions to k60 hung twice during this ring (boot-start and check sessions stuck ~2-4 min, no output); recovered on retry — transient host/gate overload.

### 3.53 ring74 TESTED (2026-11-07 09:03-09:05, blanket approval): SERVICE_READY TLV dump — only 1 TLV logged before crash
- Driver md5 (TLV dump added), kernel 15672080 B built 09:03. Added a LOG of every SERVICE_READY TLV (o, tag, len) in the parse loop.
- Boot: handshake completes, SERVICE_READY (340B, eid=2) received. Log shows WMI_INIT dispatched (line 990) BEFORE the first TLV dump line (line 991): "*** SERVICE_READY TLV o=12 tag=0x20 len=128 ***". Only ONE TLV logged, then log ends (991 lines), phone back in fastboot. The fw/rf_chains/mem_req logs (ring72/73) did NOT appear this time.
- Interpretation: WMI_INIT is dispatched from picasso_wlan_poll as soon as g_wmi_service_ready is set, while ce_poll is still inside the SERVICE_READY TLV parse loop. The phone crashes so fast after WMI_INIT dispatch that the rest of the parse output never reaches the log. The extra LOG calls in ring74 slowed the parse enough that even the fw/rf_chains lines were lost. This is a LOG RACE, not new evidence about mem_req — the mem_req question is still open (need the full TLV list, which requires delaying WMI_INIT until the parse completes, or dumping TLVs to a buffer instead of LOG).
- Ruled out so far: timing (ring70), chain mask (ring71), TLV order (ring72), host memory (ring73 — inconclusive, no mem_req seen but parse may have been cut short).
- Next: (a) gate WMI_INIT on parse completion (set a flag AFTER the TLV loop finishes, not when SERVICE_READY is first detected), so the full TLV dump + mem_req log lands before WMI_INIT is sent; (b) compare CE3 TX descriptor/ring setup against working CE0 TX — the crash is immediate on dispatch, suggesting the FW faults reading the CE3 message (descriptor, nbytes, or buffer) rather than rejecting its content.

### 3.54 ring75 TESTED (2026-11-07 09:16-09:18, blanket approval): gate WMI_INIT on parse completion — FULL TLV DUMP, host memory RULED OUT
- Driver md5 (gate added), kernel 15672080 B built 09:16. Moved g_wmi_service_ready=true to AFTER the SERVICE_READY TLV parse loop, so WMI_INIT is not dispatched until the full dump lands.
- Boot: handshake completes, SERVICE_READY (340B) received. FULL TLV DUMP landed before WMI_INIT (lines 988-995):
  - o=12 tag=0x20 len=128 (TAG_SERVICE_READY; fw=321b002a rf_chains=2 ht_caps=0x381b)
  - o=144 tag=0x21 len=40 (HAL_REG_CAPABILITIES; eeprom_rd=0)
  - o=188 tag=0x10 len=128 (service bitmap, 32 u32s — NOT parsed yet)
  - o=320 tag=0x12 len=0 (TAG_ARRAY_STRUCT EMPTY — NO mem_req!)
  - o=324 tag=0x10 len=12 (3 u32s)
  - Total 12+132+44+132+4+16 = 340. Parse complete logged (line 995), WMI_INIT dispatched (line 1000) — phone PANICKED immediately after, back in fastboot.
- HOST-MEMORY HYPOTHESIS RULED OUT: TAG_ARRAY_STRUCT len=0 means the FW does NOT request host memory; WMI_INIT n=0 is correct. Ruled out so far: timing (ring70), chain mask (ring71), TLV order (ring72), host memory (ring75).
- Remaining: (a) another rc_init value exceeds FW limits (NUM_VDEVS=4, NUM_PEERS=33 — check against service bitmap / FW caps); (b) CE3 TX path — crash is immediate on dispatch, suggesting the FW faults reading the CE3 message (descriptor, nbytes, buffer) rather than rejecting its content; (c) buffer overlap (CE3 slot 0 == CE2 slot 24) — CE2 had only used slots 0-7, so probably not the immediate cause, but still a latent bug.
- Next (ring76): compare CE3 TX descriptor/ring setup against working CE0 TX (SR_BASE/SIZE/CTRL1, descriptor nbytes/flags, kick), and verify the WMI_INIT nbytes in the CE3 descriptor is exactly 228.

### 3.55 ring76 TESTED (2026-11-07 09:25-09:27, blanket approval): buffer-overlap fix — did NOT fix the crash
- Driver md5 (buf fix), kernel 15672080 B built 09:25. Fixed CE buffer overlap: capped CE0..CE3 at 16 ring entries (was 32) and re-spaced buffer offsets to CE0 0x08000, CE1 0x10000, CE2 0x18000, CE3 0x20000 (each 16x2048=32KB, no overlap; old CE3 slot 0 == CE2 slot 24).
- Boot: handshake completes, SERVICE_READY parse complete, WMI_INIT dispatched to CE3 [slot 0] — phone PANICKED immediately after, back in fastboot. BUFFER-OVERLAP HYPOTHESIS RULED OUT. Ruled out so far: timing (ring70), chain mask (ring71), TLV order (ring72), host memory (ring75), buffer overlap (ring76).

### 3.56 ring77 TESTED (2026-11-07 09:31-09:33, blanket approval): CE descriptor flags fix — did NOT fix the crash
- Driver md5 (desc flags), kernel 15672080 B built 09:31. ios-redfin ce_send sets descriptor flags = (HTC eid << 4) & 0xfff0: CE0 control eid=0 -> 0x00, CE3 WMI eid=1 -> 0x10, CE4 HTT eid=3 -> 0x30. Picasso ce_kick_src set flags=0 for every CE. Fixed: flags = (ce==3) ? 0x10 : (ce==4) ? 0x30 : 0x00. This explained why CE0 TX worked (eid 0, flags 0 correct) and CE3 TX always crashed (WMI eid 1 but flags 0).
- Boot: handshake completes, SERVICE_READY parse complete, WMI_INIT dispatched to CE3 [slot 0] — phone PANICKED immediately after, back in fastboot. DESCRIPTOR-FLAGS HYPOTHESIS RULED OUT (at least as the sole cause). The descriptor now matches ios-redfin (nbytes=228, flags=0x10, third u32 = 0x1000E4), yet the FW still crashes on receipt.
- Ruled out so far: timing, chain mask, TLV order, host memory, buffer overlap, descriptor flags. Remaining: (a) WMI_INIT content — a specific rc_init value the FW rejects (NUM_VDEVS/NUM_PEERS or another field) despite matching ios-redfin; the FW is HL.3.2.1.c2 and may have tighter limits than redfin's FW; (b) the CE3 SR ring itself — e.g. the FW expects a DR (destination ring) also programmed on CE3, or the SR_SIZE/CTRL1 DMAX for CE3 differs from what the FW expects; (c) the WMI_INIT is sent while the FW is still in a state where it cannot accept it (e.g. before it finishes an internal init that SERVICE_READY does not signal).
- Next: (a) try sending WMI_INIT with minimal rc_init (all zeros except chain mask) to see if the FW accepts a trivial INIT — isolates content vs transport; (b) dump the CE3 SR registers (SR_BASE/SIZE/CTRL1) BEFORE the FW is armed, to verify CE3's ring programming matches CE0's; (c) compare the full WMI_INIT byte stream against a known-good ath10k WMI_INIT capture for WCN3990.

### 3.57 ring78 TESTED (2026-11-07 09:38-09:40, blanket approval): minimal rc_init — still crashes
- Driver md5 (minimal rc), kernel 15672080 B built 09:38. Zeroed all rc_init except NUM_VDEVS=4, NUM_PEERS=33, chain mask 0x3 (rc[7]=rc[8]=0x3); everything else 0. Isolates WMI_INIT content vs CE3 transport.
- Boot: handshake completes, SERVICE_READY parse complete, WMI_INIT (228B) dispatched to CE3 [slot 0] — phone PANICKED immediately after, back in fastboot. MINIMAL-CONTENT HYPOTHESIS RULED OUT: even a trivial rc_init crashes. The crash is NOT caused by a specific rc_init value.
- Ruled out so far: timing, chain mask, TLV order, host memory, buffer overlap, descriptor flags, rc_init content. The WMI_INIT message (HTC hdr, cmd ID, TLVs, ABI, rc) now matches ios-redfin byte-for-byte in structure, the CE3 ring/descriptor/buffer/kick match ios-redfin ce_send, yet the FW still crashes on receipt.
- ios-redfin ce_attr (ath10k host_ce_config_wlan) confirms: CE0 src_n=16, CE1 dst_n=512, CE2 dst_n=64, CE3 src_n=32 dst_n=0 (TX-only, no DR — picasso correct not to program a CE3 DR), CE4 src_n=2048. Picasso programs CE3 SR_SIZE=32, DMAX=2048, SRC_WM=32 — matches. Descriptor flags=(eid<<4) now matches too.
- Remaining suspects: (a) the FW is HL.3.2.1.c2 and expects a WMI_INIT variant picasso/ios-redfin do not send (e.g. a different ABI, or an extra TLV); (b) a CE3 hardware/SMMU quirk specific to picasso (SM7250) that redfin (also SM7250) does not hit — unlikely but possible if the SMMU CB5 bypass range does not cover the CE3 ring/buffer PA; (c) the WMI_INIT must be preceded by another WMI command or QMI step that picasso skips (e.g. a WMI service-available handshake, or the FW needs the host to consume the SERVICE_AVAILABLE event 0x22f first).
- Next: (a) check whether the FW sends a SERVICE_AVAILABLE event (tag 0x22f) before SERVICE_READY and whether picasso must ack/consume it; (b) verify the CE3 SR ring PA and buffer PA are inside the SMMU CB5 bypass window by reading back the programmed CE3 SR_BASE/SIZE (carefully — CE register reads after programming faulted before; do it BEFORE arming the CE, in the pre-programming dump); (c) capture the exact 228B WMI_INIT byte stream from the CE3 buffer (log the buffer contents before kick) and diff it against ios-redfin's wmi_send_init output byte-by-byte.

### 3.58 ring79 TESTED (2026-11-07 09:59-10:01, blanket approval): WMI_INIT hex dump — bytes are CORRECT, FW still crashes
- Driver md5 (hex dump), kernel 15672080 B built 09:59. Added a hex dump of the 228B WMI_INIT buffer before ce_kick_src.
- Boot: handshake completes, SERVICE_READY parse complete, WMI_INIT hex dumped (line 998), dispatched to CE3 [slot 0] — phone PANICKED immediately after, back in fastboot. Hex dump (truncated in log, first ~120B visible):
  0100dc00 00000000 01000000 1c004a00 00000001 35000000 5143415f 4d4c0000 00000000 00000000 00000000 b0004b00 04000000 21000000 00000000 ... 03000000 03000000 00000000...
  Decodes as: HTC eid=1 len=220; cmd ID 0x1; TLV len=28 tag=0x4a; ABI 0x01000000/53/"QCA_"/"ML"; 0; 0; n=0; TLV len=176 tag=0x4b; rc[0]=4 rc[1]=33 rc[7]=3 rc[8]=3 (minimal, rest 0). Every field matches ios-redfin wmi_send_init. The WMI_INIT byte stream is CORRECT, yet the FW crashes on receipt.
- Conclusion: the WMI_INIT content is verified correct at the byte level. The crash is NOT a message-format problem. It is either (a) a CE3 transport problem at a level not yet identified (the FW reads the descriptor/buffer via DMA and faults — e.g. the SMMU CB5 bypass window does not cover the CE3 ring/buffer PA, or the CE3 SR ring PA is wrong), or (b) the FW (HL.3.2.1.c2) is in a state where it cannot accept WMI_INIT at all (e.g. it needs a prior QMI/WMI step picasso skips, or it expects WMI_INIT on a different CE for this build).
- Ruled out so far: timing, chain mask, TLV order, host memory, buffer overlap, descriptor flags, rc_init content, WMI_INIT byte format.
- Next: (a) verify the CE3 SR ring PA and buffer PA are inside the SMMU CB5 bypass window (log s_ce_mem_pa and the programmed CE3 SR_BASE before arming); (b) check for a SERVICE_AVAILABLE event (0x22f) the FW may send before SERVICE_READY; (c) try the ath10k WMI_INIT construction verbatim (ath10k wmi-tlv.c) instead of ios-redfin's, in case redfin's FW and picasso's FW differ in a subtle INIT requirement.

### 3.59 ring80 TESTED (2026-11-07 10:05-10:07, blanket approval): CE program log — CE3 addresses are CORRECT, FW still crashes
- Driver md5 (CE log), kernel 15672080 B built 10:05. Added a LOG of every CE's programmed ring_pa/buf_pa/nent/nbytes/dir in program_rings.
- Boot: CE program log (lines 952-960): CE1 dir=1 ring 9b901000 buf 9b910000 nent=16; CE2 dir=1 ring 9b902000 buf 9b918000 nent=16; CE3 dir=2 ring 9b903000 buf 9b920000 nent=16 nbytes=2048; CE4 dir=2 ring 9b904000 nent=256; CE5 dir=2 ring 9b905000 nent=1024; CE6 dir=3 nent=32; CE8/9 dir=1 nent=32. Pool base 0x9b900000. CE3's SR ring (0x9b903000) and buffer (0x9b920000) are inside the pool and the SMMU CB5 bypass window; dir=2 (PD_OUT) is correct for WMI TX. WMI_INIT (228B, hex-verified) dispatched to CE3 [slot 0] — phone PANICKED immediately after, back in fastboot.
- CE3-ADDRESS HYPOTHESIS RULED OUT: the ring/buffer PAs are correct. NOTE: CE5 nent=1024 means its ring (1024x16=16KB) overflows its 4KB region and overlaps CE6/CE7 rings — a latent bug in the wlfw_ce_tbl-driven programming (CE4 nent=256, CE5 nent=1024 are not capped by the ring76 fix, which only capped CE0..CE3), but CE4/CE5 are not used for WMI so this is not the WMI_INIT crash cause; fix separately.
- Ruled out so far: timing, chain mask, TLV order, host memory, buffer overlap, descriptor flags, rc_init content, WMI_INIT byte format, CE3 ring/buffer addresses.
- Remaining: (a) BDF — the generic bdwlan.bin was accepted by WLAN_CFG but may be wrong for radio init; WMI_INIT triggers the FW to initialise the radio using the BDF, and a wrong BDF could crash it. The BDF variants tried earlier (g7a/g7ac/j9/j9a/bXX) all refused WLAN_CFG identically, so the accepted generic BDF may simply be the least-wrong one, not the right one. The correct BDF likely comes from the stock picasso firmware (MIUI ROM); (b) the modem may be in a corrupted state from ~12 consecutive crashes — a full power-off (not just reboot-to-fastboot) may be needed to reset the modem, since the battery keeps it powered; (c) the FW may need a QMI/WMI step before WMI_INIT that picasso skips.
- Next: (a) ask the user for a full power-off + power-on of the phone (hold power until off, then Volume Down + Power into fastboot) to reset the modem, then re-test WMI_INIT; (b) investigate extracting the correct BDF from the stock picasso firmware.
