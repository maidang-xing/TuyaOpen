/**
 * @file buddy_anim.c
 * @brief Terminal-style animation helpers for Claude Buddy UI.
 */
#include "buddy_anim.h"
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Typewriter
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t  *label;
    char      *text;        /* full text, heap-allocated */
    size_t     total_bytes; /* strlen(text) */
    size_t     shown_bytes; /* bytes revealed so far */
    lv_timer_t *timer;
} tw_ctx_t;

/* advance shown_bytes by one UTF-8 codepoint */
static size_t utf8_next(const char *s, size_t pos, size_t len)
{
    if (pos >= len) return pos;
    uint8_t c = (uint8_t)s[pos];
    if      (c < 0x80)              return pos + 1;
    else if ((c & 0xE0) == 0xC0)   return pos + 2;
    else if ((c & 0xF0) == 0xE0)   return pos + 3;
    else                            return pos + 4;
}

static void tw_cb(lv_timer_t *t)
{
    tw_ctx_t *ctx = (tw_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx) return;

    if (ctx->shown_bytes >= ctx->total_bytes) {
        lv_label_set_text(ctx->label, ctx->text);
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
        free(ctx->text);
        free(ctx);
        return;
    }

    ctx->shown_bytes = utf8_next(ctx->text, ctx->shown_bytes, ctx->total_bytes);

    char save = ctx->text[ctx->shown_bytes];
    ctx->text[ctx->shown_bytes] = '\0';
    lv_label_set_text(ctx->label, ctx->text);
    ctx->text[ctx->shown_bytes] = save;
}

void buddy_anim_typewriter(lv_obj_t *label, const char *text, uint32_t interval_ms)
{
    if (!label || !text) return;
    buddy_anim_typewriter_stop(label);

    tw_ctx_t *ctx = (tw_ctx_t *)malloc(sizeof(tw_ctx_t));
    if (!ctx) { lv_label_set_text(label, text); return; }

    ctx->text = (char *)malloc(strlen(text) + 1);
    if (!ctx->text) { free(ctx); lv_label_set_text(label, text); return; }

    strcpy(ctx->text, text);
    ctx->label       = label;
    ctx->total_bytes = strlen(text);
    ctx->shown_bytes = 0;

    lv_label_set_text(label, "");
    ctx->timer = lv_timer_create(tw_cb, interval_ms, ctx);
}

void buddy_anim_typewriter_stop(lv_obj_t *label)
{
    /* walk all active timers — find one whose user_data points to this label */
    lv_timer_t *t = lv_timer_get_next(NULL);
    while (t) {
        lv_timer_t *next = lv_timer_get_next(t);
        tw_ctx_t *ctx = (tw_ctx_t *)lv_timer_get_user_data(t);
        if (ctx && ctx->label == label && ctx->timer == t) {
            lv_timer_del(t);
            free(ctx->text);
            free(ctx);
            break;
        }
        t = next;
    }
}

/* ---------------------------------------------------------------------------
 * Blink
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t   *obj;
    lv_color_t  color_a;
    lv_color_t  color_b;
    bool        state;    /* false = color_a, true = color_b */
    lv_timer_t *timer;
} blink_ctx_t;

static void blink_cb(lv_timer_t *t)
{
    blink_ctx_t *ctx = (blink_ctx_t *)lv_timer_get_user_data(t);
    if (!ctx) return;
    ctx->state = !ctx->state;
    lv_color_t c = ctx->state ? ctx->color_b : ctx->color_a;
    lv_obj_set_style_bg_color(ctx->obj, c, 0);
    lv_obj_set_style_bg_opa(ctx->obj, LV_OPA_COVER, 0);
}

void buddy_anim_blink(lv_obj_t *obj, lv_color_t color_a, lv_color_t color_b, uint32_t period_ms)
{
    if (!obj) return;
    buddy_anim_blink_stop(obj);

    blink_ctx_t *ctx = (blink_ctx_t *)malloc(sizeof(blink_ctx_t));
    if (!ctx) return;
    ctx->obj     = obj;
    ctx->color_a = color_a;
    ctx->color_b = color_b;
    ctx->state   = false;
    ctx->timer   = lv_timer_create(blink_cb, period_ms, ctx);

    lv_obj_set_style_bg_color(obj, color_a, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void buddy_anim_blink_stop(lv_obj_t *obj)
{
    lv_timer_t *t = lv_timer_get_next(NULL);
    while (t) {
        lv_timer_t *next = lv_timer_get_next(t);
        blink_ctx_t *ctx = (blink_ctx_t *)lv_timer_get_user_data(t);
        if (ctx && ctx->obj == obj && ctx->timer == t) {
            lv_obj_set_style_bg_color(obj, ctx->color_a, 0);
            lv_timer_del(t);
            free(ctx);
            break;
        }
        t = next;
    }
}
