#pragma once

#include "globals.h"

// Creates WEB_ROOT if it doesn't exist, then writes index.html and loader.html
// from the embedded assets (index.h / loader.h) if not already present.
// Call once at startup before spawning any threads.
void setup_webroot(void);