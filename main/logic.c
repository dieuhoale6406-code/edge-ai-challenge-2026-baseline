#include "logic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const CLASS_NAMES[NUM_CLASSES] = {"bottle", "cup", "phone", "book", "mouse", "box", "toy_car"};
const char *const INTENT_NAMES[NUM_INTENTS] = {"COUNT", "EXIST", "LIST", "LOCATION", "OUT_OF_SCOPE"};

static const char *const VN[NUM_CLASSES] = {"chai", "cốc", "điện thoại", "sách", "chuột", "hộp", "xe đồ chơi"};
/* tu khoa (khong dau) de biet cau hoi nhac toi class nao */
static const char *const KW[NUM_CLASSES][4] = {
    {"chai"}, {"coc", "tach", "ly"}, {"thoai", "smartphone"}, {"sach"},
    {"chuot"}, {"hop", "thung"}, {"xe", "oto"},
};

static float sig(float x) { return 1.f / (1.f + expf(-x)); }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static int by_score(const void *a, const void *b)
{
    float d = ((const det_t *)b)->score - ((const det_t *)a)->score;
    return (d > 0) - (d < 0);
}

static float box_iou(const det_t *a, const det_t *b)
{
    float w = fminf(a->x2, b->x2) - fmaxf(a->x1, b->x1);
    float h = fminf(a->y2, b->y2) - fmaxf(a->y1, b->y1);
    if (w <= 0 || h <= 0) return 0;
    float inter = w * h;
    float u = (a->x2 - a->x1) * (a->y2 - a->y1) + (b->x2 - b->x1) * (b->y2 - b->y1) - inter;
    return u > 0 ? inter / u : 0;
}

#define MAX_CAND 1024   /* so ung vien toi da truoc NMS (notebook: pre_nms 3000, nhung nguong conf cao hon) */

/* = decode() trong submission_export_v2.1.ipynb */
int decode_yolo(const yolo_level_t *lv, int nlv, int img_w, int img_h, float conf, float iou,
                det_t *out, int max)
{
    static det_t cand[MAX_CAND];
    int n = 0;
    const float logit_th = logf(conf / (1.f - conf));   /* sigmoid(x) >= conf  <=>  x >= logit(conf) */
    for (int l = 0; l < nlv; l++) {
        const yolo_level_t *L = &lv[l];
        const float q_th = logit_th / L->cls_scale + L->cls_zp;   /* so sanh ngay tren gia tri int8 */
        for (int y = 0; y < L->H; y++)
            for (int x = 0; x < L->W; x++) {
                const int a = y * L->W + x;
                const int8_t *c = L->cls + a * NUM_CLASSES;
                const int8_t *b = L->box + a * 4;
                for (int k = 0; k < NUM_CLASSES; k++) {
                    if (c[k] < q_th) continue;
                    float s = sig((c[k] - L->cls_zp) * L->cls_scale);
                    float ax = x + 0.5f, ay = y + 0.5f;
                    float x1 = (ax - (b[0] - L->box_zp) * L->box_scale) * L->stride;
                    float y1 = (ay - (b[1] - L->box_zp) * L->box_scale) * L->stride;
                    float x2 = (ax + (b[2] - L->box_zp) * L->box_scale) * L->stride;
                    float y2 = (ay + (b[3] - L->box_zp) * L->box_scale) * L->stride;
                    det_t d = {k, s, clamp01(x1 / img_w), clamp01(y1 / img_h), clamp01(x2 / img_w), clamp01(y2 / img_h)};
                    if (d.x2 <= d.x1 || d.y2 <= d.y1) continue;   /* notebook bo box dien tich 0 */
                    if (n < MAX_CAND) { cand[n++] = d; continue; }
                    int worst = 0;                                  /* day: thay ung vien diem thap nhat */
                    for (int i = 1; i < n; i++) if (cand[i].score < cand[worst].score) worst = i;
                    if (cand[worst].score < s) cand[worst] = d;
                }
            }
    }
    qsort(cand, n, sizeof(det_t), by_score);
    int keep = 0;
    for (int i = 0; i < n && keep < max; i++) {
        int ok = 1;
        for (int j = 0; j < keep && ok; j++)
            if (out[j].cls == cand[i].cls && box_iou(&out[j], &cand[i]) > iou) ok = 0;
        if (ok) out[keep++] = cand[i];
    }
    return keep;
}

void normalize_text(char *s)
{
    for (; *s; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') *s = c - 'A' + 'a';
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) *s = ' ';
    }
}

void tokenize(const char *in, const char *const *vocab, int nv, int32_t ids[SEQ_LEN])
{
    /* Gop khoang trang roi re.sub("o dau do", "xuat hien") nhu normalize_question trong notebook */
    char buf[1040], rep[2080];
    size_t p = 0, q = 0;
    for (const char *s = in; *s && p < sizeof buf - 1; s++)
        if (*s != ' ' || (p && buf[p - 1] != ' ')) buf[p++] = *s;
    while (p && buf[p - 1] == ' ') p--;
    buf[p] = 0;
    for (const char *s = buf; *s && q < sizeof rep - 10;) {
        if (!strncmp(s, "o dau do", 8)) { memcpy(rep + q, "xuat hien", 9); q += 9; s += 8; }
        else rep[q++] = *s++;
    }
    rep[q] = 0;

    const char *norm = rep;
    int n = 0;
    memset(ids, 0, SEQ_LEN * sizeof(int32_t));
    while (*norm && n < SEQ_LEN) {
        while (*norm == ' ') norm++;
        size_t len = strcspn(norm, " ");
        if (!len) break;
        int id = 1;
        for (int k = 0; k < nv; k++)
            if (strlen(vocab[k]) == len && !memcmp(vocab[k], norm, len)) { id = k + 2; break; }
        ids[n++] = id;
        norm += len;
    }
}

static int has_word(const char *norm, const char *w)
{
    size_t len = strlen(w);
    for (const char *p = norm; (p = strstr(p, w)); p++)
        if ((p == norm || p[-1] == ' ') && (p[len] == ' ' || p[len] == 0)) return 1;
    return 0;
}

static int find_class(const char *norm)
{
    for (int c = 0; c < NUM_CLASSES; c++)
        for (int k = 0; k < 4 && KW[c][k]; k++)
            if (has_word(norm, KW[c][k])) return c;
    return -1;
}

void make_answer(int intent, const char *norm_q, const det_t *d, int n, char *out, size_t sz)
{
    const char *name = INTENT_NAMES[intent];
    if (!strcmp(name, "OUT_OF_SCOPE")) {
        snprintf(out, sz, "Câu hỏi nằm ngoài phạm vi.");
        return;
    }
    if (!strcmp(name, "LIST")) {
        int seen[NUM_CLASSES] = {0}, k = 0;
        for (int i = 0; i < n; i++) k += !seen[d[i].cls]++;
        size_t p = snprintf(out, sz, "Có %d loại đối tượng", k);
        const char *sep = ":";
        for (int c = 0; c < NUM_CLASSES && p < sz; c++)
            if (seen[c]) { p += snprintf(out + p, sz - p, "%s %s", sep, VN[c]); sep = ","; }
        if (p < sz) snprintf(out + p, sz - p, ".");
        return;
    }
    int c = find_class(norm_q);
    if (c < 0) {
        snprintf(out, sz, "Không xác định được đối tượng trong câu hỏi.");
        return;
    }
    int cnt = 0, best = -1;
    for (int i = 0; i < n; i++)
        if (d[i].cls == c) { cnt++; if (best < 0) best = i; }   /* d da sort theo score */
    if (!strcmp(name, "COUNT")) {
        snprintf(out, sz, "Có %d %s.", cnt, VN[c]);
    } else if (!strcmp(name, "EXIST")) {
        snprintf(out, sz, cnt ? "Có %s trong ảnh." : "Không có %s trong ảnh.", VN[c]);
    } else if (best < 0) {
        snprintf(out, sz, "Không thấy %s trong ảnh.", VN[c]);
    } else {
        float cx = (d[best].x1 + d[best].x2) / 2;
        snprintf(out, sz, "%s nằm %s.", VN[c], cx < 1.f / 3 ? "bên trái" : cx < 2.f / 3 ? "ở giữa" : "bên phải");
    }
}
