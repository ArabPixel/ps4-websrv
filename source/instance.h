#pragma once

#include "globals.h"

// Returns 1 if this process is still the active server instance, 0 otherwise.
// Checks g_running first (fast path), then reads INSTANCE_FILE.
int  is_instance_active(void);

// Generates a new instance ID, writes it to INSTANCE_FILE.
// Call once at startup before spawning threads.
void update_instance_id(void);

// Sends a dummy TCP connect to localhost:SERVER_PORT to unblock any
// sceNetAccept() call that a previous instance is blocked on.
void wakeup_previous_instance(void);

// Full shutdown sequence:
//   1. Sets g_running = 0       → all loops exit on next check.
//   2. Writes "0" to INSTANCE_FILE → is_instance_active() returns 0.
//   3. Closes g_server_fd        → sceNetAccept() unblocks immediately.
//      This step is what prevents the kernel panic on shutdown.
void shutdown_server(void);