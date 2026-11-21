/* xem README: gcc -I main test/test_logic.c main/logic.c -lm -o /tmp/t && /tmp/t */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "logic.h"

/* Tang stride 16 (15x20), scale 0.1, zero point 0: logit 5.0 -> int8 50, dist 1.0 -> int8 10 */
#define LH 15
#define LW 20
static int8_t cls[LH * LW * NUM_CLASSES], box[LH * LW * 4];

static void put(int y, int x, int c, int logit10, int dist10)
{
    cls[(y * LW + x) * NUM_CLASSES + c] = (int8_t)logit10;
    for (int k = 0; k < 4; k++) box[(y * LW + x) * 4 + k] = (int8_t)dist10;
}

int main(void)
{
    memset(cls, -100, sizeof cls);   /* logit -10 -> score ~ 0 */
    put(7, 2, 1, 50, 30);            /* cup ben trai, dist 3 stride -> box 96x96 px */
    put(7, 3, 1, 40, 30);            /* cup lech 1 o, IoU ~0.77 > 0.6 -> NMS bo */
    put(7, 17, 1, 50, 10);           /* cup ben phai */
    put(2, 10, 3, 50, 10);           /* book giua */
    put(12, 10, 4, 0, 10);           /* mouse score 0.5 */
    yolo_level_t lv = {cls, box, LH, LW, 16, 0.1f, 0.1f, 0, 0};
    det_t d[64];
    int n = decode_yolo(&lv, 1, 320, 240, 0.6f, 0.6f, d, 64);
    printf("%d box\n", n);
    assert(n == 3);                  /* mouse 0.5 < 0.6 bi loai, cup chong bi NMS */
    assert(d[0].score >= d[1].score && d[1].score >= d[2].score);
    /* cup trai: anchor (2.5, 7.5) * 16 = (40, 120) +- 48 px -> x -8..88, cat ve 0..88 */
    int found = 0;
    for (int i = 0; i < n; i++)
        if (d[i].cls == 1 && fabsf(d[i].x1 * 320) < 0.01f && fabsf(d[i].x2 * 320 - 88) < 0.01f) found = 1;
    assert(found);
    assert(decode_yolo(&lv, 1, 320, 240, 0.4f, 0.6f, d, 64) == 4);   /* ha nguong -> co mouse */
    assert(decode_yolo(&lv, 1, 320, 240, 0.4f, 0.6f, d, 2) == 2);    /* gioi han max */

    const char *vocab[] = {"bao", "co", "coc", "nhieu", "xuat", "hien", "o"};
    char q[] = "Co   bao nhieu COC_xyz?";   /* web da bo dau truoc khi gui */
    normalize_text(q);
    int32_t ids[SEQ_LEN];
    tokenize(q, vocab, 7, ids);
    assert(ids[0] == 3 && ids[1] == 2 && ids[2] == 5 && ids[3] == 4 && ids[4] == 1 && ids[5] == 0);
    char q5[] = "coc o dau do";   /* notebook: re.sub("o dau do", "xuat hien") */
    tokenize(q5, vocab, 7, ids);
    assert(ids[0] == 4 && ids[1] == 6 && ids[2] == 7 && ids[3] == 0);

    char a[256];
    char q1[] = "co bao nhieu coc"; make_answer(0, q1, d, n, a, sizeof a); puts(a); assert(strstr(a, "2"));
    char q2[] = "co dien thoai khong"; make_answer(1, q2, d, n, a, sizeof a); puts(a); assert(strstr(a, "Không"));
    char q3[] = "sach nam o dau"; make_answer(3, q3, d, n, a, sizeof a); puts(a); assert(strstr(a, "giữa"));
    make_answer(2, q1, d, n, a, sizeof a); puts(a); assert(!strcmp(a, "Có 2 loại đối tượng: cốc, sách."));
    make_answer(4, q1, d, n, a, sizeof a); puts(a);
    puts("OK");
    return 0;
}
