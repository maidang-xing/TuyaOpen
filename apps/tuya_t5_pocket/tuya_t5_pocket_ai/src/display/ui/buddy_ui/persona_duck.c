/**
 * @file persona_duck.c
 * @brief "duck" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/duck.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __duck_sleep / __duck_idle / __duck_busy / __duck_attention /
 *   __duck_celebrate / __duck_dizzy / __duck_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: duck ---- */

// ─── SLEEP ───  ~12s cycle, head tucked, gentle bobs on water
static void __duck_sleep(uint32_t t) {
  static const char* const TUCK[5]    = { "            ", "            ", "    __      ", "  <(-_)_)   ", " ~~~~~~~~~~ " };
  static const char* const BREATHE[5] = { "            ", "    __      ", "  <(-_)_)   ", "  ~~~~~~~~  ", "   ~~~~~~   " };
  static const char* const SNORE[5]   = { "            ", "    __      ", "  <(o.)_)   ", "  ~~~~~~~~  ", "   ~~~~~~   " };
  static const char* const DRIFT_L[5] = { "            ", "   __       ", " <(-_)_)    ", " ~~~~~~~~~~ ", "  ~~~~~~~~  " };
  static const char* const DRIFT_R[5] = { "            ", "     __     ", "   <(-_)_)  ", " ~~~~~~~~~~ ", "  ~~~~~~~~  " };
  static const char* const DREAM[5]   = { "            ", "    __      ", "  <(uu)_)   ", " ~~~~~~~~~~ ", "  ~~~~~~~~  " };

  const char* const* P[6] = { TUCK, BREATHE, SNORE, DRIFT_L, DRIFT_R, DREAM };
  static const uint8_t SEQ[] = {
    0,0,1,0,1,2,1,
    0,1,0,1,
    3,3,4,4,3,4,
    0,0,
    1,5,1,1
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xFFE0, 0);

  // Z particles drift up-right
  int p1 = (t)     % 10;
  int p2 = (t + 4) % 10;
  int p3 = (t + 7) % 10;
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 18 + p1, BUDDY_Y_OVERLAY + 18 - p1 * 2);
  ascii_print("z");
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 24 + p2, BUDDY_Y_OVERLAY + 14 - p2);
  ascii_print("Z");
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 14 + p3 / 2, BUDDY_Y_OVERLAY + 10 - p3 / 2);
  ascii_print("z");
}

// ─── IDLE ───  ~14s cycle, 10 micro-actions
static void __duck_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", "    __      ", "  <(o )___  ", "   (  ._>   ", "    `--´    " };
  static const char* const LOOK_L[5]  = { "            ", "    __      ", " <<(o )___  ", "   (  ._>   ", "    `--´    " };
  static const char* const LOOK_R[5]  = { "            ", "    __      ", "  <( o)___  ", "   (  ._>   ", "    `--´    " };
  static const char* const LOOK_U[5]  = { "    __      ", "  <(^ )     ", "  (    )___ ", "   (  ._>   ", "    `--´    " };
  static const char* const BLINK[5]   = { "            ", "    __      ", "  <(- )___  ", "   (  ._>   ", "    `--´    " };
  static const char* const QUACK[5]   = { "            ", "    __      ", "  <O(o)___  ", "   (  ._>   ", "    `--´    " };
  static const char* const PREEN_A[5] = { "            ", "    __      ", "  <(o )___  ", "   ( v.->   ", "    `--´    " };
  static const char* const PREEN_B[5] = { "            ", "    __      ", "  <(o )___  ", "   ( ^.->   ", "    `--´    " };
  static const char* const WAG_L[5]   = { "            ", "    __      ", "  <(o )___  ", "   (  ._<   ", "    `--´    " };
  static const char* const SHAKE[5]   = { "            ", "    __      ", "  <(o )___  ", "  ~(  ._>~  ", "   ~`--´~   " };

  const char* const* P[10] = { REST, LOOK_L, LOOK_R, LOOK_U, BLINK, QUACK, PREEN_A, PREEN_B, WAG_L, SHAKE };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,2,0,4,
    0,5,0,0,
    6,7,6,7,
    0,0,3,3,0,4,
    8,0,8,0,
    9,9,0,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xFFE0, 0);
}

// ─── BUSY ───  ~10s cycle, focused waddle/work + bubble ticker
static void __duck_busy(uint32_t t) {
  static const char* const PADDLE_A[5]= { "            ", "    __      ", "  <(o )___  ", "   (  ._>   ", "  ~ `--´    " };
  static const char* const PADDLE_B[5]= { "            ", "    __      ", "  <(o )___  ", "   (  ._>   ", "    `--´ ~  " };
  static const char* const DIVE_A[5]  = { "            ", "            ", "    __      ", "  <(v )_O_  ", "   ( ._>~~~ " };
  static const char* const DIVE_B[5]  = { "            ", "            ", "      _o_   ", "    ^>>     ", "  ~~~~~~~~  " };
  static const char* const SURFACE[5] = { "            ", "    __      ", "  <(O )___  ", "  *(  ._>*  ", "  ~~~~~~~~  " };
  static const char* const THINK[5]   = { "      ?     ", "    __      ", "  <(o )___  ", "   (  ._>   ", "    `--´    " };

  const char* const* P[6] = { PADDLE_A, PADDLE_B, DIVE_A, DIVE_B, SURFACE, THINK };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1, 5,5, 0,1,0,1, 2,3,3,2, 4,4, 0,1,0,1,5
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xFFE0, 0);

  // Bubble stream rising
  static const char* const BUBBLES[] = { "o  ", "oO ", "oOo", " Oo", "  o", "   " };
  ascii_set_color(BUDDY_CYAN);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(BUBBLES[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, head up alert + ! pulse
static void __duck_attention(uint32_t t) {
  static const char* const ALERT[5]   = { "    __      ", "  <(O )     ", "  (    )___ ", "   (  ._>   ", "    `--´    " };
  static const char* const SCAN_L[5]  = { "    __      ", " <<(O )     ", "  (    )___ ", "   (  ._>   ", "    `--´    " };
  static const char* const SCAN_R[5]  = { "    __      ", "  <( O)     ", "  (    )___ ", "   (  ._>   ", "    `--´    " };
  static const char* const CRANE[5]   = { "  <(O )     ", "    ||      ", "    ||      ", "   (  ._>   ", "    `--´    " };
  static const char* const TENSE[5]   = { "    __      ", " /<(O )\\    ", " /(    )___ ", "  /(  ._>\\  ", "   /`--´\\   " };
  static const char* const HONK[5]    = { "    __      ", "  <O(O )    ", "  (    )___ ", "   (  ._>   ", "    `--´    " };

  const char* const* P[6] = { ALERT, SCAN_L, SCAN_R, CRANE, TENSE, HONK };
  static const uint8_t SEQ[] = {
    0,5,0,1,0,2,0,3, 4,4,0,1,2,0, 5,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  uint8_t pose = SEQ[beat];
  int xOff = (pose == 4) ? ((t & 1) ? 1 : -1) : 0;
  ascii_print_sprite(P[pose], 5, 0, 0xFFE0, xOff);

  if ((t / 2) & 1) {
    ascii_set_color(BUDDY_YEL);
    ascii_set_cursor(BUDDY_X_CENTER - 4, BUDDY_Y_OVERLAY);
    ascii_print("!");
  }
  if ((t / 3) & 1) {
    ascii_set_color(BUDDY_YEL);
    ascii_set_cursor(BUDDY_X_CENTER + 4, BUDDY_Y_OVERLAY + 4);
    ascii_print("!");
  }
}

// ─── CELEBRATE ───  ~5.6s cycle, splash jump + confetti
static void __duck_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", "    __      ", "  <(^ )___  ", "   (  ._>   ", " /`--´\\     " };
  static const char* const JUMP[5]    = { "  \\(    )/  ", "    __      ", "  <(^ )___  ", "   (  ._>   ", "    `--´    " };
  static const char* const PEAK[5]    = { "  \\^ __ ^/  ", "   <(^ )___ ", "   (  ._>   ", "    `--´    ", "  ~~~~~~~~  " };
  static const char* const SPLASH_L[5]= { "            ", "    __      ", "  <(^ )___  ", " ~~( ._> )~ ", "  ~~`--´~~  " };
  static const char* const SPLASH_R[5]= { "            ", "    __      ", "  <(^ )___  ", "  ~~( ._>~~ ", "   ~`--´~   " };
  static const char* const POSE[5]    = { "    \\__/    ", "    __      ", "  <(^ )___  ", " /(  ._>\\   ", "    `--´    " };

  const char* const* P[6] = { CROUCH, JUMP, PEAK, SPLASH_L, SPLASH_R, POSE };
  static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t Y_SHIFT[] = { 0,-3,-6,-3,0, 0,0,0,0, 0,-3,-6,-3,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_SHIFT[beat], 0xFFE0, 0);

  static const uint16_t cols[] = { BUDDY_YEL, BUDDY_HEART, BUDDY_CYAN, BUDDY_WHITE, BUDDY_BLUE };
  for (int i = 0; i < 6; i++) {
    int phase = (t * 2 + i * 11) % 22;
    int x = BUDDY_X_CENTER - 36 + i * 14;
    int y = BUDDY_Y_OVERLAY - 6 + phase;
    if (y > BUDDY_Y_BASE + 20 || y < 0) continue;
    ascii_set_color(cols[i % 5]);
    ascii_set_cursor(x, y);
    ascii_print((i + (int)(t/2)) & 1 ? "*" : "~");
  }
}

// ─── DIZZY ───  ~5.6s cycle, woozy waddle + orbiting stars
static void __duck_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", "   __       ", " <(@ )___   ", "  (  .~>    ", "   `--´     " };
  static const char* const TILT_R[5]  = { "            ", "     __     ", "   <(@ )___ ", "    (  .~>  ", "     `--´   " };
  static const char* const WOOZY[5]   = { "            ", "    __      ", "  <(x@)___  ", "   ( ~~>    ", "    `--´    " };
  static const char* const WOOZY2[5]  = { "            ", "    __      ", "  <(@x)___  ", "   ( ~~>    ", "    `--´    " };
  static const char* const STUMBLE[5] = { "            ", "    __      ", "  <(@ )___  ", "   (  ~~>   ", " /`-_---_'\\ " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, STUMBLE };
  static const uint8_t SEQ[] = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xFFE0, X_SHIFT[beat]);

  static const int8_t OX[] = { 0, 5, 7, 5, 0, -5, -7, -5 };
  static const int8_t OY[] = { -5, -3, 0, 3, 5, 3, 0, -3 };
  uint8_t p1 = t % 8;
  uint8_t p2 = (t + 4) % 8;
  ascii_set_color(BUDDY_CYAN);
  ascii_set_cursor(BUDDY_X_CENTER + OX[p1] - 2, BUDDY_Y_OVERLAY + 6 + OY[p1]);
  ascii_print("*");
  ascii_set_color(BUDDY_YEL);
  ascii_set_cursor(BUDDY_X_CENTER + OX[p2] - 2, BUDDY_Y_OVERLAY + 6 + OY[p2]);
  ascii_print("*");
}

// ─── HEART ───  ~10s cycle, dreamy float + heart stream
static void __duck_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "            ", "    __      ", "  <(^ )___  ", "   (  ._>   ", "    `--´    " };
  static const char* const BLUSH[5]   = { "            ", "    __      ", "  <(^#)___  ", "   (  ._>   ", "    `--´    " };
  static const char* const EYES_C[5]  = { "            ", "    __      ", "  <(<3)___  ", "   (  ._>   ", "    `--´    " };
  static const char* const TWIRL[5]   = { "            ", "    __      ", "  <(@ )___  ", "   (  ._>   ", " /`--´\\     " };
  static const char* const SIGH[5]    = { "            ", "    __      ", "  <(- )___  ", "   (  ^_>   ", "    `--´    " };

  const char* const* P[5] = { DREAMY, BLUSH, EYES_C, TWIRL, SIGH };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_BOB[beat], 0xFFE0, 0);

  ascii_set_color(BUDDY_HEART);
  for (int i = 0; i < 5; i++) {
    int phase = (t + i * 4) % 16;
    int y = BUDDY_Y_OVERLAY + 16 - phase;
    if (y < -2 || y > BUDDY_Y_BASE) continue;
    int x = BUDDY_X_CENTER - 20 + i * 8 + ((phase / 3) & 1) * 2 - 1;
    ascii_set_cursor(x, y);
    ascii_print("v");
  }
}

/* ---- end persona ---- */

/* ---------------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------------- */
const ascii_persona_t PERSONA_DUCK = {
    .name = "duck",
    .states = { __duck_sleep, __duck_idle, __duck_busy, __duck_attention, __duck_celebrate, __duck_dizzy, __duck_heart },
};
