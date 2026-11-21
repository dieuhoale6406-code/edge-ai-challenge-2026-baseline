/* Logic thuan C (khong phu thuoc IDF) -> test duoc tren may host: test/test_logic.c */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_CLASSES 7   /* thu tu = classes.txt cua dataset */
#define NUM_INTENTS 5   /* sorted(df.intent.unique()) trong baseline.ipynb */
#define SEQ_LEN     40   /* = sequence_length trong intent_vocab.json */

extern const char *const CLASS_NAMES[NUM_CLASSES];
extern const char *const INTENT_NAMES[NUM_INTENTS];

typedef struct { int cls; float score, x1, y1, x2, y2; } det_t;   /* toa do chuan hoa 0..1 */

/* Mot tang output cua detector kieu YOLO anchor-free (stride 4/8/16):
 * cls = [H][W][NUM_CLASSES] logit int8, box = [H][W][4] khoang cach l,t,r,b tinh theo don vi stride. */
typedef struct {
    const int8_t *cls, *box;
    int H, W, stride;
    float cls_scale, box_scale;
    int cls_zp, box_zp;
} yolo_level_t;

/* = decode() trong submission_export_v2.1.ipynb: sigmoid(cls) >= conf, box = (anchor +- dist) * stride,
 * NMS theo tung class. Tra ve so box (sap xep giam dan theo score), toa do chuan hoa 0..1. */
int decode_yolo(const yolo_level_t *lv, int nlv, int img_w, int img_h, float conf, float iou,
                det_t *out, int max);

/* lowercase, ky tu khong phai [a-z0-9] -> ' ' (giong normalize_question trong notebook, chuoi da bo dau) */
void normalize_text(char *s);

/* vocab[k] co id k+2; 0 = pad, 1 = unknown. Gop khoang trang va doi "o dau do" -> "xuat hien" nhu notebook */
void tokenize(const char *norm, const char *const *vocab, int nv, int32_t ids[SEQ_LEN]);

void make_answer(int intent, const char *norm_q, const det_t *d, int n, char *out, size_t sz);

#ifdef __cplusplus
}
#endif
