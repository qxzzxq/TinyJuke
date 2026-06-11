// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Runtime SD-card lifecycle flag. Set by initSDAndLoadTags() in main.cpp;
// read by any module that needs to gate SD operations (encoder settings
// load/save, boot-screen branch, etc.). Defined in main.cpp.
extern bool sdReady;
