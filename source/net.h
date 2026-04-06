#pragma once

#include "globals.h"

// Returns the MIME type string for the given file path based on extension.
// Falls back to "application/octet-stream" for unknown types.
const char *get_mime_type(const char *path);

// Fills ip_out (must be >= 16 bytes) with the local IPv4 address.
// Returns 1 on success, 0 if no network is available or DHCP hasn't assigned yet.
int get_local_ip(char *ip_out);

// Applies a 5-second recv + send timeout to a client socket.
// Without this, a client that connects but never sends data blocks its
// thread indefinitely, eventually exhausting all thread slots.
void set_socket_timeouts(int fd);