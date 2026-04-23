/**
 * @file persona_owl.c
 * @brief "owl" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/owl.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __owl_sleep / __owl_idle / __owl_busy / __owl_attention /
 *   __owl_celebrate / __owl_dizzy / __owl_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: owl ---- */

// ─── SLEEP ───  ~12s cycle, 6 poses
static void __owl_sleep(uint32_t t) {
  static const char* const TUCK[5]    = { "            ", "   .-..-.   ", "  ( -  - )  ", "  (  __  )  ", "   `----'   " };
  static const char* const PUFF[5]    = { "    .--.    ", "   /-..-\\   ", "  ( -  - )  ", "  (  __  )  ", "   `----'   " };
  static const char* const DEEP[5]    = { "    .--.    ", "   /-..-\\   ", "  ( _  _ )  ", "  (  ZZ  )  ", "   `----'   " };
  static const char* const HEAD_L[5]  = { "            ", "   .-..-.   ", " ( -  -  )  ", "  (  __  )  ", "   `----'   " };
  static const char* const HEAD_R[5]  = { "            ", "   .-..-.   ", "  (  - - -) ", "  (  __  )  ", "   `----'   " };
  static const char* const HOOT[5]    = { "      hoo   ", "   .-..-.   ", "  ( -  - )  ", "  (  oo  )  ", "   `----'   " };

  const char* const* P[6] = { TUCK, PUFF, DEEP, HEAD_L, HEAD_R, HOOT };
  static const uint8_t SEQ[] = {
    0,1,0,1,2,2,
    1,0,1,0,
    3,3,0,4,4,0,
    1,2,2,1,
    0,5,0,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x8430, 0);

  // Z particles drift up-right (slow, sleepy)
  int p1 = (t)     % 12;
  int p2 = (t + 5) % 12;
  int p3 = (t + 9) % 12;
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 18 + p1, BUDDY_Y_OVERLAY + 20 - p1 * 2);
  ascii_print("z");
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 24 + p2, BUDDY_Y_OVERLAY + 16 - p2);
  ascii_print("Z");
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 14 + p3 / 2, BUDDY_Y_OVERLAY + 12 - p3 / 2);
  ascii_print("z");
}

// ─── IDLE ───  ~14s cycle, 10 poses
static void __owl_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", "   /\\  /\\   ", "  ((O)(O))  ", "  (  ><  )  ", "   `----'   " };
  static const char* const BLINK[5]   = { "            ", "   /\\  /\\   ", "  ((-)(-))  ", "  (  ><  )  ", "   `----'   " };
  static const char* const LOOK_L[5]  = { "            ", "   /\\  /\\   ", "  ((O)(O))  ", " (  ><   )  ", "   `----'   " };
  static const char* const LOOK_R[5]  = { "            ", "   /\\  /\\   ", "  ((O)(O))  ", "  (   >< ) ", "   `----'   " };
  static const char* const HEAD_L[5]  = { "            ", "  /\\  /\\    ", " ((O)(O))   ", "  (  ><  )  ", "   `----'   " };
  static const char* const HEAD_R[5]  = { "            ", "    /\\  /\\  ", "   ((O)(O)) ", "  (  ><  )  ", "   `----'   " };
  static const char* const WINK[5]    = { "            ", "   /\\  /\\   ", "  ((O)(-))  ", "  (  ><  )  ", "   `----'   " };
  static const char* const PREEN[5]   = { "            ", "   /\\  /\\   ", "  ((o)(o))  ", "  (  v<  )  ", "   `----'   " };
  static const char* const PUFF[5]    = { "            ", "  /^\\  /^\\  ", " (((O)(O))) ", " (  ><  )   ", "  `------'  " };
  static const char* const HOOT[5]    = { "      hoo   ", "   /\\  /\\   ", "  ((O)(O))  ", "  (  oo  )  ", "   `----'   " };

  const char* const* P[10] = { REST, BLINK, LOOK_L, LOOK_R, HEAD_L, HEAD_R, WINK, PREEN, PUFF, HOOT };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,0,
    2,2,0,3,3,0,
    1,0,
    4,4,0,5,5,0,
    6,0,
    7,7,0,
    8,8,0,
    9,9,0,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x8430, 0);
}

// ─── BUSY ───  ~10s cycle, 6 poses + dot ticker
static void __owl_busy(uint32_t t) {
  static const char* const SCROLL[5]  = { "    [___]   ", "   /\\  /\\   ", "  ((v)(v))  ", "  (  --  )  ", "   `----'   " };
  static const char* const PECK_A[5]  = { "    [___]   ", "   /\\  /\\   ", "  ((v)(v))  ", "  (  >>  )  ", "   `----'   " };
  static const char* const PECK_B[5]  = { "    [___]   ", "   /\\  /\\   ", "  ((v)(v))  ", "  (  <<  )  ", "   `----'   " };
  static const char* const PONDER[5]  = { "      ?     ", "   /\\  /\\   ", "  ((^)(^))  ", "  (  ..  )  ", "   `----'   " };
  static const char* const EUREKA[5]  = { "      *     ", "   /\\  /\\   ", "  ((O)(O))  ", "  (  ^^  )  ", "  /`----'\\  " };
  static const char* const SCROLL2[5] = { "   [____]   ", "  /\\   /\\   ", " ((v) (v))  ", "  (  --  )  ", "   `----'   " };

  const char* const* P[6] = { SCROLL, PECK_A, PECK_B, PONDER, EUREKA, SCROLL2 };
  static const uint8_t SEQ[] = {
    0,1,0,2,0,1,0,2, 3,3, 0,1,0,2, 5,5,0,1, 4,4, 0,2,0,1,3
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x8430, 0);

  static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
  ascii_set_color(BUDDY_CYAN);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(DOTS[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, 6 poses + ! pulse
static void __owl_attention(uint32_t t) {
  static const char* const ALERT[5]   = { "            ", "  /^\\  /^\\  ", " ((O))((O)) ", " (   ><   ) ", "  `------'  " };
  static const char* const SCAN_L[5]  = { "            ", "  /^\\  /^\\  ", "((O))((O))  ", " (   ><   ) ", "  `------'  " };
  static const char* const SCAN_R[5]  = { "            ", "  /^\\  /^\\  ", "  ((O))((O))", " (   ><   ) ", "  `------'  " };
  static const char* const HEAD_T[5]  = { "            ", " //^\\\\//^\\\\ ", " ((O))((O)) ", " (   ><   ) ", "  `------'  " };
  static const char* const TENSE[5]   = { "            ", " /^^\\/^^\\   ", "((O))((O))  ", "((   ><   ))", " /`------'\\ " };
  static const char* const GLARE[5]   = { "            ", "  /^\\  /^\\  ", " ((-))((-)) ", " (   ><   ) ", "  `------'  " };

  const char* const* P[6] = { ALERT, SCAN_L, SCAN_R, HEAD_T, TENSE, GLARE };
  static const uint8_t SEQ[] = {
    0,4,0,1,0,2,0,3, 4,4,0,1,2,0, 5,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  uint8_t pose = SEQ[beat];
  int xOff = (pose == 4) ? ((t & 1) ? 1 : -1) : 0;
  ascii_print_sprite(P[pose], 5, 0, 0x8430, xOff);

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

// ─── CELEBRATE ───  ~5.6s cycle, 6 poses + confetti rain
static void __owl_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", "   /\\  /\\   ", "  ((^)(^))  ", "  (  ww  )  ", "  /`----'\\  " };
  static const char* const FLAP_U[5]  = { "  </    \\>  ", "   /\\  /\\   ", "  ((^)(^))  ", "  (  ww  )  ", "   `----'   " };
  static const char* const FLAP_D[5]  = { "            ", " \\_/\\  /\\_/ ", "  ((^)(^))  ", "  (  WW  )  ", "   `----'   " };
  static const char* const SPIN_L[5]  = { "            ", " </\\  /\\    ", "((<)(<))    ", "  (  ww  )  ", "   `----'   " };
  static const char* const SPIN_R[5]  = { "            ", "    /\\  /\\> ", "    ((>)(>))", "  (  ww  )  ", "   `----'   " };
  static const char* const POSE[5]    = { "    \\__/    ", "  </\\  /\\>  ", " ((^)(^))   ", " /(  WW  )\\ ", "   `----'   " };

  const char* const* P[6] = { CROUCH, FLAP_U, FLAP_D, SPIN_L, SPIN_R, POSE };
  static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t Y_SHIFT[] = { 0,-4,-7,-4,0, 0,0,0,0, 0,-4,-7,-4,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_SHIFT[beat], 0x8430, 0);

  static const uint16_t cols[] = { BUDDY_YEL, BUDDY_HEART, BUDDY_CYAN, BUDDY_WHITE, BUDDY_GREEN };
  for (int i = 0; i < 6; i++) {
    int phase = (t * 2 + i * 11) % 22;
    int x = BUDDY_X_CENTER - 36 + i * 14;
    int y = BUDDY_Y_OVERLAY - 6 + phase;
    if (y > BUDDY_Y_BASE + 20 || y < 0) continue;
    ascii_set_color(cols[i % 5]);
    ascii_set_cursor(x, y);
    ascii_print((i + (int)(t/2)) & 1 ? "*" : "o");
  }
}

// ─── DIZZY ───  ~5.6s cycle, 5 poses + orbiting stars
static void __owl_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", "  /\\  /\\    ", " ((@)(@))   ", "  (  ~~  )  ", "   `----'   " };
  static const char* const TILT_R[5]  = { "            ", "    /\\  /\\  ", "   ((@)(@)) ", "  (  ~~  )  ", "   `----'   " };
  static const char* const WOOZY[5]   = { "            ", "   /\\  /\\   ", "  ((x)(@))  ", "  (  ~v  )  ", "   `----'   " };
  static const char* const WOOZY2[5]  = { "            ", "   /\\  /\\   ", "  ((@)(x))  ", "  (  v~  )  ", "   `----'   " };
  static const char* const SPIN360[5] = { "            ", "   \\/  \\/   ", "  ((@)(@))  ", "  (  --  )  ", "  /`-_--'\\  " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, SPIN360 };
  static const uint8_t SEQ[] = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x8430, X_SHIFT[beat]);

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

// ─── HEART ───  ~10s cycle, 5 poses + rising heart stream
static void __owl_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "            ", "   /\\  /\\   ", "  ((^)(^))  ", "  (  ww  )  ", "   `----'   " };
  static const char* const BLUSH[5]   = { "            ", "   /\\  /\\   ", " #((^)(^))# ", "  (  ww  )  ", "   `----'   " };
  static const char* const HEART_E[5] = { "            ", "   /\\  /\\   ", "  ((<3)(<3))", "  (  ww  )  ", "   `----'   " };
  static const char* const TWIRL[5]   = { "            ", "   /\\  /\\   ", "  ((@)(@))  ", "  (  ww  )  ", "  /`----'\\  " };
  static const char* const SIGH[5]    = { "      hoo   ", "   /\\  /\\   ", "  ((-)(-))  ", "  (  vv  )  ", "   `----'   " };

  const char* const* P[5] = { DREAMY, BLUSH, HEART_E, TWIRL, SIGH };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_BOB[beat], 0x8430, 0);

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
const ascii_persona_t PERSONA_OWL = {
    .name = "owl",
    .states = { __owl_sleep, __owl_idle, __owl_busy, __owl_attention, __owl_celebrate, __owl_dizzy, __owl_heart },
};
