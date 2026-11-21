# Trọng số model (mảng C `.cc` + `.h`)

Đang dùng model **YOLO KD student v2.1** (detector anchor-free stride 4/8/16) và **TextCNN 40 token**.
Cần đủ 3 cặp file: `det_model.cc/.h`, `textcnn_model.cc/.h`, `vocab.cc/.h`.

## Thay model từ file `.tflite`

```bash
python tools/tflite_to_cc.py --det student_detector_int8.tflite \
    --text textcnn_int8.tflite --vocab intent_vocab.json --out main/models
idf.py build flash monitor
```

Log khởi động đúng sẽ có `detection OK | arena dung ... KB`, `textcnn OK` và các dòng `SELFTEST`.

## Hợp đồng với firmware

- Detector: input int8 `1x240x320x3`, chuẩn hóa `pixel / 127.5 - 1` (`DET_IN_DIV`, `DET_IN_SUB` trong `main/model_config.h`).
  Output 6 tensor int8: với mỗi stride 4, 8, 16 có 1 tensor class `HxWx7` (logit, sigmoid) và 1 tensor box `HxWx4`
  (khoảng cách trái, trên, phải, dưới tính theo đơn vị stride). Giải mã trong `decode_yolo` (`main/logic.c`).
- Class theo thứ tự `bottle cup phone book mouse box toy_car`.
- TextCNN: input `SEQ_LEN` = 40 token **int32** (`main/logic.h`), `<pad>` = 0, `<unk>` = 1, từ thứ k trong `vocab.cc` có id k + 2.
  Output 5 intent `COUNT EXIST LIST LOCATION OUT_OF_SCOPE`.
- Op: danh sách trong `REGISTER_OPS` (`main/model_config.h`). TILE và SELECT không có sẵn trong TFLite Micro
  nên `main/infer.cc` tự cài kernel.
- RAM: arena đặt trong PSRAM (`DET_ARENA_KB`). Nếu thiếu, firmware tự thử lại với khối PSRAM lớn nhất
  và in ra số KB cần đặt.
- Flash: `partitions.csv` dành 3,9 MB cho firmware + model.
