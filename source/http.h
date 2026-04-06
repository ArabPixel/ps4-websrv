#pragma once

#include "globals.h"

// Reads the HTTP request from client_fd, routes it to the appropriate handler,
// and writes the response. Called either inline or from a dedicated thread.
void handle_client(int client_fd);

// Thread entry point for per-connection threads.
// Receives client_fd as (void*)(intptr_t), calls handle_client, then closes
// the socket and decrements g_client_count before returning.
void *client_thread_entry(void *arg);