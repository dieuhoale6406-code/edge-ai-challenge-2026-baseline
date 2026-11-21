# Thông số phần cứng ESP32-S3 + OV7670 cho thiết kế model

Tài liệu dành cho nhóm phần mềm và model, để biết trước giới hạn của board khi thiết kế model chạy trên thiết bị. Số liệu đo ngày 21/11/2026 trên board thật qua COM9. Thời gian và RAM lấy từ các dòng `SELFTEST` in ra lúc khởi động.

Model đang tích hợp: **YOLO KD student v2.1** (detector) và **TextCNN 40 token**. Số của model baseline cũ được giữ lại để so sánh.

Cột **Nguồn** cho biết độ tin cậy của từng số:
- **Đo**: đo trực tiếp trên board này.
- **Build**: lấy từ kết quả build ESP-IDF.
- **Tính**: suy ra từ kiến trúc model hoặc từ số đo.
- **Datasheet**: thông số nhà sản xuất.

## 1. Tóm tắt giới hạn quan trọng

| Giới hạn | Model v2.1 (đang dùng) | Baseline cũ | Nguồn | Ý nghĩa với model |
| --- | --- | --- | --- | --- |
| Khối lượng tính detection | **332 triệu MAC** | 153,7 triệu MAC | Tính | |
| Thời gian detect | **2942–2966 ms** | 1119–1221 ms | Đo | |
| Tốc độ tính thực tế (int8, TFLite Micro + ESP-NN) | khoảng **113 triệu MAC/s** | khoảng 125–135 triệu MAC/s | Tính | Thời gian (ms) ≈ số triệu MAC / 0,11–0,13. Muốn dưới 1 s thì giữ dưới khoảng 110 triệu MAC. |
| Thời gian intent | **5,1 ms** | 2,3–2,8 ms | Đo | Không đáng kể. |
| Tensor arena detection | cấp 3072 KB, dùng **2097 KB** | cấp 2048 KB, dùng 1502 KB | Đo | Model v2.1 sẽ tràn nếu giữ mức 2048 KB cũ. |
| PSRAM còn trống sau khởi tạo | **4491 KB** (khối liền lớn nhất 4480 KB) | 5419 KB | Đo | Arena có thể tăng thêm khoảng 3,9 MB, sau khi chừa 512 KB dự phòng. |
| SRAM nội còn trống | **92 KB** (khối lớn nhất 52 KB, thấp nhất 60 KB) | 116 KB | Đo | Không đặt arena hay buffer lớn ở đây. |
| Trọng số (mảng C trong flash) | detection 334.960 B, TextCNN 27.536 B | 42.208 B + 15.696 B | Build | Nằm trong flash, **không chiếm RAM**. |
| Firmware / phân vùng app | **1.514.368 B / 4.128.768 B** | 1.185.518 B / 1.536.000 B | Build | Còn khoảng 2,5 MB cho model lớn hơn. |
| Ảnh đầu vào detection | 320 × 240 × 3 int8, chuẩn hóa `pixel / 127.5 - 1` | `pixel / 255` | Code | |
| Câu hỏi đầu vào | 40 token, vocab 260 từ | 32 token, 261 từ | Code | |
| FPS camera | **12–14 fps** ở QVGA RGB565 | | Đo | Detect chậm hơn camera khoảng 40 lần. |

**Trọng số model không nằm trong RAM.** Sau khi chuyển `.tflite` thành mảng C, trọng số là hằng số và được đọc thẳng từ flash. Thứ chiếm RAM là **tensor arena**, tức vùng nhớ chứa activation giữa các lớp, đặt trong PSRAM.

## 2. Chip và CPU

| Thông số | Giá trị | Nguồn |
| --- | --- | --- |
| Chip | ESP32-S3 QFN56, revision v0.2 | Đo (esptool) |
| CPU | 2 nhân Xtensa LX7, **240 MHz** | Đo (esptool, boot log) |
| Thạch anh | 40 MHz | Đo (esptool) |
| Tính toán số thực | FPU single precision. Double chạy bằng phần mềm, rất chậm. | Datasheet |
| Lệnh vector | SIMD 128 bit (PIE) cho int8/int16, ESP-NN dùng để tăng tốc | Datasheet |
| SRAM nội | 512 KB | Datasheet |
| Wi-Fi | 2.4 GHz 802.11 b/g/n. Firmware phát AP `ESP32S3-INFER`, tối đa 4 thiết bị | Datasheet, code |

Hệ quả cho model:
- Chỉ dùng model **lượng tử hóa int8 toàn phần**. Lớp float32 không được ESP-NN tăng tốc nên chậm hơn nhiều lần.
- Mỗi lần inference chạy trên một nhân. Nhân còn lại phục vụ Wi-Fi và camera.

## 3. Bộ nhớ

### 3.1 PSRAM (bộ nhớ ngoài)

| Thông số | Giá trị | Nguồn |
| --- | --- | --- |
| Dung lượng | 8 MB (64 Mbit), AP Memory thế hệ 3 | Đo (boot log) |
| Giao tiếp | Octal, 80 MHz | Đo (boot log) |
| Kiểm tra ghi/đọc 2 MiB | PASS, 0 lỗi | Đo |

Phân bổ PSRAM với model v2.1:

| Vùng | Dung lượng | Nguồn |
| --- | --- | --- |
| Tensor arena detection (`DET_ARENA_KB`) | 3072 KB, dùng 2097 KB | Đo |
| Tensor arena TextCNN (`TXT_ARENA_KB`) | 32 KB, dùng 8 KB | Đo |
| Bộ đệm ảnh + câu hỏi cho `/run` | 230.913 B | Code |
| 2 bộ đệm khung camera | 2 × 168.976 B = 337.952 B | Đo (boot log) |
| **Còn trống** | **4491 KB** | Đo |

Nếu arena cấu hình không đủ, firmware tự thử lại với khối PSRAM lớn nhất còn trống (trừ `ARENA_RESERVE_KB` = 512 KB) và in ra số KB nên đặt. Model vì thế vẫn chạy được trong lúc chờ chỉnh cấu hình.

### 3.2 SRAM nội

| Thông số | Giá trị | Nguồn |
| --- | --- | --- |
| DIRAM tổng / đã dùng tĩnh (baseline) | 341.760 B / 139.318 B (40,8%) | Build |
| Còn trống khi chạy model v2.1 | 92 KB, khối lớn nhất 52 KB | Đo |
| Dành riêng cho DMA | 32 KiB | Đo (boot log) |
| Stack task HTTP chạy inference | 16 KB | Code |

### 3.3 Flash và kích thước model

| Thông số | Giá trị | Nguồn |
| --- | --- | --- |
| Flash vật lý | 16 MB quad | Đo (esptool) |
| Flash khai báo trong firmware | 4 MB, DIO 80 MHz, để chạy được trên board 4 MB | Build |
| Bảng phân vùng | `partitions.csv`: app `factory` 0x3F0000 = 4.128.768 B | Build |
| Firmware hiện tại | 1.514.368 B | Build |
| **Còn trống** | **2.614.400 B (khoảng 2,5 MB)** | Build |

Với bảng phân vùng cũ của baseline (1.536.000 B), firmware có model v2.1 chỉ còn dư khoảng 22 KB.

## 4. Camera OV7670

| Thông số | Giá trị | Nguồn |
| --- | --- | --- |
| Cảm biến | OV7670, PID 0x76, VER 0x73, không có FIFO | Đo |
| Độ phân giải tối đa | 640 × 480 (VGA) | Datasheet |
| Độ phân giải đang dùng | **320 × 240 (QVGA)** | Code |
| Định dạng | RGB565 big-endian, 153.600 B/khung | Đo |
| Sau khi đổi sang RGB888 | 230.400 B/khung | Tính |
| XCLK | 20 MHz (`CAM_XCLK_MHZ`) | Code |
| Thanh ghi CLKRC | 0x01 (chia 2) | Code |
| Chế độ DMA camera | Vào bộ đệm nội rồi chép sang PSRAM (tắt `CAMERA_PSRAM_DMA`), task camera trên core 0 | Code |
| Chất lượng JPEG stream | `CAM_JPEG_QUALITY=25`, tương đương 60% | Code |
| FPS | **12–14 fps** | Đo |
| Thời gian chờ khung | khoảng 3 ms (2 bộ đệm, lấy khung mới nhất) | Đo |
| Mã hóa JPEG một khung | khoảng 66 ms, là giới hạn FPS | Đo |
| FPS khi chưa sửa CLKRC | khoảng 3 fps (driver ghi sai, chia clock 34) | Đo |

Chất lượng ảnh cần tính đến khi train:
- Ảnh OV7670 bị ám màu, nhiễu, và chụp ở góc khác dataset. Nên thu thêm dữ liệu từ chính camera này, hoặc augment màu, nhiễu và độ sáng.
- Vật chuyển động nhanh bị nhòe khi thiếu sáng.
- Khung camera là RGB565 (5–6 bit mỗi kênh màu), nên chi tiết màu thấp hơn ảnh PNG trong dataset.

## 5. Runtime inference

| Thành phần | Phiên bản | Nguồn |
| --- | --- | --- |
| ESP-IDF | 6.1.1 | Build |
| esp-tflite-micro | 1.4.3 | `dependencies.lock` |
| esp-nn | 1.5.0 | `dependencies.lock` |
| esp32-camera | 2.1.9 | `dependencies.lock` |

### 5.1 Op được hỗ trợ

`REGISTER_OPS` (`main/model_config.h`) gồm: `Conv2D`, `DepthwiseConv2D`, `MaxPool2D`, `Pad`, `Add`, `Concatenation`, `Reshape`, `Less`, `Gather`, `FullyConnected`, `Softmax`.

Ngoài ra `main/infer.cc` tự viết kernel cho hai op mà TFLite Micro thiếu:
- `TILE`: detector v2.1 dùng 4 lần khi phóng to feature map.
- `SELECT`: TextCNN dùng trên token id INT32. Kernel `SELECT_V2` có sẵn không nhận INT32.

Lưu ý:
- Model dùng op khác thì phải thêm vào `REGISTER_OPS` và tăng `RESOLVER_SIZE`, nếu không `AllocateTensors` sẽ báo lỗi.
- ESP-NN có kernel int8 tối ưu cho Conv2D, DepthwiseConv2D, FullyConnected, Add, Mul và pooling. Op khác chạy kernel tham chiếu, chậm hơn.
- TFLite Micro không hỗ trợ int64 cho `Gather`. Token phải là **int32**.
- Upsample nên export thành `RESIZE_NEAREST_NEIGHBOR` (TFLite Micro có sẵn) thay vì `TILE`.

### 5.2 Hợp đồng vào/ra của detector

| Mục | Giá trị |
| --- | --- |
| Input | int8 `1 × 240 × 320 × 3`, RGB, chuẩn hóa `pixel / 127.5 - 1` (`DET_IN_DIV`, `DET_IN_SUB`) |
| Output | 6 tensor int8. Mỗi stride 4, 8, 16 có 1 tensor class `H × W × 7` (logit) và 1 tensor box `H × W × 4` (khoảng cách trái, trên, phải, dưới theo đơn vị stride) |
| Lưới | 60 × 80, 30 × 40, 15 × 20 (tổng 6300 anchor) |
| Class (đúng thứ tự) | `bottle cup phone book mouse box toy_car` |
| Hậu xử lý trên chip | sigmoid, ngưỡng score **0,25** (`CONF_THRESH`), box = (anchor ± khoảng cách) × stride, NMS IoU **0,6** theo từng class, tối đa 64 box |
| Tọa độ trả về | `[x1, y1, x2, y2]` chuẩn hóa 0..1 |

Giải mã nằm ở `decode_yolo` trong `main/logic.c`, viết theo hàm `decode()` của notebook `submission_export_v2.1.ipynb`.

### 5.3 Hợp đồng vào/ra của TextCNN

| Mục | Giá trị |
| --- | --- |
| Input | 40 token **int32**. 0 = pad, 1 = từ lạ, từ thứ k trong `vocab.cc` có id k+2 |
| Vocab | 260 từ tiếng Việt không dấu |
| Tiền xử lý | Trình duyệt bỏ dấu; chip đổi chữ thường, gộp khoảng trắng, đổi "o dau do" thành "xuat hien" như notebook |
| Output | 5 intent: `COUNT EXIST LIST LOCATION OUT_OF_SCOPE` |

Chuyển model mới bằng `tools/tflite_to_cc.py` (xem `main/models/README.md`).

## 6. Chi phí tính toán và bộ nhớ

### 6.1 Detector v2.1

| Mục | Giá trị | Nguồn |
| --- | --- | --- |
| Số op | 90 (40 Conv2D, 21 DepthwiseConv2D, 9 Add, 7 Pad, 6 Reshape, 4 Tile, 2 Concatenation, 1 MaxPool) | Tính |
| MAC | 306,2 triệu (Conv2D) + 25,8 triệu (Depthwise) = **332 triệu** | Tính |
| Trọng số int8 | 188.260 B (file `.tflite` 334.960 B) | Tính |
| Tensor activation lớn nhất | 120 × 160 × 32 = 614.400 B (tầng stride 4) | Tính |
| Đỉnh activation ước tính | khoảng 1,8 MB | Tính |
| Arena thực dùng | 2097 KB | Đo |

Phần lớn RAM và thời gian nằm ở các lớp độ phân giải 120 × 160 (nhánh stride 4). Giảm số kênh ở nhánh này, hoặc bỏ nhánh stride 4, là cách hiệu quả nhất để vừa nhanh hơn vừa nhẹ RAM hơn.

### 6.2 Detector baseline cũ (để so sánh)

| Lớp | Kích thước ra | Triệu MAC | Bộ nhớ ra (int8) | Tham số |
| --- | --- | ---: | ---: | ---: |
| Input | 240 × 320 × 3 | – | 230.400 B | – |
| Conv 3×3, 3→16 | 240 × 320 × 16 | 33,18 | **1.228.800 B** | 448 |
| MaxPool 2 | 120 × 160 × 16 | – | 307.200 B | – |
| Conv 3×3, 16→24 | 120 × 160 × 24 | 66,36 | 460.800 B | 3.480 |
| MaxPool 2 | 60 × 80 × 24 | – | 115.200 B | – |
| Conv 3×3, 24→36 | 60 × 80 × 36 | 37,32 | 172.800 B | 7.812 |
| MaxPool 2 | 30 × 40 × 36 | – | 43.200 B | – |
| Conv 3×3, 36→36 | 30 × 40 × 36 | 14,00 | 43.200 B | 11.700 |
| MaxPool 2 | 15 × 20 × 36 | – | 10.800 B | – |
| Conv 3×3, 36→28 | 15 × 20 × 28 | 2,72 | 8.400 B | 9.100 |
| Conv 1×1, 28→12 | 15 × 20 × 12 | 0,10 | 3.600 B | 348 |
| **Tổng** | | **153,68** | | **32.888** |

## 7. Ràng buộc hệ thống khi chạy

| Mục | Giá trị | Nguồn |
| --- | --- | --- |
| API | `POST /run`, body = 230.400 B RGB888 + câu hỏi ASCII tối đa 512 B | Code |
| Inference chạy ở đâu | Trong task HTTP, stack 16 KB. Mỗi lần xử lý một request. | Code |
| Watchdog | Một lần detect chiếm CPU khoảng 3 s liên tục. Chạy nhiều lần liền nhau cần nhường CPU (`vTaskDelay`), nếu không sẽ có cảnh báo `task_wdt`. | Đo |
| Stream camera | Cổng 81 (MJPEG), chạy song song và tranh CPU với detect | Code |
| Truyền ảnh qua Wi-Fi | chưa đo | |
| Công suất và nhiệt độ khi chạy liên tục | chưa đo | |

## 8. Checklist khi giao model mới

1. Lượng tử hóa int8 toàn phần, chỉ giữ float ở input/output nếu thực sự cần.
2. File `.tflite` của detection + TextCNN không vượt khoảng 2,5 MB (phần trống của phân vùng app).
3. Arena (đỉnh activation cộng scratch) dưới khoảng 6 MB. Đặt `DET_ARENA_KB` lớn hơn số đo khoảng 10%.
4. Chỉ dùng op trong mục 5.1, hoặc báo nhóm firmware để thêm kernel.
5. Ước lượng thời gian: số triệu MAC / 0,11 ≈ số ms.
6. Giữ đúng hợp đồng vào/ra ở mục 5.2 và 5.3, hoặc thống nhất trước khi đổi.
7. Thử model trên ảnh chụp từ chính OV7670, không chỉ trên ảnh test của dataset.
8. Chạy `python tools/tflite_to_cc.py ...`, build, nạp rồi đọc log khởi động: `detection OK | arena dung ...`, `textcnn OK` và các dòng `SELFTEST`.
