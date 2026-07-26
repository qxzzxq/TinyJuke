// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Pure animation helpers — no Arduino deps so they're host-testable.
//
// Progress is fixed-point per-mille (0..ANIM_SCALE) rather than float: the
// values feed pixel math, and integers keep the unit tests exact.
//
// Like timer_logic.h, nothing here calls millis() — the caller passes the
// current time in. Elapsed time is computed with unsigned subtraction, so
// millis() wrap is handled the same way as everywhere else in the codebase.

static const int32_t ANIM_SCALE = 1000;

// Linear progress through an animation, clamped to 0..ANIM_SCALE.
// A zero duration means "already finished".
static inline int32_t animProgress(uint32_t elapsedMs, uint32_t durationMs) {
  if (durationMs == 0) return ANIM_SCALE;
  if (elapsedMs >= durationMs) return ANIM_SCALE;
  return (int32_t)(((uint64_t)elapsedMs * ANIM_SCALE) / durationMs);
}

// Cubic ease-out: fast departure, gentle settle. q and the result are both
// 0..ANIM_SCALE. Computed as SCALE - (SCALE-q)^3 / SCALE^2 in 64-bit so the
// intermediate cube doesn't overflow and truncation stays under ~1 unit.
static inline int32_t easeOutCubic(int32_t q) {
  if (q <= 0) return 0;
  if (q >= ANIM_SCALE) return ANIM_SCALE;
  int64_t u = ANIM_SCALE - q;
  return (int32_t)(ANIM_SCALE - (u * u * u) / ((int64_t)ANIM_SCALE * ANIM_SCALE));
}

// Interpolate from..to by q (0..ANIM_SCALE). Exact at both endpoints.
static inline int32_t animLerp(int32_t from, int32_t to, int32_t q) {
  if (q <= 0) return from;
  if (q >= ANIM_SCALE) return to;
  return from + (int32_t)(((int64_t)(to - from) * q) / ANIM_SCALE);
}

// A single retargetable value animation. Held by the caller (screen state),
// advanced by passing the current time to animValue().
struct AnimI32 {
  int32_t  from;
  int32_t  to;
  uint32_t startMs;
  uint32_t durationMs;
};

static inline void animStart(AnimI32 &a, int32_t from, int32_t to,
                             uint32_t nowMs, uint32_t durationMs) {
  a.from       = from;
  a.to         = to;
  a.startMs    = nowMs;
  a.durationMs = durationMs;
}

// Eased current value. Returns `to` once the duration has elapsed.
static inline int32_t animValue(const AnimI32 &a, uint32_t nowMs) {
  int32_t q = animProgress(nowMs - a.startMs, a.durationMs);
  return animLerp(a.from, a.to, easeOutCubic(q));
}

static inline bool animDone(const AnimI32 &a, uint32_t nowMs) {
  return (nowMs - a.startMs) >= a.durationMs;
}

// Seed an animation that is already at its end value — the "nothing in
// flight" resting state.
static inline void animSettle(AnimI32 &a, int32_t value, uint32_t nowMs) {
  animStart(a, value, value, nowMs, 0);
}

// Long-press indicator fill, as a percentage of the way from the hint delay to
// the hold threshold. Returns -1 while the press is still too short to show
// anything — callers treat that as "clear the indicator".
//
// Linear on purpose: this reports real elapsed time, so easing it would
// misrepresent how much longer the user has to keep holding.
static inline int holdProgressPct(uint32_t heldMs, uint32_t hintDelayMs,
                                  uint32_t holdMs) {
  if (heldMs < hintDelayMs) return -1;
  if (holdMs <= hintDelayMs) return 100;   // degenerate config — never divide by 0
  if (heldMs >= holdMs) return 100;
  return (int)(((heldMs - hintDelayMs) * 100) / (holdMs - hintDelayMs));
}
