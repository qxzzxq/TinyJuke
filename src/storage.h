// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <SD.h>

// Runtime SD-card lifecycle flag. Set by initSDAndLoadTags() in main.cpp;
// read by any module that needs to gate SD operations (encoder settings
// load/save, boot-screen branch, etc.). Defined in main.cpp.
extern bool sdReady;

// Open a file for reading, treating "not there" as an ordinary outcome.
//
// SD.open() on a missing path makes the ESP32 VFS layer log
//   E (...) vfs_api.cpp:99 open(): /sd/<name> does not exist, no permits for creation
// at ERROR level. For these callers a missing file is normal and already
// handled — a fresh card has no tags.json, a tag may carry no album art, a
// track may have been deleted — so the log line is noise that reads like a
// fault. Checking first keeps the expected case quiet; genuine failures
// (corrupt card, bad path on a mounted card) still surface via SD.open().
//
// Returns a falsy File when the file is absent, so existing `if (!f)` error
// handling at the call sites works unchanged.
inline File sdOpenRead(const char *path) {
  if (!sdReady || !SD.exists(path)) return File();
  return SD.open(path, FILE_READ);
}
