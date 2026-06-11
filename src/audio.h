// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "config.h"
#include "encoder.h"  // for volumeLevel
#include "wav_parser.h"  // WavHeader, WavMeta, buffer-based parsers
#include <SD.h>
#include <PN532.h>

extern bool audioPlaying;
extern bool stopRequested;
extern bool sleepTimerFired;
extern uint32_t audioStartTime;

void i2sPrime();
void i2sDeinit();   // tear down the legacy I2S driver (used when handing off to BT)
void playWav(const char *filepath, PN532 &nfc, const uint8_t *tagUid, uint8_t tagUidLen);
void stopPlayback();
void parseWavMeta(const char *filepath, WavMeta &meta);
