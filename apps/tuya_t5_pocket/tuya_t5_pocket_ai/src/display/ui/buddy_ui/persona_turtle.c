/**
 * @file persona_turtle.c
 * @brief "turtle" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/turtle.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __turtle_sleep / __turtle_idle / __turtle_busy / __turtle_attention /
 *   __turtle_celebrate / __turtle_dizzy / __turtle_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: turtle ---- */

// ─── SLEEP ───  ~12s cycle, 6 poses (turtle retracts into shell)
static void __turtle_sleep(uint32_t t) {
  static const char* const TUCKED[5]   = { "            ", "   _,--._   ", "  /      \\  ", " /[______]\\ ", "  ``    ``  " };
  static const char* const PEEK[5]     = { "            ", "   _,--._   ", "  (-_    -) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const SNORE_A[5]  = { "            ", "   _,--._   ", "  ( -    -) ", " /[__zz__]\\ ", "  ``    ``  " };
  static const char* const SNORE_B[5]  = { "            ", "   _,--._   ", "  ( -    -) ", " /[__ZZ__]\\ ", "  ``    ``  " };
  static const char* const CURL[5]     = { "            ", "            ", "   _,--._   ", "  ( ____  ) ", " /[______]\\ " };
  static const char* const DREAM[5]    = { "            ", "   _,--._   ", "  ( o    o) ", " /[__~~__]\\ ", "  ``    ``  " };

  const char* const* P[6] = { TUCKED, PEEK, SNORE_A, SNORE_B, CURL, DREAM };
  static const uint8_t SEQ[] = {
    0,0,0,0, 1,0,
    2,3,2,3, 2,3,2,3,
    0,0, 4,4,4,
    2,3,2,3,
    1,5,1,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);

  // Z particles drift up-right from shell
  int p1 = (t)     % 12;
  int p2 = (t + 5) % 12;
  int p3 = (t + 9) % 12;
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 18 + p1, BUDDY_Y_OVERLAY + 16 - p1 * 2);
  ascii_print("z");
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 24 + p2 / 2, BUDDY_Y_OVERLAY + 12 - p2);
  ascii_print("Z");
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 14 + p3 / 2, BUDDY_Y_OVERLAY + 8 - p3 / 2);
  ascii_print("z");
}

// ─── IDLE ───  ~14s cycle, 10 poses (slow & steady micro-actions)
static void __turtle_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", "   _,--._   ", "  ( o    o) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const LOOK_L[5]  = { "            ", "   _,--._   ", "  (o     o) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const LOOK_R[5]  = { "            ", "   _,--._   ", "  ( o     o)", " /[______]\\ ", "  ``    ``  " };
  static const char* const BLINK[5]   = { "            ", "   _,--._   ", "  ( -    -) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const PEEK_L[5]  = { "            ", "   _,--._   ", " <( o    o) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const PEEK_R[5]  = { "            ", "   _,--._   ", "  ( o    o)>", " /[______]\\ ", "  ``    ``  " };
  static const char* const CHEW[5]    = { "            ", "   _,--._   ", "  ( o    o) ", " /[______]\\ ", "  `'    `'  " };
  static const char* const STEP_A[5]  = { "            ", "   _,--._   ", "  ( o    o) ", " /[______]\\ ", "  ,'    ``  " };
  static const char* const STEP_B[5]  = { "            ", "   _,--._   ", "  ( o    o) ", " /[______]\\ ", "  ``    ',  " };
  static const char* const STRETCH[5] = { "            ", "   _,--._   ", " _( o    o)_", "/-[______]-\\", "  ``    ``  " };

  const char* const* P[10] = { REST, LOOK_L, LOOK_R, BLINK, PEEK_L, PEEK_R, CHEW, STEP_A, STEP_B, STRETCH };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,2,0,3,
    0,0,4,4,0,5,5,0,
    0,3,0,0,
    7,8,7,8,7,8,
    0,0,6,6,0,
    0,3,0,0,
    9,9,9,0,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);
}

// ─── BUSY ───  ~10s cycle, 6 poses + dot ticker (deliberate plodding)
static void __turtle_busy(uint32_t t) {
  static const char* const PUSH_A[5]  = { "            ", "   _,--._   ", "  ( v    v) ", " /[______]\\ ", "  >`    >`  " };
  static const char* const PUSH_B[5]  = { "            ", "   _,--._   ", "  ( v    v) ", " /[______]\\ ", "  `>    `>  " };
  static const char* const THINK[5]   = { "      ?     ", "   _,--._   ", "  ( ^    ^) ", " /[__..__]\\ ", "  ``    ``  " };
  static const char* const CARRY[5]   = { "    [#]     ", "   _,-#-._  ", "  ( o    o) ", " /[______]\\ ", "  ``    ``  " };
  static const char* const EUREKA[5]  = { "      *     ", "   _,--._   ", "  ( O    O) ", " /[__^^__]\\ ", "  >`    >`  " };
  static const char* const PUFF[5]    = { "    ~~~     ", "   _,--._   ", "  ( -    -) ", " /[______]\\ ", "  ``    ``  " };

  const char* const* P[6] = { PUSH_A, PUSH_B, THINK, CARRY, EUREKA, PUFF };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1, 2,2, 0,1,0,1, 3,3,3, 2,4, 0,1,0,1, 5
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);

  static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(DOTS[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, 6 poses + ! pulse (head fully extended, scanning)
static void __turtle_attention(uint32_t t) {
  static const char* const ALERT[5]   = { "            ", "   _,--._   ", "  ( O    O) ", " /[__||__]\\ ", "  ``    ``  " };
  static const char* const SCAN_L[5]  = { "            ", "   _,--._   ", " <(O     O) ", " /[__||__]\\ ", "  ``    ``  " };
  static const char* const SCAN_R[5]  = { "            ", "   _,--._   ", "  ( O     O)>", " /[__||__]\\ ", "  ``    ``  " };
  static const char* const SCAN_U[5]  = { "    ^  ^    ", "   _,--._   ", "  ( ^    ^) ", " /[__||__]\\ ", "  ``    ``  " };
  static const char* const TENSE[5]   = { "            ", "  /^_,--._^\\", "  ( O    O) ", " /[__||__]\\ ", " /``    ``\\ " };
  static const char* const HISS[5]    = { "            ", "   _,--._   ", "  ( o    o) ", " /[__><__]\\ ", "  ``    ``  " };

  const char* const* P[6] = { ALERT, SCAN_L, SCAN_R, SCAN_U, TENSE, HISS };
  static const uint8_t SEQ[] = {
    0,4,0,1,0,2,0,3, 4,4,0,1,2,0, 5,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  uint8_t pose = SEQ[beat];
  int xOff = (pose == 4) ? ((t & 1) ? 1 : -1) : 0;
  ascii_print_sprite(P[pose], 5, 0, 0x07E0, xOff);

  if ((t / 2) & 1) {
    ascii_set_color(BUDDY_YEL);
    ascii_set_cursor(BUDDY_X_CENTER - 4, BUDDY_Y_OVERLAY);
    ascii_print("!");
  }
  if ((t / 3) & 1) {
    ascii_set_color(BUDDY_YEL);
    ascii_set_cursor(BUDDY_X_CENTER + 6, BUDDY_Y_OVERLAY + 4);
    ascii_print("!");
  }
}

// ─── CELEBRATE ───  ~5.6s cycle, 6 poses + confetti rain (tiny but determined hops)
static void __turtle_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", "   _,--._   ", "  ( ^    ^) ", " /[__ww__]\\ ", " /``    ``\\ " };
  static const char* const HOP[5]     = { "  \\(    )/  ", "   _,--._   ", "  ( ^    ^) ", " /[__ww__]\\ ", "  ``    ``  " };
  static const char* const PEAK[5]    = { "  \\^    ^/  ", "   _,--._   ", "  ( ^    ^) ", " /[__WW__]\\ ", "  ``    ``  " };
  static const char* const FLIP_L[5]  = { "            ", "   .--._    ", "  ( ^    ^) ", "/[==_v_==]\\ ", "  ``    ``  " };
  static const char* const FLIP_R[5]  = { "            ", "    _,--.   ", "  ( ^    ^) ", " /[==_v_==]\\", "  ``    ``  " };
  static const char* const POSE[5]    = { "    \\__/    ", "   _,--._   ", "  ( ^    ^) ", "/[__WW__]\\  ", " /``    ``\\ " };

  const char* const* P[6] = { CROUCH, HOP, PEAK, FLIP_L, FLIP_R, POSE };
  static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t Y_SHIFT[] = { 0,-3,-6,-3,0, 0,0,0,0, 0,-3,-6,-3,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_SHIFT[beat], 0x07E0, 0);

  static const uint16_t cols[] = { BUDDY_YEL, BUDDY_HEART, BUDDY_CYAN, BUDDY_WHITE, BUDDY_PURPLE };
  for (int i = 0; i < 6; i++) {
    int phase = (t * 2 + i * 11) % 22;
    int x = BUDDY_X_CENTER - 36 + i * 14;
    int y = BUDDY_Y_OVERLAY - 6 + phase;
    if (y > BUDDY_Y_BASE + 20 || y < 0) continue;
    ascii_set_color(cols[i % 5]);
    ascii_set_cursor(x, y);
    ascii_print((i + (int)(t/2)) & 1 ? "*" : ".");
  }
}

// ─── DIZZY ───  ~5.6s cycle, 5 poses + orbiting stars (shell wobble)
static void __turtle_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", "  _,--._    ", " ( @    @)  ", "/[__~~__]\\  ", " ``    ``   " };
  static const char* const TILT_R[5]  = { "            ", "    _,--._  ", "  ( @    @) ", "  /[__~~__]\\", "   ``    `` " };
  static const char* const WOOZY[5]   = { "            ", "   _,--._   ", "  ( x    @) ", " /[__~v__]\\ ", "  ``    ``  " };
  static const char* const WOOZY2[5]  = { "            ", "   _,--._   ", "  ( @    x) ", " /[__v~__]\\ ", "  ``    ``  " };
  static const char* const FLIP[5]    = { "            ", "  ``    ``  ", " \\[__~~__]/ ", "  ( @    @) ", "   `--''   " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, FLIP };
  static const uint8_t SEQ[] = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, X_SHIFT[beat]);

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

// ─── HEART ───  ~10s cycle, 5 poses + rising heart stream (dreamy turtle blush)
static void __turtle_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "            ", "   _,--._   ", "  ( ^    ^) ", " /[__ww__]\\ ", "  ``    ``  " };
  static const char* const BLUSH[5]   = { "            ", "   _,--._   ", "  (#^    ^#)", " /[__ww__]\\ ", "  ``    ``  " };
  static const char* const EYES_C[5]  = { "            ", "   _,--._   ", "  (<3    <3)", " /[__ww__]\\ ", "  ``    ``  " };
  static const char* const SWAY[5]    = { "            ", "   _,--._   ", "  ( @    @) ", " /[__ww__]\\ ", " /``    ``\\ " };
  static const char* const SIGH[5]    = { "            ", "   _,--._   ", "  ( -    -) ", " /[__^^__]\\ ", "  ``    ``  " };

  const char* const* P[5] = { DREAMY, BLUSH, EYES_C, SWAY, SIGH };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_BOB[beat], 0x07E0, 0);

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
const ascii_persona_t PERSONA_TURTLE = {
    .name = "turtle",
    .states = { __turtle_sleep, __turtle_idle, __turtle_busy, __turtle_attention, __turtle_celebrate, __turtle_dizzy, __turtle_heart },
};
