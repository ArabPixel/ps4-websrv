#pragma once

#include "globals.h"

// Main HTTP server loop. Binds to SERVER_PORT, accepts connections, and spawns
// a client_thread_entry thread per connection (capped at MAX_CLIENTS).
// Auto-restarts when the network drops and comes back.
void *http_server_thread(void *arg);

// Polls the local IP every NET_POLL_SEC seconds and sends a PS4 notification
// when the network connects, disconnects, or the IP changes.
void *network_monitor_thread(void *arg);