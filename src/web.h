#pragma once

#include "config.h"

#include <PN532.h>

void initWebServer(PN532 &nfc);
void stopWebServer();
void handleWebClient();
int getWebConnectionCount();
const char *getWebPin();  // per-session 4-digit PIN gating /update (shown on the web screen)
