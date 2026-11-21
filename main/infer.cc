#include "infer.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/schema/schema_generated.h"
#include "model_config.h"

static const char *TAG = "infer";

/* ---------- Op khong co san trong TFLite Micro ---------- */

/* TILE: lap lai tensor theo "multiples" (detector dung cho upsample). Shape output da co san trong model. */
static void tile_dim(const TfLiteIntArray *dims, const uint8_t *in, uint8_t *out, const int64_t *mult,
                     int dim, size_t esz, size_t *in_n, size_t *out_n)
{
    const int d = dims->data[dim];
    if (dim == dims->size - 1) {
        const size_t row = d * esz;
        for (int64_t m = 0; m < mult[dim]; m++) memcpy(out + m * row, in, row);
        *in_n = d;
        *out_n = d * mult[dim];
        return;
    }
    size_t tin = 0, tout = 0;
    for (int i = 0; i < d; i++) {
        size_t a, b;
        tile_dim(dims, in + tin * esz, out + tout * esz, mult, dim + 1, esz, &a, &b);
        tin += a;
        tout += b;
    }
    for (int64_t m = 1; m < mult[dim]; m++) memcpy(out + m * tout * esz, out, tout * esz);
    *in_n = tin;
    *out_n = tout * mult[dim];
}

static TfLiteStatus tile_prepare(TfLiteContext *, TfLiteNode *node)
{
    return node->inputs->size == 2 && node->outputs->size == 1 ? kTfLiteOk : kTfLiteError;
}

static TfLiteStatus tile_eval(TfLiteContext *context, TfLiteNode *node)
{
    const TfLiteEvalTensor *in = tflite::micro::GetEvalInput(context, node, 0);
    const TfLiteEvalTensor *mt = tflite::micro::GetEvalInput(context, node, 1);
    TfLiteEvalTensor *out = tflite::micro::GetEvalOutput(context, node, 0);
    const int nd = in->dims->size;
    if (nd < 1 || nd > 6) return kTfLiteError;
    int64_t mult[6];
    for (int i = 0; i < nd; i++)
        mult[i] = mt->type == kTfLiteInt64 ? mt->data.i64[i] : mt->data.i32[i];
    size_t esz = 0;
    if (tflite::TfLiteTypeSizeOf(in->type, &esz) != kTfLiteOk) return kTfLiteError;
    size_t a, b;
    tile_dim(in->dims, (const uint8_t *)in->data.raw, (uint8_t *)out->data.raw, mult, 0, esz, &a, &b);
    return kTfLiteOk;
}

/* SELECT: out = cond ? x : y, moi kieu du lieu (kernel SELECT_V2 cua TFLite Micro khong nhan INT32,
 * ma TextCNN dung SELECT tren token id INT32). Ho tro cung shape hoac tensor 1 phan tu (scalar). */
static TfLiteStatus select_prepare(TfLiteContext *, TfLiteNode *node)
{
    return node->inputs->size == 3 && node->outputs->size == 1 ? kTfLiteOk : kTfLiteError;
}

static size_t elem_count(const TfLiteEvalTensor *t)
{
    size_t n = 1;
    for (int i = 0; i < t->dims->size; i++) n *= t->dims->data[i];
    return n;
}

static TfLiteStatus select_eval(TfLiteContext *context, TfLiteNode *node)
{
    const TfLiteEvalTensor *c = tflite::micro::GetEvalInput(context, node, 0);
    const TfLiteEvalTensor *x = tflite::micro::GetEvalInput(context, node, 1);
    const TfLiteEvalTensor *y = tflite::micro::GetEvalInput(context, node, 2);
    TfLiteEvalTensor *out = tflite::micro::GetEvalOutput(context, node, 0);
    size_t esz = 0;
    if (c->type != kTfLiteBool || tflite::TfLiteTypeSizeOf(out->type, &esz) != kTfLiteOk) return kTfLiteError;
    const size_t n = elem_count(out), cn = elem_count(c), xn = elem_count(x), yn = elem_count(y);
    if ((cn != n && cn != 1) || (xn != n && xn != 1) || (yn != n && yn != 1)) return kTfLiteError;
    const uint8_t *xs = (const uint8_t *)x->data.raw, *ys = (const uint8_t *)y->data.raw;
    uint8_t *os = (uint8_t *)out->data.raw;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *src = c->data.b[cn == 1 ? 0 : i] ? xs + (xn == 1 ? 0 : i) * esz : ys + (yn == 1 ? 0 : i) * esz;
        memcpy(os + i * esz, src, esz);
    }
    return kTfLiteOk;
}

static TfLiteStatus parse_no_options(const tflite::Operator *, tflite::TfLiteBridgeBuiltinDataAllocator *, void **data)
{
    *data = nullptr;
    return kTfLiteOk;
}

/* Resolver = cac op trong REGISTER_OPS + 2 op tu viet o tren (TILE, SELECT). */
class Resolver : public tflite::MicroMutableOpResolver<RESOLVER_SIZE> {
  public:
    Resolver()
    {
        REGISTER_OPS(*this);
        select_ = {};
        select_.prepare = select_prepare;
        select_.invoke = select_eval;
        select_.builtin_code = tflite::BuiltinOperator_SELECT;
        tile_ = {};
        tile_.prepare = tile_prepare;
        tile_.invoke = tile_eval;
        tile_.builtin_code = tflite::BuiltinOperator_TILE;
    }
    using tflite::MicroMutableOpResolver<RESOLVER_SIZE>::FindOp;
    const TFLMRegistration *FindOp(tflite::BuiltinOperator op) const override
    {
        if (op == tflite::BuiltinOperator_SELECT) return &select_;
        if (op == tflite::BuiltinOperator_TILE) return &tile_;
        return tflite::MicroMutableOpResolver<RESOLVER_SIZE>::FindOp(op);
    }
    tflite::TfLiteBridgeBuiltinParseFunction GetOpDataParser(tflite::BuiltinOperator op) const override
    {
        if (op == tflite::BuiltinOperator_SELECT || op == tflite::BuiltinOperator_TILE) return parse_no_options;
        return tflite::MicroMutableOpResolver<RESOLVER_SIZE>::GetOpDataParser(op);
    }

  private:
    TFLMRegistration select_, tile_;
};

/* ---------- Nap model ---------- */

static tflite::MicroInterpreter *s_det, *s_txt;
static int8_t s_lut[256];             /* pixel 0..255 -> int8 da quantize */
static yolo_level_t s_lv[3];          /* 3 tang output stride 4, 8, 16 */

static tflite::MicroInterpreter *try_load(const tflite::Model *model, Resolver &ops, size_t bytes, uint8_t **arena)
{
    *arena = (uint8_t *)heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM);
    if (!*arena) return nullptr;
    auto *it = new tflite::MicroInterpreter(model, ops, *arena, bytes);
    if (it->AllocateTensors() == kTfLiteOk) return it;
    delete it;
    heap_caps_free(*arena);
    *arena = nullptr;
    return nullptr;
}

/* Thu arena cau hinh truoc. Thieu RAM -> thu lai voi khoi PSRAM lon nhat (tru ARENA_RESERVE_KB),
 * in ra so KB can that de chinh DET_ARENA_KB / TXT_ARENA_KB. */
static tflite::MicroInterpreter *load(const unsigned char *data, int arena_kb, const char *name)
{
    static Resolver ops;
    const tflite::Model *model = tflite::GetModel(data);
    if (model->version() != TFLITE_SCHEMA_VERSION) { ESP_LOGE(TAG, "%s: sai schema version", name); return nullptr; }
    uint8_t *arena;
    tflite::MicroInterpreter *it = try_load(model, ops, (size_t)arena_kb * 1024, &arena);
    if (!it) {
        size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        size_t reserve = (size_t)ARENA_RESERVE_KB * 1024;
        big = big > reserve ? (big - reserve) & ~(size_t)15 : 0;
        ESP_LOGW(TAG, "%s: arena %d KB khong du (hoac thieu op, xem log tren). Thu lai voi %u KB PSRAM",
                 name, arena_kb, (unsigned)(big / 1024));
        if (big > (size_t)arena_kb * 1024) it = try_load(model, ops, big, &arena);
        if (!it) { ESP_LOGE(TAG, "%s: AllocateTensors loi ca khi dung het PSRAM (xem log op thieu)", name); return nullptr; }
        ESP_LOGW(TAG, "%s: dang dung arena tam %u KB. Dat %s_ARENA_KB >= %u trong model_config.h",
                 name, (unsigned)(big / 1024), name[0] == 'd' ? "DET" : "TXT",
                 (unsigned)(it->arena_used_bytes() / 1024 + 16));
    }
    ESP_LOGI(TAG, "%s OK | arena dung %u KB", name, (unsigned)it->arena_used_bytes() / 1024);
    return it;
}

static float deq(const TfLiteTensor *t, int i)
{
    return t->type == kTfLiteInt8 ? (t->data.int8[i] - t->params.zero_point) * t->params.scale : t->data.f[i];
}

static int setup_det_outputs(void)
{
    memset(s_lv, 0, sizeof s_lv);
    for (size_t i = 0; i < s_det->outputs_size(); i++) {
        TfLiteTensor *o = s_det->output(i);
        if (o->type != kTfLiteInt8 || o->dims->size != 4) return -1;
        int h = o->dims->data[1], w = o->dims->data[2], c = o->dims->data[3];
        int stride = IMG_H / h, l = stride == 4 ? 0 : stride == 8 ? 1 : stride == 16 ? 2 : -1;
        if (l < 0 || IMG_W / w != stride) return -1;
        yolo_level_t *L = &s_lv[l];
        L->H = h; L->W = w; L->stride = stride;
        if (c == NUM_CLASSES) { L->cls = o->data.int8; L->cls_scale = o->params.scale; L->cls_zp = o->params.zero_point; }
        else if (c == 4) { L->box = o->data.int8; L->box_scale = o->params.scale; L->box_zp = o->params.zero_point; }
        else return -1;
    }
    for (int l = 0; l < 3; l++) if (!s_lv[l].cls || !s_lv[l].box) return -1;
    return 0;
}

int infer_init(void)
{
    s_det = load(DET_MODEL_DATA, DET_ARENA_KB, "detection");
    s_txt = load(TXT_MODEL_DATA, TXT_ARENA_KB, "textcnn");
    if (!s_det || !s_txt) { s_det = s_txt = nullptr; return -1; }

    TfLiteTensor *in = s_det->input(0);
    if (in->type != kTfLiteInt8 || in->bytes != IMG_BYTES || setup_det_outputs() != 0) {
        ESP_LOGE(TAG, "detection: can input int8 1x240x320x3 va 6 output int8 (cls %d + box 4 o stride 4/8/16)",
                 NUM_CLASSES);
        s_det = s_txt = nullptr;
        return -1;
    }
    for (int v = 0; v < 256; v++)
        s_lut[v] = (int8_t)fminf(127, fmaxf(-128, roundf((v / DET_IN_DIV - DET_IN_SUB) / in->params.scale + in->params.zero_point)));

    TfLiteTensor *ti = s_txt->input(0), *to = s_txt->output(0);
    if (ti->dims->data[ti->dims->size - 1] != SEQ_LEN || to->dims->data[to->dims->size - 1] != NUM_INTENTS) {
        ESP_LOGE(TAG, "textcnn: can input %d token va output %d intent", SEQ_LEN, NUM_INTENTS);
        s_det = s_txt = nullptr;
        return -1;
    }
    ESP_LOGI(TAG, "vocab %d tu, cau hoi %d token", VOCAB_LEN, SEQ_LEN);
    return 0;
}

int infer_detect(const uint8_t *rgb, det_t *out, int max)
{
    if (!s_det) return -1;
    TfLiteTensor *in = s_det->input(0);
    for (int i = 0; i < IMG_BYTES; i++) in->data.int8[i] = s_lut[rgb[i]];
    if (s_det->Invoke() != kTfLiteOk) return -1;
    return decode_yolo(s_lv, 3, IMG_W, IMG_H, CONF_THRESH, NMS_IOU, out, max);
}

int infer_intent(const char *norm_q)
{
    if (!s_txt) return -1;
    int32_t ids[SEQ_LEN];
    tokenize(norm_q, VOCAB_DATA, VOCAB_LEN, ids);
    TfLiteTensor *in = s_txt->input(0);
    for (int i = 0; i < SEQ_LEN; i++) {
        if (in->type == kTfLiteInt64) in->data.i64[i] = ids[i];
        else if (in->type == kTfLiteInt32) in->data.i32[i] = ids[i];
        else if (in->type == kTfLiteFloat32) in->data.f[i] = ids[i];
        else { ESP_LOGE(TAG, "textcnn: kieu input %d chua ho tro", in->type); return -1; }
    }
    if (s_txt->Invoke() != kTfLiteOk) return -1;
    TfLiteTensor *o = s_txt->output(0);
    int best = 0;
    for (int k = 1; k < NUM_INTENTS; k++) if (deq(o, k) > deq(o, best)) best = k;
    return best;
}
