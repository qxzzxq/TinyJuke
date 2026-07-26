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
static const uint32_t NFC_PLAYBACK_POLL_MS = 150;  // interval between polls in playWav()
static const uint32_t NFC_REPLAY_POLL_MS   =  50;  // re-poll between repeats

// Read timeout for the in-playback poll. This is *not* the interval above —
// it's how long each individual read waits for the reader to answer.
//
// It was 30 ms, chosen to keep the streaming loop responsive, but that is not
// long enough for the PN532 to reliably complete anticollision: a log from
// the field showed playWav() concluding "absent" on 30 ms reads while the
// re-poll immediately afterwards found the same stationary tag on a 50 ms
// read. 50 ms of stall every 150 ms is well inside the ~185 ms of I2S DMA
// buffering (8 descriptors x 1024 frames at 44.1 kHz), so audio is unaffected.
static const uint32_t NFC_PLAYBACK_READ_MS =  50;

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

// Upper bound on how long a freshly detected tag is allowed to be unreadable
// before its misses start counting anyway.
//
// A tag placed quickly is detected at the edge of the field and keeps moving
// for a moment before coming to rest, while the firmware is busy loading album
// art off SD and starting I2S. This is only a backstop: normally the wait ends
// as soon as the tag is actually read (see tagMissTick). It exists so a tag
// that is waved past the reader and never lands still stops playback.
static const uint32_t TAG_SETTLE_MS = 1000;

// Fold one poll result into the consecutive-miss counter; returns true when
// playback should stop.
//
// Two things this has to get right, both learned the hard way:
//
//  - The wait ends on the first *successful* read, not on a timer. Until the
//    tag has been seen once since playback began, a miss means "hasn't landed
//    yet", not "gone". Once it has been seen, normal debouncing applies
//    immediately, so lifting a tag still stops audio promptly.
//
//  - Early misses are *discarded*, not merely ignored. An earlier version kept
//    incrementing during the settle window and only gated the comparison; the
//    counter was then already past the threshold when the window closed, so
//    the very next miss stopped playback and the settle window achieved
//    nothing. That showed up as a track that still restarted, just less often,
//    and more the faster the tag was inserted.
static inline bool tagMissTick(uint8_t &misses, bool &confirmed,
                               bool seen, uint32_t sinceDetectMs) {
  if (seen) {
    confirmed = true;
    misses = 0;
    return false;
  }
  if (!confirmed && sinceDetectMs < TAG_SETTLE_MS) {
    misses = 0;                 // discard, don't accumulate
    return false;
  }
  if (misses < 255) misses++;
  return misses >= tagAbsentMisses(TAG_ABSENT_PLAYING_MS, NFC_PLAYBACK_POLL_MS);
}
