#pragma once

#include "config.h"

void initWebServer();
void stopWebServer();
void handleWebClient();
int getWebConnectionCount();
const char *getWebPin();  // per-session 4-digit PIN gating /update (shown on the web screen)
