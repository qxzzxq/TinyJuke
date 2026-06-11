// SPDX-FileCopyrightText: 2026 qxzzxq
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <PN532.h>

// Bluetooth A2DP sink mode (the jukebox acts as a Bluetooth speaker).
//
// Lifecycle: initBluetoothMode() hands the I2S peripheral to the A2DP stack
// and starts the BT classic stack. stopBluetoothMode() tears it down and
// re-primes the local I2S driver so the MAX98357A pins stay driven.
//
// While in BT mode, the GUI is "active" — main.cpp short-circuits the NFC
// FSM, so we run our own NFC poll here at ~300 ms intervals to detect a tag
// for the "switch to jukebox?" prompt.

void initBluetoothMode(PN532 &nfc);
void stopBluetoothMode();

// Called from guiLoop() every iteration while on the BT screen.
// Services AVRCP metadata callbacks, NFC polling, and the sleep timer.
void handleBluetoothLoop();

bool btIsConnected();         // peer connected and A2DP session open
bool btIsStreaming();          // audio frames flowing in the last ~500 ms
bool btTagDetected();          // PN532 saw a UID on the most recent poll
bool btSleepFired();           // sleep timer expired; consumer should exit BT
bool btMetadataChanged();      // title/artist/connection changed since last read

const char *btTrackTitle();    // "" when invalid / not reported
const char *btTrackArtist();
const char *btPeerName();      // "" when no peer
