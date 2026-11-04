# Ghi Chú Kỹ Thuật & Nhật Ký Debug HakiOS Picasso (`note.md`)

Cập nhật lần cuối: `2026-11-04`
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

Qua đối chiếu nhật ký thực tế (`/home/tritm/gpu_gui.log` lúc 15:28 và `/home/tritm/gui_springboard.log` lúc 16:53) cùng kết quả dịch ngược `dyld_shared_cache_arm64` (`20F66` — `IOMobileFramebuffer`, `QuartzCore`, `CoreBrightness`):

### 2.1. Nguyên nhân trực tiếp khiến `backboardd` dừng ở `/home/tritm/gpu_gui.log` (15:28)
Trong `/home/tritm/gpu_gui.log`:
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
- Trong `/home/tritm/gpu_gui.log`, `lsd[25]` gây bão log console 701 lần với thông báo:
  `lsd[25] corp: denied attempt to escape CPU monitor (missing required entitlement)`
- **Nguyên nhân:** `minisandbox_priv_grant()` trong `xnu/files/bsd/kern/kern_minimac.c` (dòng 325) chỉ mới cấp tự động `PRIV_WORK_INTERVAL`.
- **Cách khắc phục:** Cấp thêm `PRIV_PROC_CPUMON_OVERRIDE` (1016) tại `minisandbox_priv_grant()`:
  ```c
  if (priv == PRIV_WORK_INTERVAL || priv == PRIV_PROC_CPUMON_OVERRIDE) {
      return 0;
  }
  ```

---

## 3. Kế Hoạch Hành Động Ở Phiên Tiếp Theo (Next Session Checklist)

1. **Cập nhật Kernel (`MiniIOMobileFramebuffer.cpp` & `kern_minimac.c`) và xuất Patch Downstream:**
   - Thêm properties `"CanvasWidth"`, `"CanvasHeight"` và các selector `15`, `22`, `29`, `33`, `57` vào `MiniIOMobileFramebuffer.cpp`.
   - Thêm `PRIV_PROC_CPUMON_OVERRIDE` vào `minisandbox_priv_grant()` trong `kern_minimac.c`.
   - Chạy `xnu/export.sh` và cập nhật patch trong `hakios-picasso/patches/`.
2. **Làm gọn `userland/src/inject.c` và đóng gói lại `userland/root.apfs`:**
   - Bỏ vòng lặp in 34 dòng `IOService` và các lệnh mở thử `IOMobileFramebufferOpen` dư thừa trong `setup_primary_display_descriptor()`.
   - Đảm bảo `dlopen` `CoreBrightness.framework` trong `brightness_fixes()` nếu `BLControl` chưa nạp, và in log xác nhận khi đã cài hook `-[BLControl waitForALSArrival]`.
   - Chạy `cd /home/tritm/hakios-u/userland && ./build.sh GL=1` để cập nhật `userland/root.apfs`.
3. **Boot Test GUI cô lập (`boot_gpu_gui.py`):**
   - Dùng cmdline cô lập (tắt `wifi=0 modem=0 adsp=0 sound=0 mic=0`, bật `adreno=22 gpu=1 adreno_keep=1 ca_vbl=1 buttons=1`).
   - Kiểm tra `/home/tritm/gpu_gui.log` xác nhận `backboardd` vượt qua `[Backlight] instantiating BrightnessSystem`, khởi chạy `SpringBoard`, và thực hiện `SwapBegin` / `SwapEnd` (`MiniIOMobileFramebuffer: swap #1`).
4. **Sửa lỗi `tlmm.size` trong `drivers/audio/hakios.sound.c`:**
   - Khi `is_picasso = true`, đặt `tlmm.phys = 0x0f500000; tlmm.size = 0x5d000;` (đúng kích thước vùng nhớ South TLMM của SM7250 theo DeviceTree) để tránh lỗi `_io_map` khi bật `sound=4`.
