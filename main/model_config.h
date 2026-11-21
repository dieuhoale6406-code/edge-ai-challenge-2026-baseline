#pragma once
/* Cau hinh model. Doi ten mang theo file .h trong main/models/ (sinh bang tools/tflite_to_cc.py) */
#include "det_model.h"
#include "textcnn_model.h"
#include "vocab.h"

#define DET_MODEL_DATA   det_model        /* mang trong det_model.cc */
#define TXT_MODEL_DATA   textcnn_model    /* mang trong textcnn_model.cc */
#define VOCAB_DATA       vocab            /* vocab.cc: vocab[k] co token id k+2 */
#define VOCAB_LEN        vocab_len

/* Tensor arena trong PSRAM. Neu AllocateTensors bao thieu, infer.cc tu thu lai voi khoi PSRAM
 * lon nhat con trong (tru ARENA_RESERVE_KB) va in ra so KB can that -> chinh lai gia tri nay. */
#define DET_ARENA_KB     3072
#define TXT_ARENA_KB     32
#define ARENA_RESERVE_KB 512    /* chua lai cho camera, Wi-Fi, buffer /run */

/* Detector YOLO student v2.1: input = pixel / 127.5 - 1 (nhu run_detector trong notebook) */
#define DET_IN_DIV       127.5f
#define DET_IN_SUB       1.0f
#define CONF_THRESH      0.25f  /* notebook dung 0.001 de tinh mAP; tren web loc 0.25 cho de doc */
#define NMS_IOU          0.6f   /* = decode_config.nms_iou trong notebook */

/* Op builtin 2 model dang dung. TILE (detector) va SELECT (TextCNN) khong co san trong TFLite Micro:
 * infer.cc tu cai kernel cho 2 op nay, khong can them o day. Doi model -> AllocateTensors se bao op thieu */
#define RESOLVER_SIZE    12
#define REGISTER_OPS(r) do { \
    (r).AddConv2D(); (r).AddDepthwiseConv2D(); (r).AddMaxPool2D(); (r).AddPad();          \
    (r).AddAdd(); (r).AddConcatenation(); (r).AddReshape();                                \
    (r).AddLess(); (r).AddGather(); (r).AddFullyConnected(); (r).AddSoftmax();             \
} while (0)
