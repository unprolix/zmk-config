/*                        48 POSITION MATRIX / LAYOUT MAPPING  (Rolio)

  ╭────────────────────────┬────────────────────────╮ ╭─────────────────────────┬─────────────────────────╮
  │  0   1   2   3   4   5 │  6   7   8   9  10  11 │ │ LT5 LT4 LT3 LT2 LT1 LT0 │ RT0 RT1 RT2 RT3 RT4 RT5 │
  │ 12  13  14  15  16  17 │ 18  19  20  21  22  23 │ │ LM5 LM4 LM3 LM2 LM1 LM0 │ RM0 RM1 RM2 RM3 RM4 RM5 │
  │ 24  25  26  27  28  29 │ 32  33  34  35  36  37 │ │ LB5 LB4 LB3 LB2 LB1 LB0 │ RB0 RB1 RB2 RB3 RB4 RB5 │
  │                 30  31 │                        │ │                 LEC REC │  (roller push switches) │
  ╰───╮ 38  39  40  41  42 │ 43  44  45  46  47 ╭───╯ ╰───╮ LX1 LX0 LH2 LH1 LH0 │ RH0 RH1 RH2 RX0 RX1 ╭───╯
      ╰────────────────────┴────────────────────╯         ╰─────────────────────┴─────────────────────╯

  Six columns of keycaps per half, same as a corne. The seventh matrix column
  is not a keycap column: it carries only the roller's push switch (LEC/REC,
  row 2) and the wide inner thumb key (LH0/RH0, row 3).

  LX1/LX0 and RX0/RX1 are the Sofle-style "sometimes" keys at the outer end of
  the bottom row -- the shared jjb layout has no slot for them.                */

#pragma once

#define LT0  5  // left-top row
#define LT1  4
#define LT2  3
#define LT3  2
#define LT4  1
#define LT5  0

#define RT0  6  // right-top row
#define RT1  7
#define RT2  8
#define RT3  9
#define RT4 10
#define RT5 11

#define LM0 17  // left-middle row
#define LM1 16
#define LM2 15
#define LM3 14
#define LM4 13
#define LM5 12

#define RM0 18  // right-middle row
#define RM1 19
#define RM2 20
#define RM3 21
#define RM4 22
#define RM5 23

#define LB0 29  // left-bottom row
#define LB1 28
#define LB2 27
#define LB3 26
#define LB4 25
#define LB5 24

#define LEC 30  // left roller push switch
#define REC 31  // right roller push switch

#define RB0 32  // right-bottom row
#define RB1 33
#define RB2 34
#define RB3 35
#define RB4 36
#define RB5 37

#define LX0 39  // left "sometimes" keys, inner first
#define LX1 38

#define LH0 42  // left thumb keys, LH0 is the wide inner one
#define LH1 41
#define LH2 40

#define RH0 43  // right thumb keys, RH0 is the wide inner one
#define RH1 44
#define RH2 45

#define RX0 46  // right "sometimes" keys, inner first
#define RX1 47

#define NUMROW
#define KEYS_L LT0 LT1 LT2 LT3 LT4 LT5 LM0 LM1 LM2 LM3 LM4 LM5 LB0 LB1 LB2 LB3 LB4 LB5
#define KEYS_R RT0 RT1 RT2 RT3 RT4 RT5 RM0 RM1 RM2 RM3 RM4 RM5 RB0 RB1 RB2 RB3 RB4 RB5
#define THUMBS_L LH0 LH1 LH2
#define THUMBS_R RH0 RH1 RH2
/* jjb.keymap defines THUMBS itself (LH2 LH1 LH0 RH0 RH1 RH2). */
