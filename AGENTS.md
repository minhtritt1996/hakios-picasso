# HakiOS Picasso (`picasso` / `sm7250`) Workspace Rules (`AGENTS.md`)

Tài liệu này định nghĩa các quy tắc bắt buộc (Always-On Rules) cho mọi AI Agent khi làm việc trong repository **Xiaomi Redmi K30 5G (`picasso` / `sm7250`)**.
Trước khi thực hiện bất kỳ thay đổi mã nguồn hoặc chạy thử nghiệm nào, Agent **bắt buộc** phải đọc [note.md](./note.md) để nắm trạng thái kỹ thuật mới nhất và kích hoạt skill `hakios-picasso-porting` (`.agents/skills/hakios-picasso-porting/SKILL.md`) khi thực hiện build, xuất patch, hoặc debug phần cứng.

---

## 1. Bốn Quy Tắc Bất Biến (Mandatory Invariants)

1. **RAM-Boot Only (An toàn phần cứng tuyệt đối):**
   - **TUYỆT ĐỐI KHÔNG BAO GIỜ** chạy các lệnh ghi đè lên bộ nhớ UFS vật lý của điện thoại (`fastboot flash`, `fastboot erase`, `dd of=/dev/sd*`, ...).
   - Chỉ được phép nạp và chạy trên RAM thông qua `fastboot boot` hoặc `xnuloader` (`fastboot oem ramdisk`, `fastboot stage`, `fastboot oem run`).
2. **Bảo toàn tuyệt đối thư mục `upstream/` (Read-Only Upstream):**
   - Thư mục `upstream/` chứa bản clone nguyên gốc của `hakios-xnu-kernel`, `hakios-uefi`, `hakios-metal`, `xnu-8796.121.2`, `hfs-627.100.6`, và `Mu-Silicium`.
   - **CẤM TUYỆT ĐỐI** chỉnh sửa, thêm/xóa file hoặc tạo hardlink ghi đè vào bất kỳ file nào trong `upstream/`.
   - Mọi quá trình áp patch và biên dịch bắt buộc phải diễn ra trong không gian cô lập `build/` thông qua script [`./build.sh`](./build.sh).
3. **Phân tách Driver Thiết Bị (`drivers/`) và Bản Vá Upstream (`patches/`):**
   - **Driver riêng của thiết bị (`picasso` / `sm7250`):** Phải đặt trực tiếp trong [`drivers/<subsystem>/`](./drivers/) kèm file `conf` khai báo `CONFIG_HAKIOS_<NAME>`. Không nhét driver riêng của máy vào file patch chung của XNU.
   - **Sửa đổi mã nguồn thuộc Upstream (`xnu`, common `drivers`, `userland`, `xnuloader`, `hakios-metal`):** Thử nghiệm trong `build/`, sau khi xác nhận hoạt động trên thiết bị thì xuất thành file patch chuẩn trong [`patches/series`](./patches/series) (`0001`..`0006`) bằng lệnh `./build.sh export-patches`.
   - Mục tiêu: Đảm bảo repository có thể nhận bản cập nhật mới từ upstream bất kỳ lúc nào chỉ bằng cách cập nhật `upstream/` + [`patches/UPSTREAM`](./patches/UPSTREAM) và chạy `./build.sh all --clean`.
4. **Không sử dụng đường dẫn tuyệt đối (No Hardcoded Absolute Paths):**
   - Trong toàn bộ script (`*.sh`, `*.py`), cấu hình, và tài liệu (`*.md`), không được hardcode đường dẫn máy cá nhân (như `/home/...`). Luôn suy dẫn tương đối từ thư mục gốc của repository.

---

## 2. Cấu Trúc Phân Tầng Chuẩn HakiOS (`hakios-picasso`)

```text
hakios-picasso/
├── AGENTS.md                        # Quy tắc bắt buộc tự động nạp bởi Antigravity (Always-On Rules)
├── AGENT.md                         # Symlink tới AGENTS.md để tương thích ngược
├── .agents/
│   └── skills/
│       └── hakios-picasso-porting/
│           └── SKILL.md             # Quy trình chi tiết build, xuất patch, CI check & debug phần cứng
├── device.conf                      # Định nghĩa thiết bị & ma trận trạng thái phần cứng (works/partial/missing)
├── soc.args                         # Các địa chỉ MMIO đặc thù của SoC SM7250 trích xuất từ FDT
├── configs/
│   ├── picasso_defconfig            # Gồm 3 dòng #include: common_defconfig, sm7250.config, picasso.config
│   └── picasso.config               # Cấu hình riêng của Picasso (bật/tắt drivers & boot-args)
├── soc/
│   ├── soc.conf                     # Khai báo họ SoC Qualcomm SM7250 (Snapdragon 765G, 8 nhân, Adreno 620)
│   └── configs/sm7250.config        # Cấu hình cấp SoC (CONFIG_SOC="sm7250", CPUFREQ, THERMAL, SOCINFO...)
├── drivers/                         # Driver đặc thù riêng của thiết bị (bluetooth, input/novatek, wifi)
├── uefi/                            # Cấu hình Mu-Silicium UEFI cho Picasso (apply.sh, export.sh, files/)
├── prebuilt/                        # BootShim.bin, device-tree.dtb, mkbootimg.py, ramdisk
├── tools/                           # Công cụ đóng gói & kiểm thử RAM-boot (build-bootimg.py, boot_gui_full.py, ...)
├── build.sh                         # Script trung tâm: build [all|kernel|userland|uefi], export-patches, check
├── patches/                         # Bộ 6 bản vá downstream (0001..0006) trên nền commit trong UPSTREAM
├── upstream/                        # (gitignored) Các repo upstream nguyên gốc (STRICTLY READ-ONLY)
├── build/                           # (gitignored) Không gian biên dịch cô lập & tạo tác đầu ra tại build/out/
└── note.md                          # Nhật ký kỹ thuật, kết quả debug thực tế và kế hoạch phiên tiếp theo
```

---

## 3. Lệnh Thao Tác Nhanh (`./build.sh`)

- **Build toàn bộ từ `upstream/` + `patches/` ra `build/out/`:**
  ```bash
  ./build.sh all --clean
  ```
- **Build từng thành phần trong quá trình phát triển:**
  ```bash
  ./build.sh kernel
  ./build.sh userland
  ./build.sh uefi
  ```
- **Xuất các thay đổi từ `build/` về lại bộ 6 patch `patches/0001..0006` và kiểm chứng với `upstream/`:**
  ```bash
  ./build.sh export-patches
  ```
- **Chạy toàn bộ kiểm định CI chuẩn HakiOS & kiểm tra tính toàn vẹn của `upstream/`:**
  ```bash
  ./build.sh check
  ```
