// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Pure NFC presence policy — no Arduino deps so it's host-testable.
//
// Three places poll the reader at different rates (idle waiting screen, the
// re-poll between track repeats, and during playback). They used to carry
// unrelated hard-coded miss counts, which meant the physical behaviour
// silently changed whenever a poll period was tuned. Express the policy as a
// *duration* instead and derive each site's count from its own cadence.

// Poll periods. readPassiveTargetID() blocks for its timeout when no tag is
// present, so each of these also sets the loop cadence at that site.
static const uint32_t NFC_POLL_MS          = 100;  // idle waiting screen
static const uint32_t NFC_PLAYBACK_POLL_MS = 150;  // inside playWav()
static const uint32_t NFC_REPLAY_POLL_MS   =  50;  // re-poll between repeats

// How long a tag must stay unreadable before we accept that it is gone.
//
// A single failed read must NEVER be enough. Placing a tag quickly makes it
// skim the edge of the reader's field, so the first reads after a successful
// detection can miss while it settles. Treating one miss as a removal makes
// playback start, stop, and immediately restart.
//
// Two values because the trade-off differs: while audio is playing the sound
// keeps going until we accept the removal, so confirm sooner; when idle
// nothing is happening and a lazier, more tolerant confirmation is better.
static const uint32_t TAG_ABSENT_IDLE_MS    = 900;
static const uint32_t TAG_ABSENT_PLAYING_MS = 450;

// Consecutive missed reads that add up to `confirmMs` at a given poll period.
// Rounds up, and is always at least 2 so no cadence can ever reduce this to a
// single unlucky read.
constexpr uint8_t tagAbsentMisses(uint32_t confirmMs, uint32_t pollPeriodMs) {
  if (pollPeriodMs == 0) return 2;
  uint32_t n = (confirmMs + pollPeriodMs - 1) / pollPeriodMs;  // ceil
  if (n < 2)   n = 2;
  if (n > 255) n = 255;
  return (uint8_t)n;
}

// How long after a tag is first detected its reads stay unreliable.
//
// A tag placed quickly is detected at the edge of the field and then keeps
// moving for a moment before it comes to rest. Meanwhile the firmware is busy
// loading album art off the SD card and starting I2S, so by the time polling
// resumes the tag can still be mid-flight. Observed in the field: one
// detection, playWav() stopping on its own miss run, and the track restarting
// from the beginning — twice — before the tag settled.
static const uint32_t TAG_SETTLE_MS = 1000;

// Should a run of consecutive missed reads stop playback?
//
// Misses inside the settle window don't count. Mid-track a miss run means the
// tag really is gone; right after detection it usually means the tag has not
// landed yet, and stopping there restarts the track from zero.
constexpr bool playbackShouldStop(uint8_t misses, uint32_t sinceDetectMs) {
  return sinceDetectMs >= TAG_SETTLE_MS &&
         misses >= tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS);
}
