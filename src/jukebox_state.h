#pragma once

// Top-level jukebox state machine — pure logic, host-testable.
//
// What it models: the *between-iterations* mode of loop() — Waiting vs.
// Sleeping — and the guard conditions that gated transitions before
// (`tagPresent`, `sleepStopped`, tag-removal debounce, idle timer).
//
// What it does NOT model: the inner `while (tagPresent)` play loop, the
// unknown-tag screen wait, and the management GUI. main.cpp short-circuits
// to guiLoop() when menuActive, and invokes the play loop as the
// `TriggerPlayback` action; the FSM only resumes ticking after these
// blocking sections return.

#include <stdint.h>
#include <stddef.h>

enum class Mode : uint8_t {
  Waiting,   // jukebox idle, display on, ready to detect a tag
  Sleeping,  // display off (power saving)
};

// Normalized encoder event — main.cpp maps raw readEncoder() into this.
enum class EncEvent : uint8_t {
  None,
  Rotated,
  Click,
  Hold,
};

struct JukeboxState {
  Mode     mode;
  bool     sleepStopped;     // sleep timer fired during playback; suppress
                             //   playback re-trigger AND nfc-wake until tag removal
  bool     tagPresent;       // last NFC poll(s) saw a tag (gates trigger + removal debounce)
  uint8_t  tagAbsentCount;   // consecutive missed NFC reads (3 confirms removal)
  uint32_t lastActivityMs;   // millis() of last user interaction
};

struct TickInput {
  uint32_t  nowMs;
  EncEvent  encoderEvent;
  bool      nfcFound;
  bool      audioPlaying;        // external — true while playWav() is streaming
  bool      sleepTimerJustFired; // edge: audio's sleepTimerFired flag set since last tick
  int       powerSaveMinutes;
};

enum class Action : uint8_t {
  None = 0,
  EnterSleep,           // gfx.displayOff + backlight off
  WakeFromSleep,        // gfx.displayOn + backlight on + drain encoder + nfc.SAMConfig + drawWaiting
  EnterMenu,            // saveVolume + guiEnter
  TriggerPlayback,      // main.cpp: lookup tag → play loop (blocking)
  ConfirmTagRemoved,    // main.cpp: stopPlayback if still playing, drawWaiting
};

struct ActionList {
  Action items[4];
  uint8_t count;
};

struct TickResult {
  JukeboxState state;
  ActionList   actions;
};

// One tick of the outer state machine. Pure: no globals, no I/O.
TickResult jukeboxStep(JukeboxState state, TickInput in);

// Convenience: build a fresh state suitable for boot.
static inline JukeboxState jukeboxInitialState(uint32_t nowMs) {
  JukeboxState s = {};
  s.mode = Mode::Waiting;
  s.sleepStopped = false;
  s.tagPresent = false;
  s.tagAbsentCount = 0;
  s.lastActivityMs = nowMs;
  return s;
}
