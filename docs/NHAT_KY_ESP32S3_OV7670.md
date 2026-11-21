# Nhật ký ESP32-S3 và OV7670

Cập nhật ngày 21/11/2026. Ghi lại các bước đã làm với board ESP32-S3 + OV7670, từ lúc đọc cấu hình đến khi chạy model YOLO KD v2.1 trên firmware này.

Các sketch Arduino nhắc tới ở mục 1–6 (`esp32s3_blink`, `esp32s3_full_test`, `esp32s3_ov7670_test`) nằm trong thư mục làm việc `D:\EdgeAI`, ngoài repo này.

## Trạng thái hiện tại

Board chạy firmware của repo này (ESP-IDF 6.1.1) với model **YOLO KD student v2.1** và **TextCNN 40 token**. Log khởi động xác nhận `detection OK | arena dung 2097 KB`, `textcnn OK`, detect 2942–2966 ms, intent 5,1 ms. Độ chính xác box trên ảnh thật chưa được đối chiếu với notebook.

| Mục | Giá trị |
| --- | --- |
| Firmware | Repo này, nạp bằng `idf.py -p COM9 flash` |
| Cổng và tốc độ log | COM9 qua USB–UART CH343, 115200 baud |
| Wi-Fi của board | `ESP32S3-INFER`, mật khẩu `12345678` |
| Trang web | http://192.168.4.1/ (tải ảnh, câu hỏi, camera, kết quả) |
| Stream camera | `http://192.168.4.1:81/stream` (MJPEG) |
| Thông số phần cứng chi tiết | [THONG_SO_PHAN_CUNG.md](THONG_SO_PHAN_CUNG.md) |

## 1. Đọc cấu hình board qua COM9

- Dùng `esptool` để xác nhận ESP32-S3 QFN56 rev v0.2, CPU hai nhân 240 MHz, thạch anh 40 MHz, flash quad 16 MB và PSRAM tích hợp 8 MB. Mã MAC đọc được là `3c:dc:75:62:ae:a4`.
- Đọc bảng phân vùng trên flash để chọn đúng cấu hình khi build và nạp. Bảng có `nvs`, `otadata`, hai slot ứng dụng `app0` và `app1` dung lượng 3 MiB mỗi slot, `ffat` và `coredump`. Cấu hình Arduino tương ứng là `16M Flash (3MB APP/9.9MB FATFS)`.
- Có lúc COM9 báo `Access denied` trong khi Serial Monitor đang mở. Sau khi đóng Serial Monitor, đã mở lại cổng và nạp firmware được.

## 2. Sửa và nạp Blink 1 Hz

- Đồng bộ sketch Blink dùng trong Arduino IDE (`esp32s3_blink/esp32s3_blink.ino`) và bản sao ở thư mục gốc (`esp32s3_blink.ino`). LED RGB sáng xanh lá 500 ms rồi tắt 500 ms. Sketch cũng phát tín hiệu bật/tắt trên GPIO2 cho LED đơn.
- Build và nạp qua COM9. Log 115200 baud ghi nhận tám trạng thái ON/OFF liên tiếp, mỗi lần cách đúng 500 ms. Nhịp được xác nhận qua log firmware, còn ánh sáng LED trên board chưa được quan sát trực tiếp trong phiên kiểm tra.

## 3. Đọc sketch chẩn đoán và hướng dẫn môi trường

- Đọc `esp32s3_full_test.ino` và đối chiếu với bản Arduino IDE (`esp32s3_full_test/esp32s3_full_test.ino`). Sketch đọc thông tin chip, flash, RAM, ghi và đọc lại 2 MiB PSRAM, đọc nhiệt độ, quét Wi-Fi và chạy LED cầu vồng. Sketch này được rà soát để giải thích cách hoạt động.
- Hướng dẫn dùng Wokwi để thử logic với ESP32-S3 và cấu hình flash 16 MB, PSRAM octal 8 MB. Các phép đo vật lý như tốc độ PSRAM và nhiệt độ chip cần board thật.
- Hướng dẫn cài ESP-IDF qua ESP-IDF Installation Manager và extension Espressif trong VS Code. Đã xác định lỗi `EIM-E802` trong ảnh là thiếu quyền ở bước **Install Drivers**. Driver CH343 đã hoạt động qua COM9, vì vậy có thể chọn **Install New Version** để cài SDK dù chưa cắm board. Phần này là hướng dẫn, chưa xác nhận kết quả cài SDK trên máy.

## 4. Tạo sketch kiểm tra ESP32-S3 và OV7670

Sketch kết hợp LED 1 Hz, thông tin phần cứng, phép thử ghi/đọc 2 MiB PSRAM và kiểm tra camera OV7670. Camera chạy XCLK 20 MHz, dùng một bộ đệm khung. Khi có PSRAM, ảnh RGB565 có kích thước 320 × 240. Sketch có chế độ dự phòng 160 × 120 trong RAM nội.

### Sơ đồ chân đã đối chiếu với ảnh người dùng gửi

| Chân OV7670 | Kết nối ESP32-S3 |
| --- | --- |
| 3.3V, GND | 3V3, GND |
| SDA / SIOD | GPIO16 |
| SCL / SIOC | GPIO17 |
| XCLK | GPIO12 |
| PCLK | GPIO13 |
| VSYNC | GPIO14 |
| HREF | GPIO15 |
| D0, D1, D2, D3 | GPIO4, GPIO5, GPIO6, GPIO7 |
| D4, D5, D6, D7 | GPIO8, GPIO9, GPIO10, GPIO11 |
| RST | Nối cứng 3V3, khai báo `-1` trong code |
| PWDN | Nối cứng GND, khai báo `-1` trong code |

- Sau khi nạp bản có web server, log khởi động xác nhận phép thử PSRAM **PASS**, sai lệch `0` từ 32 bit. Camera được nhận diện với `PID=0x76`, `VER=0x73` và bộ đệm đặt trong PSRAM.
- Log `FRAME OK` kiểm tra định dạng, độ dài và in mã băm của khung. Đã quan sát nhiều mã băm khác nhau. Lệnh `p` trong Serial Monitor in ảnh xem trước dạng ký tự. Lệnh `r` khởi tạo lại camera.

## 5. Khắc phục mất log và triển khai web server

- Ảnh Serial Monitor ban đầu chỉ có thông báo boot ROM. Firmware được chuyển sang `Serial0` để log đi thẳng qua UART0 của COM9. Sau khi nạp, đã đọc được log khởi động và log định kỳ.
- ESP32-S3 tự phát Wi-Fi và chạy web server ngay trên board. Bản đầu dùng trang HTTP gọi `GET /frame.jpg` mỗi lần cần ảnh rồi chờ 200 ms sau phản hồi. OV7670 cung cấp RGB565 và ESP32-S3 mã hóa sang JPEG để trình duyệt hiển thị.
- Bản đã nạp sau cùng giữ HTTP để tải trang ở `/`, rồi mở một kết nối WebSocket tại `/ws`. Trình duyệt gửi `next`, ESP32-S3 trả về một JPEG nhị phân. Sau khi giải mã và vẽ khung lên canvas, trình duyệt yêu cầu khung kế tiếp. Cách này bỏ khoảng chờ 200 ms và giới hạn mỗi trình xem có một khung đang xử lý.
- Bản WebSocket bật `TCP_NODELAY`, chọn JPEG quality `60`, tự kết nối lại khi socket đóng và hiển thị FPS trên trang. Firmware in nhịp trạng thái và số khung WebSocket gửi được mỗi giây theo từng khoảng đo. Một mutex đồng bộ phần chụp ảnh giữa web server và các lệnh chẩn đoán Serial.
- Build WebSocket thành công với Arduino ESP32 core 4.0.0. Đã kiểm tra logic JavaScript bằng Node.js với WebSocket và canvas mô phỏng, gồm thứ tự vẽ trước khi yêu cầu khung tiếp theo, hiển thị FPS và thử lại khi nhận lỗi từ camera. Đã nạp firmware và xác nhận hash flash, log `FRAME OK` và `web=ready` trên board thật.

## 6. Tăng FPS, giảm trễ và thêm luồng UDP

- Đo từng bước trên board: chụp mất 270–370 ms, mã hóa JPEG 66 ms. Driver esp32-camera đọc CLKRC qua SCCB, nhận về `0xA1` rồi ghi lại, nên clock cảm biến bị chia 34. Repo baseline cũng ghi nhận lỗi này. Ghi `CLKRC=0x01` sau khi khởi tạo đưa camera lên 12–14 fps.
- Ghi bộ 139 thanh ghi chất lượng ảnh (gamma, AEC/AGC, AWB, ma trận màu, khử nhiễu) và cửa sổ QVGA lấy từ `edge-ai-challenge-2026-baseline/main/cam.c`.
- Chuyển sang 2 bộ đệm khung với `CAMERA_GRAB_LATEST`. Camera và JPEG chạy trong task riêng trên core 1 và chỉ giữ khung mới nhất. WebSocket không bao giờ gửi lại khung cũ. Camera nghỉ sau 3 giây nếu không có người xem.
- Trình duyệt không nhận được UDP thô, nên WebSocket vẫn dùng cho trang web. UDP dành cho `esp32s3_ov7670_test/udp_viewer.py` trên máy tính. Khung mất gói hoặc đã cũ bị bỏ, không gửi lại.
- Lệnh Serial mới: `b` chạy benchmark 10 giây (in dòng `[PERF]`), `x` đổi XCLK 20/16/10/8 MHz, `wRRVV` ghi thanh ghi OV7670. Thử nghiệm cho thấy XCLK 20 MHz cho FPS cao nhất mà không phát sinh lỗi khung.

## 7. Giao diện detect + intent trên firmware baseline

- Sketch Arduino không có model, nên giao diện có tải ảnh, câu hỏi và kết quả được làm trên dự án ESP-IDF `edge-ai-challenge-2026-baseline`. Đã thiết kế lại `main/index.html`: ảnh đầu vào (chọn, kéo thả, dán, câu hỏi mẫu), camera trực tiếp có nút **Detect khung này** để gửi khung camera vào model, kết quả gồm bounding box, intent, câu trả lời, thời gian và bảng đối tượng. Trang có giao diện tối và bố cục cho điện thoại.
- Build bằng ESP-IDF v6.1.1 sau `idf.py set-target esp32s3`, đặt XCLK 20 MHz trong `sdkconfig`, rồi nạp qua COM9. Log xác nhận `detection OK` (arena 1502/2048 KB), `textcnn OK`, `vocab 261 tu`, camera OV7670 và stream cổng 81.
- Firmware baseline phát Wi-Fi `ESP32S3-INFER`, mật khẩu `12345678`, trang http://192.168.4.1/.

## 8. Đưa project lên fork GitHub

- Remote `origin` ban đầu vẫn trỏ về repo của ban tổ chức. Đã đổi thành `origin` = fork `dieuhoale6406-code/edge-ai-challenge-2026-baseline` và giữ repo gốc dưới tên `upstream` để kéo cập nhật (`git pull upstream main`).
- Commit `ca5097f` đưa lên fork: giao diện mới, cấu hình camera giống firmware Arduino, tự kiểm tra lúc khởi động.

## 9. Kiểm tra lỗi ảnh nhiễu như TV

- So cấu hình camera của ban tổ chức với firmware Arduino: khác ở `CAMERA_PSRAM_DMA` (bật/tắt), XCLK (10/20 MHz) và nhân CPU chạy task camera (core 1/core 0).
- Thêm công cụ `CAM_DIAG_DUMP` trong `main/cam.c`: in một khung RGB565 thô qua UART dạng base64, rồi dựng lại ảnh trên máy tính để so sánh.
- Lần chụp đầu, cấu hình của ban tổ chức cho ảnh vỡ thành các dải sai màu. Sau khi tắt log trong lúc gửi ảnh, chụp lại thì **cả hai cấu hình đều cho ảnh sạch** (chỉ số chênh lệch giữa các hàng điểm ảnh 2,0 ở cả hai). Các dải ở lần đầu là do log chen vào dữ liệu serial, không phải do camera.
- Kết luận: chưa tái hiện được nhiễu trên ảnh gốc khi board rảnh. Nhiễu có thể chỉ xuất hiện khi đang stream qua Wi-Fi hoặc khi chạy detect cùng lúc. Repo vẫn chuyển sang cấu hình giống firmware Arduino: XCLK 20 MHz, tắt `CAMERA_PSRAM_DMA`, task camera trên core 0, JPEG quality 25.

## 10. Đo thông số phần cứng cho nhóm model

- Thêm phần tự kiểm tra lúc khởi động trong `main/main.c`: chạy detect 3 lần trên ảnh mẫu, chạy intent một lần, rồi in RAM nội và PSRAM còn trống ra các dòng `SELFTEST`.
- Với model baseline: detect 1119–1221 ms, intent 2,3–2,8 ms, PSRAM còn trống 5419 KB, SRAM nội 116 KB.
- Kết quả tổng hợp trong [THONG_SO_PHAN_CUNG.md](THONG_SO_PHAN_CUNG.md).

## 11. Tích hợp model YOLO KD v2.1

- Model lấy từ `edge_ai_2026_round1_yolo_kd_v2.1/models`: `student_detector_int8.tflite` (334.960 B), `textcnn_int8.tflite` (27.536 B), `intent_vocab.json`. Chuyển sang mảng C bằng `tools/tflite_to_cc.py`.
- Đọc cấu trúc model: detector có 90 op, 332 triệu MAC, 6 output (class và box ở stride 4, 8, 16), ảnh vào chuẩn hóa `pixel / 127.5 - 1`. TextCNN có 40 token.
- TFLite Micro thiếu hai op: `TILE` (không có kernel) và `SELECT` (kernel `SELECT_V2` có sẵn không nhận INT32). Đã viết kernel riêng cho cả hai trong `main/infer.cc`. Lần nạp đầu intent báo `Node SELECT failed to invoke`, sửa xong thì intent trả đúng `COUNT`.
- Viết `decode_yolo` trong `main/logic.c` theo hàm `decode()` của notebook. Tokenizer thêm quy tắc đổi "o dau do" thành "xuat hien". Test trên máy tính (`test/test_logic.c`) pass.
- RAM: arena detection cần 2097 KB, vượt mức 2048 KB cũ. Đã đặt `DET_ARENA_KB` = 3072, và thêm cơ chế tự thử lại với khối PSRAM lớn nhất nếu arena không đủ. PSRAM còn trống 4491 KB.
- Flash: firmware 1.514.368 B, gần chạm phân vùng app cũ 1.536.000 B. Đã thêm `partitions.csv` với app 3,94 MB, vẫn vừa flash 4 MB.

## Mở lại hệ thống

1. Cấp nguồn cho board, nối điện thoại hoặc máy tính vào Wi-Fi `ESP32S3-INFER` và mở http://192.168.4.1/. Nếu thiết bị hỏi về Wi-Fi không có Internet, chọn giữ kết nối.
2. Để xem log, mở monitor ở **COM9, 115200 baud** (`idf.py -p COM9 monitor`). Các dòng chính: `detection OK`, `textcnn OK`, `Camera stream`, `SELFTEST`. Đóng Serial Monitor của Arduino IDE trước khi nạp, nếu không COM9 sẽ báo bận.
3. Build và nạp lại: mở terminal ESP-IDF 6.1.1, chạy `idf.py build` rồi `idf.py -p COM9 flash`. Lần đầu trên máy mới, chạy `idf.py set-target esp32s3`.
4. Thay model: `python tools/tflite_to_cc.py --det ... --text ... --vocab ... --out main/models` rồi build lại. Xem `main/models/README.md`.
5. Quay về firmware stream WebSocket/UDP (sketch Arduino `esp32s3_ov7670_test`): chọn board **ESP32S3 Dev Module**, **Flash Size 16 MB**, **DIO 80 MHz**, **OPI PSRAM**, bảng phân vùng **16M Flash (3MB APP/9.9MB FATFS)**, **USB CDC On Boot: Disabled**. Wi-Fi của sketch là `ESP32S3-OV7670`, mật khẩu `OV7670-view`.
