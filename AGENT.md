
# QUY TẮC UPSTREAM, DOWNSTREAM VÀ PATCH MANAGEMENT

## 1. Nguyên tắc bảo vệ HakiOS upstream

Repository upstream:
https://github.com/oosexgang/hakios-xnu-kernel

Đây là nền tảng chung của HakiOS.

Agent KHÔNG ĐƯỢC tự ý đưa các thay đổi riêng của Picasso
vào repository upstream hoặc commit chúng vào common branch.

Mọi thay đổi dành riêng cho Picasso phải được quản lý
trong repository hakios-picasso.

Không sử dụng upstream repository như nơi lưu trữ
các sửa đổi thử nghiệm riêng của thiết bị.

Upstream chỉ được thay đổi khi:
- Người dùng yêu cầu đóng góp upstream.
- Thay đổi thực sự phù hợp với kiến trúc common.
- Đã tuân thủ quy trình CONTRIBUTING.md.
- Đã được kiểm tra và chuẩn bị cho Pull Request.

## 2. Tuân thủ kiến trúc phân tầng HakiOS

Bắt buộc đọc:

- CONTRIBUTING.md
- docs/concepts/architecture.md
- docs/porting/repositories.md
- docs/porting/index.md
- docs/porting/drivers.md
- docs/configs.md
- docs/drivers-config.md
- docs/boot-args.md
- docs/getting-started/building.md

Quy tắc sở hữu mã nguồn:

- Common: hakios-xnu-kernel.
- Device: devices/picasso.
- SoC: devices/picasso/soc.
- Device-specific drivers: devices/picasso/drivers.
- SoC-specific drivers: devices/picasso/soc/drivers.

Không tạo patch cho những chức năng đã có cơ chế
extension chính thức trong HakiOS.

## 3. Downstream Patch Series

Mọi thay đổi cần thiết phải tác động vào source chung
nhưng chỉ phục vụ Picasso phải được đóng gói thành
patch trong:

hakios-picasso/patches/

Không sửa trực tiếp source trong upstream checkout
đang được người dùng sử dụng.

Patch series phải có:

- patches/UPSTREAM
- patches/series
- Các file patch được đánh số.
- Thứ tự áp dụng xác định.
- Mô tả chức năng.
- Phạm vi ảnh hưởng.
- Kết quả kiểm thử.
- Trạng thái tương thích với upstream.

Không tạo patch không có mục đích hoặc không có
nguồn gốc rõ ràng.

## 4. Quy trình phát triển patch

Khi thay đổi mã nguồn XNU:

1. Tạo HakiOS workspace riêng.
2. Kiểm tra upstream commit.
3. Apply patch series hiện tại.
4. Chạy xnu/apply.sh để tạo XNU working tree.
5. Chỉnh sửa XNU trong xnu-src/.
6. Chạy xnu/export.sh theo quy trình HakiOS.
7. Kiểm tra thay đổi được export.
8. Tạo downstream patch mới.
9. Lưu patch trong hakios-picasso/patches/.
10. Cập nhật patches/series.
11. Kiểm thử lại từ workspace sạch.

Không sửa trực tiếp generated XNU source mà không
thực hiện quy trình export phù hợp.

Driver device-specific phải được đặt trong repository
thiết bị, không đóng gói thành common driver patch.

## 5. Quy trình Apply Patch

Patch phải được áp dụng tại thư mục gốc của HakiOS
build workspace.

Không áp dụng toàn bộ series trực tiếp vào xnu-src/.

Trình tự:

1. Xác nhận đúng upstream base commit.
2. Tạo workspace sạch.
3. Chuẩn bị repository Picasso.
4. Kiểm tra patches/series.
5. Lần lượt chạy git apply --check cho từng patch.
6. Nếu hợp lệ, áp dụng patch đó.
7. Tiếp tục patch tiếp theo.
8. Dừng ngay nếu một patch thất bại.
9. Không tự ý bỏ qua patch lỗi.
10. Không sửa upstream checkout để xử lý lỗi apply.

Patch series phải được áp dụng trước khi chạy
quy trình build kernel chính thức.

## 6. Quy trình đồng bộ upstream

Khi upstream có commit mới:

1. Không thay đổi upstream checkout của người dùng.
2. Tạo workspace mới tại commit được chọn.
3. Kiểm tra tương thích của từng patch.
4. Sửa patch có conflict.
5. Không tự động loại bỏ thay đổi.
6. Build lại với Picasso configuration.
7. Kiểm tra device repository.
8. Kiểm tra các HakiOS CI tools.
9. Cập nhật patches/UPSTREAM.
10. Ghi lại kết quả đồng bộ.

Không được tuyên bố đã đồng bộ thành công
nếu patch series chưa được kiểm tra.

## 7. Điều kiện tạo Pull Request upstream

Trước khi đề xuất merge:

- Phân biệt code common và code Picasso.
- Không đưa firmware độc quyền vào Git.
- Không đưa Apple userland binary vào Git.
- Tuân thủ APSL-2.0 và giấy phép bên thứ ba.
- Không phá vỡ Redwood/QEMU.
- Chạy các HakiOS quality checks.
- Có log và kết quả thực tế.
- Mô tả rõ thay đổi và lý do.

Sau khi thay đổi đã được upstream chấp nhận,
phải loại bỏ patch tương ứng khỏi downstream series
ở lần cập nhật tiếp theo.

## 8. Quy tắc bất biến

Không sửa upstream checkout tùy tiện.
Không phát triển trực tiếp trên common branch.
Không để patch thất bại bị bỏ qua.
Không áp dụng patch không rõ base commit.
Không để patch series mất thứ tự.
Không đưa device-specific code vào common.
Không để downstream patch che giấu thay đổi
trong upstream source.
