/**
 * @file persona_cat.c
 * @brief "cat" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/cat.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __cat_sleep / __cat_idle / __cat_busy / __cat_attention /
 *   __cat_celebrate / __cat_dizzy / __cat_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: cat ---- */

// ─── SLEEP ───  ~12s cycle, 6 poses (curled loaf, breathing, twitching tail)
static void __cat_sleep(uint32_t t) {
  static const char* const LOAF[5]     = { "            ", "            ", "   .-..-.   ", "  ( -.- )   ", "  `------`~ " };
  static const char* const BREATHE[5]  = { "            ", "            ", "   .-..-.   ", "  ( -.- )_  ", " `~------'~ " };
  static const char* const CURL[5]     = { "            ", "            ", "   .-/\\.    ", "  (  ..  )) ", "  `~~~~~~`  " };
  static const char* const CURL_TW[5]  = { "            ", "            ", "   .-/\\.    ", "  (  ..  )) ", "  `~~~~~~`~ " };
  static const char* const PURR[5]     = { "            ", "            ", "   .-..-.   ", "  ( u.u )   ", " `~------'~ " };
  // static const char* const DREAM[5]    = { "            ", "            ", "   .-..-.   ", "  ( o.o )   ", "  `------`  " };

  const char* const* P[6] = { LOAF, BREATHE, LOAF, PURR, CURL, CURL_TW };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1,
    3,3,0,1,
    4,5,4,5,4,5,
    2,2,
    0,1,0,1,
    5,5,4,4
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xC2A6, 0);

  // Z particles drift up-right (3 staggered streams)
  int p1 = (t)     % 12;
  int p2 = (t + 5) % 12;
  int p3 = (t + 9) % 12;
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 18 + p1, BUDDY_Y_OVERLAY + 18 - p1 * 2);
  ascii_print("z");
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 24 + p2, BUDDY_Y_OVERLAY + 14 - p2);
  ascii_print("Z");
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 14 + p3 / 2, BUDDY_Y_OVERLAY + 8 - p3 / 2);
  ascii_print("z");
}

// ─── IDLE ───  ~16s cycle, 10 poses (sassy micro-actions)
static void __cat_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const LOOK_L[5]  = { "            ", "   /\\_/\\    ", "  (o    o ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const LOOK_R[5]  = { "            ", "   /\\_/\\    ", "  ( o    o) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const BLINK[5]   = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const SLOW_BL[5] = { "            ", "   /\\-/\\    ", "  ( _   _ ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const EAR_L[5]   = { "            ", "   <\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const EAR_R[5]   = { "            ", "   /\\_/>    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const TAIL_L[5]  = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")~  " };
  static const char* const TAIL_R[5]  = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", " ~(\")_(\")   " };
  static const char* const GROOM[5]   = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  P   )  ", "  (\")_(\")   " };

  const char* const* P[10] = { REST, LOOK_L, LOOK_R, BLINK, SLOW_BL, EAR_L, EAR_R, TAIL_L, TAIL_R, GROOM };
  static const uint8_t SEQ[] = {
    0,0,0,3,0,1,0,2,0,
    7,8,7,8,7,
    0,5,0,6,0,
    4,4,0,
    9,9,9,0,
    0,3,0,
    8,7,8,7,
    0,0,4,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xC2A6, 0);
}

// ─── BUSY ───  ~10s cycle, 6 poses + dot ticker (knocking-things-off-table energy)
static void __cat_busy(uint32_t t) {
  static const char* const PAW_UP[5]  = { "      .     ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )/ ", "  (\")_(\")   " };
  static const char* const PAW_TAP[5] = { "    .       ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )_ ", "  (\")_(\")   " };
  static const char* const STARE[5]   = { "            ", "   /\\_/\\    ", "  ( O   O ) ", "  (  w   )  ", "  (\")_(\")   " };
  static const char* const NUDGE[5]   = { "    o       ", "   /\\_/\\    ", "  ( o   o ) ", "  ( -w   )  ", "  (\")_(\")   " };
  static const char* const SHOVE[5]   = { "  o         ", "   /\\_/\\    ", "  ( o   o ) ", "  (-w    )  ", "  (\")_(\")   " };
  static const char* const SMUG[5]    = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };

  const char* const* P[6] = { PAW_UP, PAW_TAP, STARE, NUDGE, SHOVE, SMUG };
  static const uint8_t SEQ[] = {
    2,2,2, 0,1,0,1, 3,4,3,4, 5,5, 2,2, 0,1,0,1, 5,2
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xC2A6, 0);

  static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(DOTS[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, 6 poses + ! pulse (ears up, dilated pupils)
static void __cat_attention(uint32_t t) {
  static const char* const ALERT[5]   = { "            ", "   /^_^\\    ", "  ( O   O ) ", "  (  v   )  ", "  (\")_(\")   " };
  static const char* const SCAN_L[5]  = { "            ", "   /^_^\\    ", "  (O    O ) ", "  (  v   )  ", "  (\")_(\")   " };
  static const char* const SCAN_R[5]  = { "            ", "   /^_^\\    ", "  ( O    O) ", "  (  v   )  ", "  (\")_(\")   " };
  static const char* const SCAN_U[5]  = { "            ", "   /^_^\\    ", "  ( ^   ^ ) ", "  (  v   )  ", "  (\")_(\")   " };
  static const char* const CROUCH[5]  = { "            ", "   /^_^\\    ", " /( O   O )\\", " (   v    ) ", " /(\")_(\")\\  " };
  static const char* const HISS[5]    = { "            ", "   /^_^\\    ", "  ( O   O ) ", "  (  >   )  ", "  (\")_(\")   " };

  const char* const* P[6] = { ALERT, SCAN_L, SCAN_R, SCAN_U, CROUCH, HISS };
  static const uint8_t SEQ[] = {
    0,4,0,1,0,2,0,3, 4,4,0,1,2,0, 5,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  uint8_t pose = SEQ[beat];
  int xOff = (pose == 4) ? ((t & 1) ? 1 : -1) : 0;
  ascii_print_sprite(P[pose], 5, 0, 0xC2A6, xOff);

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

// ─── CELEBRATE ───  ~5s cycle, 6 poses + confetti rain (zoomies)
static void __cat_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  W   )  ", " /(\")_(\")\\  " };
  static const char* const JUMP[5]    = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( ^   ^ ) ", "  (  W   )  ", "  (\")_(\")   " };
  static const char* const PEAK[5]    = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( * * * ) ", "  (  W   )  ", "  (\")_(\")~  " };
  static const char* const SPIN_L[5]  = { "            ", "   /\\_/\\    ", "  ( <   < ) ", "  (  W   ) /", " ~(\")_(\")   " };
  static const char* const SPIN_R[5]  = { "            ", "   /\\_/\\    ", "  ( >   > ) ", " \\(  W   )  ", "  (\")_(\")~  " };
  static const char* const POSE[5]    = { "    \\o/     ", "   /\\_/\\    ", "  ( ^   ^ ) ", " /(  W   )\\ ", "  (\")_(\")   " };

  const char* const* P[6] = { CROUCH, JUMP, PEAK, SPIN_L, SPIN_R, POSE };
  static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t Y_SHIFT[] = { 0,-3,-6,-3,0, 0,0,0,0, 0,-3,-6,-3,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_SHIFT[beat], 0xC2A6, 0);

  static const uint16_t cols[] = { BUDDY_YEL, BUDDY_HEART, BUDDY_CYAN, BUDDY_WHITE, BUDDY_GREEN };
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

// ─── DIZZY ───  ~6s cycle, 5 poses + orbiting stars (chasing own tail)
static void __cat_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", "  /\\_/\\     ", " ( @   @ )  ", " (   ~~  )  ", " (\")_(\")    " };
  static const char* const TILT_R[5]  = { "            ", "    /\\_/\\   ", "  ( @   @ ) ", "  (  ~~  )  ", "    (\")_(\") " };
  static const char* const WOOZY[5]   = { "            ", "   /\\_/\\    ", "  ( x   @ ) ", "  (  v   )  ", "  (\")_(\")~  " };
  static const char* const WOOZY2[5]  = { "            ", "   /\\_/\\    ", "  ( @   x ) ", "  (  v   )  ", " ~(\")_(\")   " };
  static const char* const SPLAT[5]   = { "            ", "   /\\_/\\    ", "  ( @   @ ) ", "  (  -   )  ", " /(\")_(\")\\~ " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, SPLAT };
  static const uint8_t SEQ[] = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xC2A6, X_SHIFT[beat]);

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

// ─── HEART ───  ~10s cycle, 5 poses + rising hearts (smitten purr-monster)
static void __cat_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  u   )  ", "  (\")_(\")~  " };
  static const char* const BLUSH[5]   = { "            ", "   /\\_/\\    ", "  (#^   ^#) ", "  (  u   )  ", "  (\")_(\")   " };
  static const char* const HEART_E[5] = { "            ", "   /\\_/\\    ", "  ( <3 <3 ) ", "  (  u   )  ", "  (\")_(\")~  " };
  static const char* const PURR[5]    = { "            ", "   /\\-/\\    ", "  ( ~   ~ ) ", "  (  u   )  ", " ~(\")_(\")~  " };
  static const char* const HEAD_T[5]  = { "            ", "   /\\_/\\    ", "  ( ^   - ) ", "  (  u   )  ", "  (\")_(\")   " };

  const char* const* P[5] = { DREAMY, BLUSH, HEART_E, PURR, HEAD_T };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_BOB[beat], 0xC2A6, 0);

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
const ascii_persona_t PERSONA_CAT = {
    .name = "cat",
    .states = { __cat_sleep, __cat_idle, __cat_busy, __cat_attention, __cat_celebrate, __cat_dizzy, __cat_heart },
};
