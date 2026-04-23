/**
 * @file persona_mushroom.c
 * @brief "mushroom" ASCII 人格状态机。
 *
 * 由 tools/port_buddies.py 从 claude-desktop-buddy/src/buddies/mushroom.cpp
 * 机械化迁移而来。为保持与上游 1:1 对应，sprite 数据、tick 序列与 overlay
 * 布局一律保留；RGB565 颜色参数在单色 OLED 上被实现忽略。
 *
 * 七个状态函数（对应 buddy_persona_state_e）：
 *   __mushroom_sleep / __mushroom_idle / __mushroom_busy / __mushroom_attention /
 *   __mushroom_celebrate / __mushroom_dizzy / __mushroom_heart
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project (port)
 * @copyright Copyright (c) claude-desktop-buddy authors (frame data)
 */

#include "ascii_persona.h"
#include <stdint.h>

/* ---- persona: mushroom ---- */

// ─── SLEEP ───  ~12s cycle, 6 poses
static void __mushroom_sleep(uint32_t t) {
  static const char* const TUCK[5]    = { "            ", " .-o-OO-o-. ", "(__________)", "   |-  - |  ", "   |____|   " };
  static const char* const BREATHE[5] = { "            ", " .-O-oo-O-. ", "(__________)", "   |-  - |  ", "   |____|   " };
  static const char* const DEEP[5]    = { "            ", " .-o-oo-o-. ", "(__________)", "   |_  _ |  ", "   |____|   " };
  static const char* const LEAN[5]    = { "            ", "  .-o-OO-o-.", "  (________)", "    |-  -| /", "    |____|  " };
  static const char* const LEAN_Z[5]  = { "            ", "  .-o-OO-o-.", "  (________)", "    |z  z| /", "    |____|  " };
  static const char* const MUMBLE[5]  = { "            ", " .-O-OO-O-. ", "(__________)", "   |o  o |  ", "   |____|   " };

  const char* const* P[6] = { TUCK, BREATHE, DEEP, LEAN, LEAN_Z, MUMBLE };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1,2,1,
    0,1,0,1,
    3,4,3,4,3,4,
    3,3,
    1,5,1,1
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xF810, 0);

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
static void __mushroom_idle(uint32_t t) {
  static const char* const REST[5]    = { "            ", " .-o-OO-o-. ", "(__________)", "   |o   o|  ", "   |____|   " };
  static const char* const LOOK_L[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |o  o |  ", "   |____|   " };
  static const char* const LOOK_R[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   | o  o|  ", "   |____|   " };
  static const char* const LOOK_U[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const BLINK[5]   = { "            ", " .-o-OO-o-. ", "(__________)", "   |-   -|  ", "   |____|   " };
  static const char* const CAP_BOB[5] = { "            ", " .-O-oo-O-. ", "(__________)", "   |o   o|  ", "   |____|   " };
  static const char* const SPORE_A[5] = { "  . o .  .  ", " .-o-OO-o-. ", "(__________)", "   |o   o|  ", "   |____|   " };
  static const char* const SPORE_B[5] = { " o  .  o .  ", " .-O-oo-O-. ", "(__________)", "   |o   o|  ", "   |____|   " };
  static const char* const SHIVER[5]  = { "            ", " .~o-OO-o~. ", "(~~~~~~~~~~)", "   |o   o|  ", "   |____|   " };
  static const char* const SWAY[5]    = { "            ", "  .-o-OO-o-.", "  (________)", "   |o   o|  ", "   |____|   " };

  const char* const* P[10] = { REST, LOOK_L, LOOK_R, LOOK_U, BLINK, CAP_BOB, SPORE_A, SPORE_B, SHIVER, SWAY };
  static const uint8_t SEQ[] = {
    0,0,0,1,0,2,0,4,
    0,5,0,0,
    6,7,6,7,
    0,0,3,3,0,4,
    8,8,0,0,
    9,9,0,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xF810, 0);
}

// ─── BUSY ───  ~10s cycle, 6 poses + dot ticker
static void __mushroom_busy(uint32_t t) {
  static const char* const SCAN_A[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |v   v|  ", "  /|____|\\  " };
  static const char* const SCAN_B[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |v   v|  ", "  \\|____|/  " };
  static const char* const THINK[5]   = { "      ?     ", " .-O-oo-O-. ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const READ[5]    = { "    [_]     ", " .-o-OO-o|. ", "(________|_)", "   |o   o|  ", "   |____|   " };
  static const char* const AHA[5]     = { "      *     ", " .-O-OO-O-. ", "(__________)", "   |O   O|  ", "  /|____|\\  " };
  static const char* const PHEW[5]    = { "    ~~~     ", " .-o-OO-o-. ", "(__________)", "   |-   -|  ", "   |____|   " };

  const char* const* P[6] = { SCAN_A, SCAN_B, THINK, READ, AHA, PHEW };
  static const uint8_t SEQ[] = {
    0,1,0,1,0,1, 2,2, 0,1,0,1, 3,3, 2,4, 0,1,0,1,5
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xF810, 0);

  static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
  ascii_set_color(BUDDY_WHITE);
  ascii_set_cursor(BUDDY_X_CENTER + 22, BUDDY_Y_OVERLAY + 14);
  ascii_print(DOTS[t % 6]);
}

// ─── ATTENTION ───  ~8s cycle, 6 poses + ! pulse
static void __mushroom_attention(uint32_t t) {
  static const char* const ALERT[5]   = { "            ", " /^o-OO-o^\\ ", "(__________)", "   |O   O|  ", "   |____|   " };
  static const char* const SCAN_L[5]  = { "            ", " /^o-OO-o^\\ ", "(__________)", "   |O  O |  ", "   |____|   " };
  static const char* const SCAN_R[5]  = { "            ", " /^o-OO-o^\\ ", "(__________)", "   | O  O|  ", "   |____|   " };
  static const char* const SCAN_U[5]  = { "            ", " /^o-OO-o^\\ ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const TENSE[5]   = { "    ^  ^    ", "/^^o-OO-o^^\\", "(__________)", "   |O   O|  ", "  /|____|\\  " };
  static const char* const HUSH[5]    = { "            ", " /^o-OO-o^\\ ", "(__________)", "   |o   o|  ", "   |____|   " };

  const char* const* P[6] = { ALERT, SCAN_L, SCAN_R, SCAN_U, TENSE, HUSH };
  static const uint8_t SEQ[] = {
    0,4,0,1,0,2,0,3, 4,4,0,1,2,0, 5,0
  };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  uint8_t pose = SEQ[beat];
  int xOff = (pose == 4) ? ((t & 1) ? 1 : -1) : 0;
  ascii_print_sprite(P[pose], 5, 0, 0xF810, xOff);

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

// ─── CELEBRATE ───  ~5.6s cycle, 6 poses + confetti rain
static void __mushroom_celebrate(uint32_t t) {
  static const char* const CROUCH[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |^   ^|  ", "  /|____|\\  " };
  static const char* const JUMP[5]    = { "  \\(    )/  ", " .-o-OO-o-. ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const PEAK[5]    = { "  \\^    ^/  ", " .-O-OO-O-. ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const SPIN_L[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "  <|<   <|  ", "   |____|   " };
  static const char* const SPIN_R[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |>   >|> ", "   |____|   " };
  static const char* const POSE[5]    = { "    \\__/    ", " .-O-OO-O-. ", "(__________)", "   |^   ^|  ", " /|____|\\   " };

  const char* const* P[6] = { CROUCH, JUMP, PEAK, SPIN_L, SPIN_R, POSE };
  static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
  static const int8_t Y_SHIFT[] = { 0,-3,-6,-3,0, 0,0,0,0, 0,-3,-6,-3,0, 0,0 };
  uint8_t beat = (t / 3) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_SHIFT[beat], 0xF810, 0);

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

// ─── DIZZY ───  ~5.6s cycle, 5 poses + orbiting stars
static void __mushroom_dizzy(uint32_t t) {
  static const char* const TILT_L[5]  = { "            ", ".-o-OO-o-.  ", "(________)  ", "  |@   @|   ", "  |____|    " };
  static const char* const TILT_R[5]  = { "            ", "  .-o-OO-o-.", "  (________)", "    |@   @| ", "    |____|  " };
  static const char* const WOOZY[5]   = { "            ", " .~o-OO-o~. ", "(~~~~~~~~~~)", "   |x   @|  ", "   |~v~~|   " };
  static const char* const WOOZY2[5]  = { "            ", " .~o-OO-o~. ", "(~~~~~~~~~~)", "   |@   x|  ", "   |~~v~|   " };
  static const char* const STUMBLE[5] = { "            ", " .-o-OO-o-. ", "(__________)", "   |@   @|  ", " /-|_---_|\\ " };

  const char* const* P[5] = { TILT_L, TILT_R, WOOZY, WOOZY2, STUMBLE };
  static const uint8_t SEQ[] = { 0,1,0,1, 2,3, 0,1,0,1, 4,4, 2,3 };
  static const int8_t X_SHIFT[] = { -3,3,-3,3, 0,0, -3,3,-3,3, 0,0, 0,0 };
  uint8_t beat = (t / 4) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, 0, 0xF810, X_SHIFT[beat]);

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
static void __mushroom_heart(uint32_t t) {
  static const char* const DREAMY[5]  = { "            ", " .-o-OO-o-. ", "(__________)", "   |^   ^|  ", "   |____|   " };
  static const char* const BLUSH[5]   = { "            ", " .-o-OO-o-. ", "(__________)", "  #|^   ^|# ", "   |____|   " };
  static const char* const EYES_C[5]  = { "            ", " .-O-oo-O-. ", "(__________)", "   |<3 <3|  ", "   |____|   " };
  static const char* const TWIRL[5]   = { "            ", " .-O-OO-O-. ", "(__________)", "   |@   @|  ", "  /|____|\\  " };
  static const char* const SIGH[5]    = { "            ", " .-o-OO-o-. ", "(__________)", "   |-   -|  ", "   |^^^^|   " };

  const char* const* P[5] = { DREAMY, BLUSH, EYES_C, TWIRL, SIGH };
  static const uint8_t SEQ[] = {
    0,0,1,0, 2,2,0, 1,0,4, 0,0,3,3, 0,1,0,2, 1,0
  };
  static const int8_t Y_BOB[] = { 0,-1,0,-1, 0,-1,0, -1,0,0, -1,0,0,0, -1,0,-1,0, -1,0 };
  uint8_t beat = (t / 5) % sizeof(SEQ);
  ascii_print_sprite(P[SEQ[beat]], 5, Y_BOB[beat], 0xF810, 0);

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
const ascii_persona_t PERSONA_MUSHROOM = {
    .name = "mushroom",
    .states = { __mushroom_sleep, __mushroom_idle, __mushroom_busy, __mushroom_attention, __mushroom_celebrate, __mushroom_dizzy, __mushroom_heart },
};
