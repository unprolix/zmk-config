/*                    46 POSITION MATRIX / LAYOUT MAPPING  (Corne v4 "ex2")

  ╭────────────────────────┬───────┬────────────────────────╮
  │  0   1   2   3   4   5 │  6  7 │  8   9  10  11  12  13 │
  │ 14  15  16  17  18  19 │ 20 21 │ 22  23  24  25  26  27 │
  │ 28  29  30  31  32  33 │       │ 34  35  36  37  38  39 │
  ╰───────────╮ 40  41  42 │       │ 43  44  45 ╭───────────╯
              ╰────────────┴───────┴────────────╯

  ╭─────────────────────────┬─────────┬─────────────────────────╮
  │ LT5 LT4 LT3 LT2 LT1 LT0 │ LEC REC │ RT0 RT1 RT2 RT3 RT4 RT5 │
  │ LM5 LM4 LM3 LM2 LM1 LM0 │ LEX REX │ RM0 RM1 RM2 RM3 RM4 RM5 │
  │ LB5 LB4 LB3 LB2 LB1 LB0 │         │ RB0 RB1 RB2 RB3 RB4 RB5 │
  ╰───────────╮ LH2 LH1 LH0 │         │ RH0 RH1 RH2 ╭───────────╯
              ╰─────────────┴─────────┴─────────────╯

  THIS BOARD CANNOT USE zmk-helpers/key-labels/42.h, WHICH IS WHAT IT DID AT
  FIRST. This is the ex2 population: a two-key column at the inner edge of each
  half, interleaved into the transform at 6, 7, 20 and 21 so that the 42 shared
  positions keep their reading order. That pushes everything after the left top
  row along -- RT by two, and RM, LB, RB and the thumbs by four -- so 42.h's
  numbers are
  wrong for all but LT0..LT5.

  Nothing FAILS when they are wrong, which is what makes it expensive: a combo
  simply fires on two other keys, and an HRM's hold-trigger-key-positions
  quietly gates on a scrambled set. The contraction combos are what surfaced it
  (i_am is LM3+RB2, which under 42.h landed on the left home pinky and a left
  bottom-row key). Confirmed against the transform in corne_v4.dtsi, and against
  a console capture: the nav thumbs report positions 41 and 44, which are LH1
  and RH1 here and 37 and 40 under 42.h.

  LEC/REC are the push switches under the two rotary encoders; LEX/REX the
  ordinary keycaps below them. They are DELIBERATELY not in KEYS_L/KEYS_R, the
  same choice rolio48.h makes for the roller push switches: those lists are the
  shared layout's hold-trigger-key-positions, and adding positions to them
  changes how home-row mods resolve on this board and no other.

  SPDX-License-Identifier: MIT                                                */

#pragma once

#define LT0  5  // left-top row
#define LT1  4
#define LT2  3
#define LT3  2
#define LT4  1
#define LT5  0

#define LEC  6  // expansion upper: the encoder push switches
#define REC  7

#define RT0  8  // right-top row
#define RT1  9
#define RT2 10
#define RT3 11
#define RT4 12
#define RT5 13

#define LM0 19  // left-middle row
#define LM1 18
#define LM2 17
#define LM3 16
#define LM4 15
#define LM5 14

#define LEX 20  // expansion lower: ordinary keycaps
#define REX 21

#define RM0 22  // right-middle row
#define RM1 23
#define RM2 24
#define RM3 25
#define RM4 26
#define RM5 27

#define LB0 33  // left-bottom row
#define LB1 32
#define LB2 31
#define LB3 30
#define LB4 29
#define LB5 28

#define RB0 34  // right-bottom row
#define RB1 35
#define RB2 36
#define RB3 37
#define RB4 38
#define RB5 39

#define LH0 42  // left thumb keys, inner first
#define LH1 41
#define LH2 40

#define RH0 43  // right thumb keys, inner first
#define RH1 44
#define RH2 45

#define NUMROW
#define KEYS_L LT0 LT1 LT2 LT3 LT4 LT5 LM0 LM1 LM2 LM3 LM4 LM5 LB0 LB1 LB2 LB3 LB4 LB5
#define KEYS_R RT0 RT1 RT2 RT3 RT4 RT5 RM0 RM1 RM2 RM3 RM4 RM5 RB0 RB1 RB2 RB3 RB4 RB5
#define THUMBS_L LH0 LH1 LH2
#define THUMBS_R RH0 RH1 RH2
/* jjb.keymap defines THUMBS itself (LH2 LH1 LH0 RH0 RH1 RH2). */
