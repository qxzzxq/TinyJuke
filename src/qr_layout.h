// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Pure QR placement maths — no Arduino deps so it's host-testable.
//
// Kept separate from the drawing code because getting the quiet zone or the
// rounding wrong produces a code that looks fine but won't scan, and that is
// not something the firmware can check for itself.

// The QR spec requires a light margin of at least 4 modules on every side.
// Scanners use it to find the symbol; without it many simply never lock on.
static const int QR_QUIET_MODULES = 4;

struct QrPlacement {
  int scale;    // pixels per module (0 if it cannot fit at all)
  int size;     // total drawn square, in pixels, including the quiet zone
  int x, y;     // top-left of that square
  int originX;  // top-left of module (0,0), i.e. inside the quiet zone
  int originY;
};

// Fit `modules` x `modules` plus a quiet zone into the given box, centred,
// at the largest whole-pixel module size available.
//
// The scale is deliberately an integer: a fractional one makes some modules a
// pixel wider than others, which smears the edges scanners rely on.
static inline QrPlacement qrPlace(int modules, int boxX, int boxY,
                                  int boxW, int boxH) {
  QrPlacement p = {};
  if (modules <= 0) return p;

  int total = modules + 2 * QR_QUIET_MODULES;
  int scale = (boxW < boxH ? boxW : boxH) / total;
  if (scale < 1) return p;   // caller must handle "does not fit"

  p.scale   = scale;
  p.size    = total * scale;
  p.x       = boxX + (boxW - p.size) / 2;
  p.y       = boxY + (boxH - p.size) / 2;
  p.originX = p.x + QR_QUIET_MODULES * scale;
  p.originY = p.y + QR_QUIET_MODULES * scale;
  return p;
}
