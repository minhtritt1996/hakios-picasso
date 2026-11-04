# QUY TRÌNH PORT THIẾT BỊ, KIẾN TRÚC PHÂN TẦNG & QUẢN LÝ REPOSITORY HAKIOS (`AGENT.md`)

Tài liệu này quy định chuẩn mực kiến trúc, quy trình port thiết bị mới theo đúng chuẩn của **HakiOS** (`docs/porting/`), quy tắc phân tách Upstream / Downstream, và trạng thái kỹ thuật của dự án **Xiaomi Redmi K30 5G (`picasso` / `sm7250`)**.
**Mọi Agent phải đọc kỹ tài liệu này và `note.md` trước khi thực hiện bất kỳ thay đổi nào.**

---

## 1. Kiến Trúc Phân Tầng Chuẩn HakiOS (`docs/porting/repositories.md`)

HakiOS được tổ chức thành các tầng độc lập, tương tự mô hình AOSP (`common`, `<soc>-common`, `device/<vendor>/<name>`):

```text
hakios-u/                            # Tầng Common (Upstream: oosexgang/hakios-xnu-kernel)
├── xnu/                             # Bản vá XNU chuẩn, xnu/files/, apply.sh, export.sh, sync-drivers.sh
├── drivers/                         # Common drivers dùng chung (guarded bởi PE_parse_boot_argn)
├── userland/                        # Darwin 16.5 init, libhakios.dylib (inject.c, eagl.c), Mesa/Turnip
├── xnuloader/                       # UEFI OS Loader (loader.efi, mkpayload.py)
├── tools/                           # Công cụ cấu hình & kiểm định (hakios-device.py, hakios-config.py, ...)
├── devices/manifest.conf            # Danh mục các SoC và Device repositories
└── devices/picasso/                 # Symlink hoặc clone của repo thiết bị (minhtritt1996/hakios-picasso)
    ├── device.conf                  # Định nghĩa thiết bị & ma trận trạng thái từng phần cứng (works/partial/missing)
    ├── soc.args                     # Các địa chỉ MMIO đặc thù của SoC trích xuất từ FDT của máy
    ├── pull-firmware.sh             # Script kéo firmware từ phân vùng vendor của chính thiết bị
    ├── configs/
    │   ├── picasso_defconfig        # Gồm 3 dòng #include: common_defconfig, sm7250.config, picasso.config
    │   └── picasso.config           # Cấu hình riêng của Picasso (CONFIG_DEVICE="picasso", bật/tắt drivers & boot-args)
    ├── soc/
    │   ├── soc.conf                 # Khai báo họ SoC Qualcomm SM7250 (Snapdragon 765G, 8 nhân, Adreno 620)
    │   └── configs/sm7250.config    # Cấu hình cấp SoC (CONFIG_SOC="sm7250", CPUFREQ, THERMAL, SOCINFO...)
    ├── drivers/                     # Driver đặc thù riêng của thiết bị (devices/picasso/drivers/<subsystem>/conf)
    │   ├── bluetooth/               # CONFIG_HAKIOS_PICASSO_BT (/dev/hakios-bt)
    │   ├── input/                   # CONFIG_HAKIOS_NOVATEK (Novatek NT36672C SPI touch)
    │   └── wifi/                    # CONFIG_HAKIOS_PICASSO_WLAN (WCN3990 MPSS PAS)
    ├── uefi/                        # Cấu hình Mu-Silicium UEFI cho Picasso
    ├── patches/                     # Downstream patch series cho phần common/XNU khi chưa merge upstream
    │   ├── UPSTREAM                 # Commit hash của upstream làm base
    │   ├── series                   # Thứ tự áp dụng các file .patch
    │   └── *.patch                  # Các bản vá đánh số thứ tự rõ ràng
    └── note.md                      # Nhật ký kỹ thuật, kết quả debug và kế hoạch phiên tiếp theo
```

### Quy tắc sở hữu mã nguồn (Ownership Rules):
1. **Không sửa bẩn Upstream Common:** Không tự ý commit code riêng của `picasso` vào nhánh `main` của `hakios-xnu-kernel`. Mọi phát triển tích hợp tại `/home/tritm/hakios-u` nằm trên nhánh `picasso-main`, và mọi tài sản thiết bị phải được đồng bộ về `/home/tritm/hakios-picasso`.
2. **Driver riêng của thiết bị (`devices/picasso/drivers/<subsystem>/conf`):**
   - Khai báo qua symbol `CONFIG_HAKIOS_<NAME>`.
   - Khi chạy `make kernel`, `xnu/sync-drivers.sh` (qua `tools/hakios-config.py external-files`) tự động chép file vào cây XNU dưới dạng `optional`.
   - Khi chạy `xnu/export.sh`, các file driver của thiết bị được tự động loại trừ khỏi `xnu/hakios.patch` chung.
3. **Không phát minh lại cơ chế đã có:** Nếu một thông số phần cứng là địa chỉ SoC đã được hỗ trợ qua `soc_*` boot-args, phải đặt trong `soc.args` hoặc `configs/picasso.config` thay vì hardcode vào mã nguồn common.

---

## 2. Quy Trình 5 Bước Port & Bringup Thiết Bị Mới Theo Chuẩn HakiOS (`docs/porting/index.md`)

### Bước 1: Khởi tạo & Cấu hình Repository Thiết bị từ FDT
1. Trích xuất DeviceTree thực tế từ Android của máy (`adb pull /sys/firmware/fdt`).
2. Tạo hoặc đồng bộ cấu trúc thiết bị:
   ```bash
   python3 tools/hakios-device.py check picasso
   make picasso_defconfig
   make config
   ```
3. Đối chiếu địa chỉ thanh ghi SoC (`soc_ipa_base`, `soc_gsi_base`, `soc_ipcc`, `soc_aoss`, `soc_rsc`, `soc_apps_smmu`, `soc_smem`, `soc_imem`, `soc_cmd_db`) trong `soc.args`.
4. Trong `configs/picasso.config`, **tắt toàn bộ driver đặc thù của điện thoại khác** (`CONFIG_HAKIOS_GOODIX=n`, `CONFIG_HAKIOS_FOCALTECH=n`, ...) để tránh ghi nhầm vào GPIO/rail của máy.

### Bước 2: Boot Cơ Sở Tối Giản Từ RAM (Zero-Hardware First Boot)
- **Nguyên tắc bất biến (RAM-Boot Only):** Tuyệt đối **KHÔNG BAO GIỜ** flash hay ghi đè vào phân vùng UFS vật lý của máy. Chỉ nạp và chạy trên RAM qua `xnuloader` Fastboot:
  ```bash
  fastboot oem ramdisk
  fastboot stage userland/system.apfs
  fastboot stage /tmp/payload.bin
  fastboot oem run
  ```
- Đảm bảo kernel cơ sở (với `wifi=0 modem=0 adsp=0 sound=0 bt=0`) boot thẳng vào `launchd`, vượt qua 36/36 bài kiểm tra userland Darwin và hiển thị framebuffer UEFI.

### Bước 3: Bringup Từng Subsystem Theo Đúng Thứ Tự Phụ Thuộc
Khi bật hoặc gỡ lỗi một subsystem, **luôn cô lập bằng boot-args** (tắt các driver không liên quan) và tuân thủ thứ tự chuẩn của HakiOS:

| Thứ tự | Subsystem / Driver | Boot-arg kiểm thử | Tiêu chí hoàn thành (Done Criteria) |
|---|---|---|---|
| **1** | **SoC Core:** `CPUFREQ`, `THERMAL`, `SOCINFO` | `cpus=8 small_cores_after=0` | Đọc đúng thông tin SoC, nhiệt độ và tần số CPU, không chạm phần cứng ngoại vi. |
| **2** | **Input:** `BUTTONS`, Touch (`NOVATEK`) | `buttons=1 touchscreen=1` | Phím cứng và cảm ứng gửi sự kiện qua `/dev/hakios-touch` tới `backboardd` `IOHIDEventSystem`. |
| **3** | **Storage:** `UFS` | `ufs=2 ufs_native=1` | Đọc bảng phân vùng UFS 2.1 (read-only), phục vụ nạp firmware cho modem/DSP. |
| **4** | **GPU & Display GUI:** `ADRENO` + `MiniIOMobileFramebuffer` | `adreno=22 gpu=1 adreno_keep=1 ca_vbl=1` | GMU + Zap shader (`a620_zap.mdt`) lên nguồn; Step 22 vẽ 3 dải màu; `backboardd` khởi tạo GLES 3.2 (`FD620`) và `SpringBoard` swap frame (`SwapBegin`/`SwapEnd`) lên màn hình. |
| **5** | **Audio & Battery:** `ADSP`, `SPEAKER`, `SOUND`, `MIC`, `BATTERY` | `adsp=1 sound=4 mic=4` | ADSP PAS 1 lên nguồn, GLINK/QRTR/APR thông suốt, phát âm thanh qua TFA9874 & thu âm qua WCD938x. |
| **6** | **Wi-Fi / Network:** `PICASSO_WLAN` (`QMI`) | `wifi=1` | MPSS PAS 4 lên nguồn, WLFW QMI attach, quét beacon 802.11, WPA2-PSK và nhận IP DHCP trên `en0`. |
| **7** | **Peripherals khác:** `PICASSO_BT`, `HAPTIC`, `CAMERA`, `TORCH`, `MODEM` | `bt=1`, ... | Khởi tạo từng thiết bị `/dev/hakios-*` và kiểm chứng bằng công cụ test userland. |

### Bước 4: Quy Trình Phát Triển Kernel / XNU & Quản Lý Downstream Patch
Khi cần sửa mã nguồn thuộc XNU (`xnu/files/` hoặc `xnu-src/`):
1. Chỉnh sửa trong `xnu/files/...` và đồng bộ sang `xnu-src/xnu-8796.121.2/...` (hoặc sửa trong `xnu-src/` rồi chạy `./xnu/export.sh`).
2. Biên dịch lại kernel và kiểm thử thực tế trên thiết bị qua RAM boot.
3. Xuất bản vá downstream định danh rõ ràng vào `/home/tritm/hakios-picasso/patches/` và cập nhật `/home/tritm/hakios-picasso/patches/series` cùng `/home/tritm/hakios-picasso/patches/UPSTREAM`.

### Bước 5: Kiểm Định Chất Lượng (CI Checks) & Ghi Chép (`device.conf` + `note.md`)
Trước khi commit và push:
1. Chạy bộ công cụ kiểm định chuẩn HakiOS tại `/home/tritm/hakios-u`:
   ```bash
   python3 tools/hakios-device.py check picasso
   python3 tools/hakios-config.py check
   python3 tools/drivers-check.py
   python3 tools/boot-args.py --check
   python3 tools/provenance-check.py
   ```
2. Cập nhật trạng thái trung thực trong `devices/picasso/device.conf` (`works` chỉ dành cho tính năng đã xác nhận chạy thật trên phần cứng; `partial` cho tính năng đang hoàn thiện kèm ghi chú rõ ràng; `missing` cho phần chưa làm).
3. Cập nhật nhật ký kỹ thuật vào `note.md` để phiên làm việc sau nắm bắt ngay ngữ cảnh mà không phải dò lại từ đầu.

---

## 3. Các Kho Mã Nguồn Tham Khảo Trọng Yếu

1. **[oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel):** Upstream HakiOS XNU Kernel, tài liệu kiến trúc (`docs/porting/`), chuẩn driver và CI tools.
2. **[minhtritt1996/hakios-picasso](https://github.com/minhtritt1996/hakios-picasso):** Repository thiết bị chính thức của Xiaomi Redmi K30 5G (`picasso`).
3. **[YinCQL/UniXNU-picasso](https://github.com/YinCQL/UniXNU-picasso):** Tham khảo cấu hình DeviceTree, MMIO và framebuffer của Redmi K30 5G.
4. **[silenzio111/UniXNU-picasso](https://github.com/silenzio111/UniXNU-picasso/commits/stage12/picasso-cpufreq-big/):** Tham khảo mở khóa bảo mật Adreno 620 (`a620_zap.mdt`, `CP_SET_SECURE_MODE`) và CPU frequency scaling.
5. **[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet):** Tài liệu dịch ngược `StartWindowServer`, `CAWindowServer`, `IOMobileFramebuffer` và `QuartzCore` trên iOS 16.5 (`20F66`).
