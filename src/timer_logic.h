#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Pure timer policy helpers — no Arduino deps so they're host-testable.
// `minutes <= 0` means "disabled" for both timers.

// Sleep timer (audio): fire after `minutes` of playback have elapsed.
static inline bool sleepTimerShouldFire(int minutes, uint32_t elapsedMs) {
  return minutes > 0 && elapsedMs >= (uint32_t)minutes * 60000UL;
}

// Power save (display): enter sleep after `minutes` of user idle.
static inline bool powerSaveShouldSleep(int minutes, uint32_t idleMs) {
  return minutes > 0 && idleMs >= (uint32_t)minutes * 60000UL;
}

// Milliseconds remaining before a timer of `minutes` length fires.
// Returns 0 if disabled, already fired, or saturated to non-negative.
static inline uint32_t timerRemainingMs(int minutes, uint32_t elapsedMs) {
  if (minutes <= 0) return 0;
  uint32_t total = (uint32_t)minutes * 60000UL;
  return (elapsedMs < total) ? (total - elapsedMs) : 0;
}

// Format a remaining-ms value as "MM:SS" (or "MMM:SS" if minutes ≥ 100).
// buf must be at least 8 bytes (5 digits + colon + NUL + slack).
static inline void formatCountdownMMSS(uint32_t remainingMs, char *buf, size_t bufLen) {
  uint32_t totalSec = remainingMs / 1000;
  snprintf(buf, bufLen, "%02lu:%02lu",
           (unsigned long)(totalSec / 60),
           (unsigned long)(totalSec % 60));
}
