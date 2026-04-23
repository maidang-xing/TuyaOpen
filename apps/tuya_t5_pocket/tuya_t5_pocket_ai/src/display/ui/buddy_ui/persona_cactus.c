/**
 * @file persona_cactus.c
 * @brief "cactus" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/cactus.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __cactus_sleep / __cactus_idle / __cactus_busy / __cactus_attention /
 *   __cactus_celebrate / __cactus_dizzy / __cactus_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: cactus ---- */

// ─── SLEEP ───  ~12s cycle, 6 poses
static void __cactus_sleep(uint32_t t) {
  static const char* const DROOP[5]   = { "            ", "    ____    ", "   |--  |   ", "   |_  _|   ", "    |  |    " };
  static const char* const BREATHE[5] = { "            ", "    ____    ", " . |--  |.  ", "   |_  _|   ", "    |  |    " };
  static const char* const SAG_L[5]   = { "            ", "    ____    ", " \\ |--  |   ", "  \\|_  _|   ", "    |  |    " };
  static const char* const SAG_R[5]   = { "            ", "    ____    ", "   |--  | / ", "   |_  _|/  ", "    |  |    " };
  static const char* const SNORE[5]   = { "            ", "    ____    ", "   |~~  |   ", "   |_  _|   ", "   ~|  |~   " };
  static const char* const DEEP[5]    = { "            ", "            ", "    ____    ", "   |--  |   ", "   |_  _|   " };

  const char* const* P[6] = { DROOP, BREATHE, SNORE, SAG_L, SAG_R, DEEP };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1,2,1,
    0,1,0,1,
    3,3,4,4,3,4,
    0,0,
    1,5,1,1
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);

  // Z particles drift up-right
  int p1 = (t)     % 10;
  int p2 = (t + 4) % 10;
  int p3 = (t + 7) % 10;
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 20 + p1, BUDDY_Y_OVERLAY + 18 - p1 * 2);
  ascii_print("z");
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 26 + p2, BUDDY_Y_OVERLAY + 14 - p2);
  ascii_print("Z");
  ascii_set_color(BUDDY_DIM);
  ascii_set_cursor(BUDDY_X_CENTER + 16 + p3 / 2, BUDDY_Y_OVERLAY + 10 - p3 / 2);
  ascii_print("z");
}

// ─── IDLE ───  ~14s cycle, 10 poses
static void __cactus_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", " n  ____  n ", " | |o  o| | ", " |_|    |_| ", "   |    |   " };
  static const char* const LOOK_L[5]  = { "            ", " n  ____  n ", " | |o   o| ", " |_|    |_| ", "   |    |   " };
  static const char* const LOOK_R[5]  = { "            ", " n  ____  n ", " | | o  o| ", " |_|    |_| ", "   |    |   " };
  static const char* const LOOK_U[5]  = { "            ", " n  ____  n ", " | |^  ^| | ", " |_|    |_| ", "   |    |   " };
  static const char* const BLINK[5]   = { "            ", " n  ____  n ", " | |-  -| | ", " |_|    |_| ", "   |    |   " };
  static const char* const WAVE_L[5]  = { "  \\         ", "   \\____  n ", " n |o  o| | ", " |_|    |_| ", "   |    |   " };
  static const char* const WAVE_R[5]  = { "         /  ", " n  ____/   ", " | |o  o| n ", " |_|    |_| ", "   |    |   " };
  static const char* const SWAY_L[5]  = { "            ", "n   ____  n ", "| |o  o| |  ", " |_|    |_| ", "   |    |   " };
  static const char* const SWAY_R[5]  = { "            ", " n  ____   n", " | |o  o| | ", " |_|    |_|", "    |    |  " };
  static const char* const HUM[5]     = { "      o     ", " n  ____  n ", " | |- -| | ", " |_|  o |_| ", "   |    |   " };

  const char* const* P[10] = { REST, LOOK_L, LOOK_R, LOOK_U, BLINK, WAVE_L, WAVE_R, SWAY_L, SWAY_R, HUM };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,2,0,4,
    0,3,0,0,
    5,5,0,6,6,
    0,0,7,8,7,8,0,
    9,9,0,4,
    0,0,1,2,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);
}

// ─── BUSY ───  ~10s cycle, 6 poses + sap-drip ticker
static void __cactus_busy(uint32_t t) {
  static const char* const FOCUS[5]   = { "            ", " n  ____  n ", " | |v  v| | ", " |_|  --|_| ", "   |    |   " };
  static const char* const SQUINT[5]  = { "            ", " n  ____  n ", " | |- -| | ", " |_|  __|_| ", "   |    |   " };
  static const char* const THINK[5]   = { "      ?     ", " n  ____  n ", " | |^  ^| | ", " |_|  ..|_| ", "   |    |   " };
  static const char* const SCRIBE[5]  = { "            ", " n  ____  /=", " | |o  o|/  ", " |_|  --|_| ", "   |    |   " };
  static const char* const EUREKA[5]  = { "      *     ", " n  ____  n ", " | |O  O| | ", " |_|  ^^|_| ", "   |    |   " };
  static const char* const RECHECK[5] = { "      .     ", " n  ____  n ", " | |o  o| | ", " |_|  --|_| ", "   |    |   " };

  const char* const* P[6] = { FOCUS, SQUINT, THINK, SCRIBE, EUREKA, RECHECK };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1, 2,2, 0,1,0,1, 3,3, 2,4, 0,1,0,1,5
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0x07E0, 0);

  static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
  ascii_set_color(BUDDY_GREEN);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(DOTS[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, 6 poses + ! pulse + bristling spines
static void __cactus_attention(uint32_t t) {
  static const char* const BRISTLE[5] = { " *  ____  * ", " n *|O  O|* n", " |*|    |*| ", " |_|    |_| ", "  *|    |*  " };
  static const char* const SCAN_L[5]  = { " *  ____  * ", " n *|O  O|* n", " |*|O    | ", " |_|    |_| ", "  *|    |*  " };
  static const char* const SCAN_R[5]  = { " *  ____  * ", " n *|O  O|* n", " |*|    O|*|", " |_|    |_| ", "  *|    |*  " };
  static const char* const TALL[5]    = { " *  ^^^^  * ", " n *|O  O|* n", " |*|    |*| ", " |_|    |_| ", "  *|    |*  " };
  static const char* const SHARP[5]   = { "*** **** ***", "*n*|O  O|*n*", "*|*|    |*|*", "*|_|    |_|*", " *|    |* " };
  static const char* const HUSH[5]    = { " *  ____  * ", " n *|o  o|* n", " |*|    |*| ", " |_|  . |_| ", "  *|    |*  " };

  const char* const* P[6] = { BRISTLE, SCAN_L, SCAN_R, TALL, SHARP, HUSH };
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
    ascii_set_cursor(BUDDY_X_CENTER + 4, BUDDY_Y_OVERLAY + 4);
    ascii_print("!");
  }
}

// ─── CELEBRATE ───  ~5.6s cycle, 6 poses + flower bloom + confetti
static void __cactus_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", " n  ____  n ", " | |^  ^| | ", " |_|  ww|_| ", "  /|    |\\  " };
  static const char* const HOP[5]     = { "    .--.    ", " \\  ____  / ", "  \\|^  ^|/  ", " |_|  ww|_| ", "   |    |   " };
  static const char* const PEAK[5]    = { "    (**)    ", "  \\ ____ /  ", " \\ |^  ^| / ", "  ||  ww||  ", "   |    |   " };
  static const char* const SPIN_L[5]  = { "            ", "<n  ____  n ", " | |<  <| | ", " |_|    |_| ", "  /|    |   " };
  static const char* const SPIN_R[5]  = { "            ", " n  ____  n>", " | |>  >| | ", " |_|    |_| ", "   |    |\\  " };
  static const char* const BLOOM[5]   = { "    @--@    ", " n  \\__/  n ", " | |^  ^| | ", " |_|  WW|_| ", "  /|    |\\  " };

  const char* const* P[6] = { CROUCH, HOP, PEAK, SPIN_L, SPIN_R, BLOOM };
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
    ascii_print((i + (int)(t/2)) & 1 ? "*" : "o");
  }
}

// ─── DIZZY ───  ~5.6s cycle, 5 poses + orbiting stars + spine wobble
static void __cactus_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", "n  ____   n ", "| |@  @|  | ", " |_|~~  |_| ", "   |    |   " };
  static const char* const TILT_R[5]  = { "            ", " n   ____  n", " |  |@  @| |", " |_|  ~~|_| ", "   |    |   " };
  static const char* const WOOZY[5]   = { "            ", " n  ____  n ", " | |x  @| | ", " |_|  ~v|_| ", "   /    \\   " };
  static const char* const WOOZY2[5]  = { "            ", " n  ____  n ", " | |@  x| | ", " |_|  v~|_| ", "   \\    /   " };
  static const char* const STUMBLE[5] = { "            ", " n  ____  n ", " | |@  @| | ", " |_|  --|_| ", "  /-|  |-\\  " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, STUMBLE };
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

// ─── HEART ───  ~10s cycle, 5 poses + rising heart stream + flower
static void __cactus_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "    @       ", " n  ____  n ", " | |^  ^| | ", " |_|  ww|_| ", "   |    |   " };
  static const char* const BLUSH[5]   = { "    @       ", " n  ____  n ", " |#|^  ^|#| ", " |_|  ww|_| ", "   |    |   " };
  static const char* const EYES_C[5]  = { "    @--@    ", " n  \\__/  n ", " | |<3<3| | ", " |_|  ww|_| ", "   |    |   " };
  static const char* const TWIRL[5]   = { "       @    ", " n  ____  n ", " | |@  @| | ", " |_|  ww|_| ", "  /|    |\\  " };
  static const char* const SIGH[5]    = { "    @       ", " n  ____  n ", " | |- -| | ", " |_|  ^^|_| ", "   |    |   " };

  const char* const* P[5] = { DREAMY, BLUSH, EYES_C, TWIRL, SIGH };
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
const ascii_persona_t PERSONA_CACTUS = {
    .name = "cactus",
    .states = { __cactus_sleep, __cactus_idle, __cactus_busy, __cactus_attention, __cactus_celebrate, __cactus_dizzy, __cactus_heart },
};
