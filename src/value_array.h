// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Generic helpers for the "fixed list of discrete option values" pattern
// used by power-save and sleep-timer settings.

// Return the index of `value` in `values[0..count-1]`, or 0 if not present.
static inline int valueToIndex(const int *values, int count, int value) {
  for (int i = 0; i < count; i++)
    if (values[i] == value) return i;
  return 0;
}

// Return `values[index]` with the index clamped to a valid range.
static inline int indexToValue(const int *values, int count, int index) {
  if (count <= 0) return 0;
  if (index < 0) index = 0;
  if (index >= count) index = count - 1;
  return values[index];
}
