# Câu hỏi cho upstream HakiOS (picasso / SM7250)

**Bối cảnh**: port mới `picasso` (Redmi K30 5G, SM7250). Mu-Silicium picasso (YinCQL fork) + `hakios-uefi`
(platform_picasso thêm trong `xnuloader/loader.c`), identity thật từ `DeviceTree.d22ap.im4p`, kernel tự build
`make picasso_defconfig && make kernel` (chỉ driver chung + socinfo/cpufreq/thermal/ufs).

**Đã chạy được**: loader `platform picasso`, display 1080x2400 @ a0000000, USB, UFS survey (109 partitions),
kernel in log tới `vm_page_bootstrap`.

**Điểm dừng** (đo bằng marker ghi vào ring log loader, một lần chạy):

```
CP92 CP100 CP101 CP109 CP112 CP113 CP114 CP115 CP116
```

tức `kernel_bootstrap()` → `vm_mem_bootstrap()` → `vm_map_init()` xong (CP109) → `kmem_init()` →
`vm_map_create_options()` → **`zalloc_id(ZONE_ID_VM_MAP, Z_WAITOK|Z_NOFAIL|Z_ZERO)` không trả về** (CP117 không tới).

**Số liệu ngay trước đó** (dump `loader_cp_hex`):

```
printf: "VM boostrap: 0 maps, 0 entries and 0 holes available"
H1=0x4000 map_data_size   H2=0x4000 kentry_data_size   H3=0x4000 map_holes_data_size
H4=fffffff140974000 map_data   H5=fffffff14097c000 map_holes_data
H6/H7/H8 = 0   zone_count_free(vm_map_zone / entry / holes) SAU zone_cram_early
```

**Câu hỏi**:
1. Trên redwood, dòng `VM boostrap: ...` in ra số nào? Nếu khác 0 thì zone early của chúng tôi không được
   cram đúng; nếu cũng 0 thì điểm dừng thật sự nằm trong `zalloc_id`.
2. Điều gì khiến `zalloc_id(Z_WAITOK)` chặn ở giai đoạn này trên một SoC mới (grow zone cần `kernel_map`
   đang được tạo)? Có cần thêm bước/SoC hook nào cho SM7250 (ví dụ khác biệt PMAP/`pmap_virtual_region`)?
3. Nếu có thể, cho biết log kernel đầy đủ của một lần boot redwood ngay sau `VM boostrap` để so sánh thứ tự.

Chi tiết tái lập: `HANDOFF.md` mục "2026-10-31 10:05"; nhánh `hakios` của repo này; fork
`minhtritt1996/hakios-xnu-kernel` nhánh `picasso` (platform + build fix).

## Số liệu bổ sung (zalloc / zcram) — 2026-10-31 10:40

Chuỗi mốc đầy đủ một lần chạy:

```
CP92 100 101 109 112 113 114 115 116
CP120 CP121 CP122                       (zalloc_ext -> zalloc_item)
H20=0                                   z_elems_free của ZONE_ID_VM_MAP = 0
CP124 CP126                             (zone_expand_locked, đã chiếm expander)
                                        ... CP127 KHÔNG tới: chết trong zone_allocate_va_locked
```

Và ngay trong `zcram()` (gọi từ `vm_map_init`):

```
CP130 H30(pages)=1  H31(chunk_pages)=1  H32(zone_elem_outer_size)=0
      H34(z_elem_size)=0  H35(z_elems_avail)=0  H36(z_chunk_elems)=0x66  H37(flags)=0
CP131 H33(z_elems_free sau chunk) = 0
```

Ngay sau `zone_create_ext(VM_MAP_ZONE_NAME, sizeof(struct _vm_map), ZFLAGS, ZONE_ID_VM_MAP, NULL)`:

```
H40(z_elem_size)=0  H41(z_elems_avail)=0  H42(zone_elem_outer_size)=0
```

⇒ Zone `ZONE_ID_VM_MAP` **chưa có `z_elem_size`** tại thời điểm `vm_map_init()` cram nó; `zcram_and_lock()`
tính `elem_size = zone_elem_outer_size(zone) = 0` rồi chia cho nó ⇒ nghi vấn fault im lặng (không có console).
Sau đó `zalloc_id()` thấy `z_elems_free == 0` nên đi nhánh blocking `zone_expand_locked()` — mà mở rộng cần
`kernel_map`, chính là map đang được tạo trong `vm_map_create_options()` ⇒ treo.

Câu hỏi 4: trên redwood, `zone_array[ZONE_ID_VM_MAP].z_elem_size` có khác 0 ngay sau `zone_create_ext()`
không? Có bước nào (ví dụ `zone_metadata_init()` / `startup_phase >= STARTUP_SUB_ZALLOC`) mà ở một SoC mới
cần chạy trước `vm_map_init()` không?

## Số liệu cuối (2026-10-31 11:05) — điểm bất thường rõ ràng

Tại `vm_map_init()`, `startup_phase = 5` (`STARTUP_SUB_PMAP_STEAL`, trước `STARTUP_SUB_KMEM=6`/`STARTUP_SUB_ZALLOC=7`)
— đúng như mong đợi cho lần gọi này.

Trong `zone_create_ext()` cho các zone VM (zid 0x1d, 0x0f, 0x10, …):

```
H54 (size sau zone_elem_adjust_size) = 0x100 / 0xa0 / …
H55 (redzone)                        = 0
H52 (z->z_elem_size ngay sau dòng gán) = 0        ← phải là 0x100 / 0xa0
```

Tức **lệnh `z->z_elem_size = (uint16_t)(size - redzone);` không để lại giá trị đọc được ở cùng đối tượng**,
dù `size=256`, `redzone=0`. Kéo theo `zone_elem_outer_size()=0` ⇒ `zcram_and_lock()` chia 0 (fault im lặng,
không có console) ⇒ zone rỗng ⇒ `zalloc_id(Z_WAITOK)` đi `zone_expand_locked()` ⇒ treo khi cần `kernel_map`
đang được tạo.

Giả thuyết cần kiểm chứng phía upstream:
1. `struct zone` có biến thể theo cấu hình (dẫn tới lệch layout giữa `zalloc.c` và người đọc `vm_map_zone`), hoặc
2. có bước nào (ví dụ `zone_init()`/`zone_metadata_init()` ở `STARTUP_SUB_ZALLOC`) phải chạy *trước* để
   `zone_array[]` có layout/kích thước thật, và trên một SoC mới thứ tự này bị lệch.

## 2026-10-31 11:05 — bất thường của compiler + workaround

Disassembly `zone_create_ext` (nhánh cấp phát, offset 0xa484–0xa4e8 của object):
```
a484: udiv  x8, x8, x20        (chunk_elems = alloc / size)
a488: strh  w8, [x23, #58]     (z_chunk_elems)
a48c: ldrh  w1, [x23, #52]     (dump cua ta)
a4a4: ldrh  w8, [x23, #52]     ← đọc volatile của fix-up
a4a8: tbz   w19, #5, ...       ← KHÔNG có store #52, KHÔNG có nhánh theo w8
```
⇒ clang **giữ lệnh đọc volatile nhưng bỏ lệnh ghi volatile** trong nhánh này (chỉ còn store ở nhánh
`ZC_OBJ_CACHE`: `strh w20, [x23, #52]`). Vì vậy `z->z_elem_size = size - redzone` không bao giờ tới bộ nhớ.

Workaround (`patches/0011`): gọi hàm ở TU khác — `hakios_zone_force_elem_size(void *zone, uint32_t size)`
trong `osfmk/vm/vm_kern.c` (ghi thẳng offset 52) — nên compiler buộc phải giữ; kernel đã có
`bl _hakios_zone_force_elem_size` ở 2 điểm.

Câu hỏi thêm: đây có phải bug clang/TU này đã biết (ví dụ do `__attribute__((musttail))` hoặc LTO/`-O2` +
`#define` nào đó trong `zalloc.c`) hay là dấu hiệu source trong repo không khớp với bản build prebuilt?

## 2026-10-31 11:50 — `bootcpu` trên picasso: core phụ KHỞI ĐỘNG nhưng kernel không in gì

Đo trên máy (loader log qua `oem log`):

```
core 100: firmware answers ... (CPU_ON thành công)
8..50 s: the kernel has logged 0 bytes, heartbeat 1; ...
THE KERNEL HUNG on core 100: no sign of life for 50 seconds
```

⇒ `other_cpu_stage` **được set** (loader thấy core đã tới `other_cpu_entry`) và kernel đã publish `kernel_live`
(heartbeat 1, timer chạy), nhưng **log 0 byte** — kernel dừng rất sớm trên core phụ.

Đối chứng: **cùng kernel, cùng loader, trong QEMU `-cpu max`, `QEMU_SMP=2`, `bootcpu=1`** chạy hoàn hảo:
```
the kernel will run on core 1 (this is core 0)
VM boostrap: 102 maps, 204 entries and 512 holes available
launchd.development … Process 1 exec of /sbin/launchd failed, errno 2
```
⇒ Cơ chế `other_cpu_entry` của loader đúng; khác biệt nằm ở **trạng thái core phụ do PSCI/picasso cấp**
(ví dụ EL vào, MMU/cache, clock của core) hoặc ở chỗ kernel cần thêm một bước cho core không phải core 0.

Vì kernel chỉ chạy được trên core 0 (nơi loader sống), loader bị chiếm core ⇒ mất USB, không ghi được log/UFS
trong lúc chạy (đây cũng là lý do `oem log`/`oem screenshot` không trả lời khi kernel đang chạy).

Câu hỏi 5: trên một SoC mới, ngoài `core_shift`/`small_cores`, loader còn cần biết gì để core phụ chạy được
kernel (ví dụ trạng thái EL mong đợi, `psci_hvc`, hay việc bật clock của core bằng `soc_*`)?
