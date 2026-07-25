// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#include "jukebox_state.h"
#include "timer_logic.h"

static inline void emit(ActionList &al, Action a) {
  if (al.count < (sizeof(al.items) / sizeof(al.items[0])))
    al.items[al.count++] = a;
}

TickResult jukeboxStep(JukeboxState s, TickInput in) {
  ActionList al = {};

  // Any encoder event counts as user activity (rotation, click, or hold).
  if (in.encoderEvent != EncEvent::None) {
    s.lastActivityMs = in.nowMs;
  }

  // -------------------------------------------------------------
  // SLEEPING — only wake conditions are evaluated.
  // -------------------------------------------------------------
  if (s.mode == Mode::Sleeping) {
    bool wake = false;
    // Any encoder event wakes (including ENC_HOLD, which is consumed here
    // and does NOT enter the menu — matches pre-FSM behavior).
    if (in.encoderEvent != EncEvent::None) wake = true;
    // NFC wake is suppressed while a tag is still on the reader after the
    // audio sleep timer fired (sleepStopped). Otherwise an arrival wakes.
    if (!wake && in.nfcFound && !s.sleepStopped) wake = true;

    if (wake) {
      s.mode = Mode::Waiting;
      s.lastActivityMs = in.nowMs;
      emit(al, Action::WakeFromSleep);
    }
    return { s, al };
  }

  // -------------------------------------------------------------
  // WAITING — full event handling.
  // -------------------------------------------------------------

  // Edge from audio module: sleep timer fired during the last play loop.
  // Mark the suppression flag; the tag is still on the reader physically,
  // so tagPresent stays true and absent-debounce continues from where it was.
  if (in.sleepTimerJustFired) {
    s.sleepStopped = true;
    s.lastActivityMs = in.nowMs;
  }

  // Long-press → enter menu. Menu is handled externally; we just emit the
  // action. No FSM mode for menu (main.cpp short-circuits while menuActive).
  if (in.encoderEvent == EncEvent::Hold) {
    emit(al, Action::EnterMenu);
    return { s, al };
  }

  // --- NFC handling ---
  if (in.nfcFound) {
    if (s.tagPresent) {
      // Tag still present — reset removal debounce.
      s.tagAbsentCount = 0;
    } else if (!s.sleepStopped) {
      // New tag arrival, no suppression → trigger playback.
      s.tagPresent = true;
      s.tagAbsentCount = 0;
      s.lastActivityMs = in.nowMs;
      emit(al, Action::TriggerPlayback);
    }
    // else: nfcFound && !tagPresent && sleepStopped — shouldn't reach here
    //   under correct sequencing (sleepStopped is only true with tagPresent),
    //   so we ignore defensively.
  } else if (s.tagPresent) {
    // Tag missing this tick. Require several consecutive misses before
    // accepting removal — guards against PN532 read glitches.
    if (++s.tagAbsentCount >= TAG_ABSENT_CONFIRM) {
      s.tagPresent = false;
      s.sleepStopped = false;
      s.tagAbsentCount = 0;
      s.lastActivityMs = in.nowMs;
      emit(al, Action::ConfirmTagRemoved);
    }
  }

  // --- Power-save sleep entry ---
  // Don't sleep while audio is streaming or while a tag is being actively
  // played (tagPresent && !sleepStopped). Either no tag or suppressed-tag
  // allows the idle clock to elapse.
  if (!in.audioPlaying && (!s.tagPresent || s.sleepStopped)) {
    if (powerSaveShouldSleep(in.powerSaveMinutes, in.nowMs - s.lastActivityMs)) {
      s.mode = Mode::Sleeping;
      emit(al, Action::EnterSleep);
    }
  }

  return { s, al };
}
