#pragma once

#include "ps4.h"

// PS4 payload SDK doesn't define these; fall back to raw FreeBSD values.
// (0x1006 == 4102, 0x1005 == 4101)
#ifndef SCE_NET_SO_RCVTIMEO
#  define SCE_NET_SO_RCVTIMEO 0x1006
#endif
#ifndef SCE_NET_SO_SNDTIMEO
#  define SCE_NET_SO_SNDTIMEO 0x1005
#endif

#define SERVER_PORT   80
#define WEB_ROOT      "/data/websrv"
#define BUFFER_SIZE   8192
#define NET_POLL_SEC  3   // how often network monitor checks (seconds)
#define MAX_CLIENTS   12  // max concurrent client threads; excess gets a 503

#define INSTANCE_FILE WEB_ROOT "/.instance"

// Shared globals (defined in instance.c)
// g_instance_id  : ID written to INSTANCE_FILE; used for hot-swap detection.
// g_running      : set to 0 by shutdown_server(); every loop checks this first.
// g_server_fd    : kept here so shutdown_server() can close it and unblock accept().
// g_client_count : atomic counter; prevents unbounded thread spawning.
extern uint32_t     g_instance_id;
extern volatile int g_running;
extern volatile int g_server_fd;
extern volatile int g_client_count;