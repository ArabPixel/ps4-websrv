/*
  This file was commented in the hope of myself and others understand the code better
  Even useful for myself in the future when I forget how it works and need to update it, or fix bugs, or add features.
*/

#include "ps4.h"
#include "index.h"  // built by xxd -i index.html  > assets/index.h  in Makefile
#include "loader.h" // built by xxd -i loader.html > assets/loader.h in Makefile

// PS4 payload SDK doesn't have this? fall back to the raw FreeBSD 
// socket-option values
#ifndef SCE_NET_SO_RCVTIMEO
#  define SCE_NET_SO_RCVTIMEO 0x1006 // 4102
#endif
#ifndef SCE_NET_SO_SNDTIMEO
#  define SCE_NET_SO_SNDTIMEO 0x1005 // 4101
#endif

#define SERVER_PORT 80
#define WEB_ROOT "/data/websrv"
#define BUFFER_SIZE 8192
#define NET_POLL_SEC 3   // how often network monitor checks (seconds)
#define MAX_CLIENTS  12  // max concurrent client threads; excess connections are dropped

// Instance file used for checking if the server is already running
static uint32_t g_instance_id = 0;
#define INSTANCE_FILE WEB_ROOT "/.instance"

// In-memory globals
// g_running  : set to 0 by shutdown_server(); every loop checks this first.
// g_server_fd: kept here so shutdown_server() can close it and unblock accept().
// g_client_count: prevents unbounded thread spawning.
static volatile int g_running      = 1;
static volatile int g_server_fd    = -1;
static volatile int g_client_count = 0;

static int is_instance_active(void);
static void shutdown_server(void);


// MIME

const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)                          return "application/octet-stream";
  if (strcmp(ext, ".html") == 0)     return "text/html";
  if (strcmp(ext, ".css")  == 0)     return "text/css";
  if (strcmp(ext, ".js")   == 0 ||
      strcmp(ext, ".mjs")  == 0)     return "text/javascript";
  if (strcmp(ext, ".json") == 0)     return "application/json";
  if (strcmp(ext, ".wasm") == 0)     return "application/wasm";
  if (strcmp(ext, ".png")  == 0)     return "image/png";
  if (strcmp(ext, ".jpg")  == 0 ||
      strcmp(ext, ".jpeg") == 0)     return "image/jpeg";
  if (strcmp(ext, ".ico")  == 0)     return "image/x-icon";
  if (strcmp(ext, ".svg")  == 0)     return "image/svg+xml";
  if (strcmp(ext, ".mp4")  == 0)     return "video/mp4";
  if (strcmp(ext, ".webm") == 0)     return "video/webm";
  if (strcmp(ext, ".ogg")  == 0)     return "audio/ogg";
  if (strcmp(ext, ".mp3")  == 0)     return "audio/mpeg";
  if (strcmp(ext, ".txt")  == 0)     return "text/plain";
  if (strcmp(ext, ".xml")  == 0)     return "application/xml";
  if (strcmp(ext, ".pdf")  == 0)     return "application/pdf";
  return "application/octet-stream";
}

// File / directory helpers
static void write_default_file(const char *path,
                                const unsigned char *data,
                                unsigned int len) {
  if (file_exists((char *)path)) return; // cast: SDK file_exists takes char*, not const char*
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  write(fd, data, len);
  close(fd);
}

static void mkdir_recursive(const char *path) {
  char tmp[PATH_MAX];
  char *p;
  size_t len;

  snprintf(tmp, sizeof(tmp), "%s", path);
  len = strlen(tmp);
  if (len == 0) return;
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

  for (p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

static void setup_webroot(void) {
  struct stat st;
  char path[PATH_MAX];

  if (stat(WEB_ROOT, &st) != 0 || !S_ISDIR(st.st_mode))
    mkdir_recursive(WEB_ROOT);

  snprintf(path, sizeof(path), "%s/index.html", WEB_ROOT);
  write_default_file(path, index_html, index_html_len);

  snprintf(path, sizeof(path), "%s/loader.html", WEB_ROOT);
  write_default_file(path, loader_html, loader_html_len);
}

// Network helpers
// Fill ip_out (must be at least 16 bytes) with the local IPv4 address.
// Returns 1 on success, 0 if no network is available.
static int get_local_ip(char *ip_out) {
  ip_out[0] = '\0';
  int fd = sceNetSocket("ip_probe", AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return 0;

  struct sockaddr_in remote, local;
  unsigned int len = sizeof(local);
  memset(&remote, 0, sizeof(remote));
  remote.sin_family      = AF_INET;
  remote.sin_addr.s_addr = IP(8, 8, 8, 8);
  remote.sin_port        = sceNetHtons(53);

  int ok = 0;
  if (sceNetConnect(fd, (struct sockaddr *)&remote, sizeof(remote)) == 0 &&
      sceNetGetsockname(fd, (struct sockaddr *)&local, &len) == 0) {
    sceNetInetNtop(AF_INET, &local.sin_addr, ip_out, 16);
    // Reject 0.0.0.0 — interface is up but DHCP hasn't assigned an IP yet
    ok = (ip_out[0] != '\0' && strcmp(ip_out, "0.0.0.0") != 0);
    if (!ok) ip_out[0] = '\0';
  }
  sceNetSocketClose(fd);
  return ok;
}

// Send timeouts to a client socket.
// without this, any client that connects and sends nothing blocks its
// thread indefinitely, resulting in server exhausts.
static void set_socket_timeouts(int fd) {
  struct timeval tv;
  tv.tv_sec  = 5;
  tv.tv_usec = 0;
  sceNetSetsockopt(fd, SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &tv, sizeof(tv));
  sceNetSetsockopt(fd, SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &tv, sizeof(tv));
}


// HTTP request handler
static void handle_client(int client_fd) {
  char buffer[BUFFER_SIZE];
  int bytes_read = sceNetRecv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) return;
  buffer[bytes_read] = '\0';

  // Only handle GET requests
  if (strncmp(buffer, "GET ", 4) != 0) {
    const char *method_not_allowed =
      "HTTP/1.1 405 Method Not Allowed\r\n"
      "Content-Length: 18\r\n"
      "Connection: close\r\n\r\n"
      "Method Not Allowed";
    sceNetSend(client_fd, method_not_allowed, strlen(method_not_allowed), 0);
    return;
  }

  char *path     = buffer + 4;
  char *end_path = strchr(path, ' ');
  if (!end_path) return;
  *end_path = '\0';

  // Strip query string
  char *query = strchr(path, '?');
  if (query) *query = '\0';

  // Shutdown endpoint
  if (strcmp(path, "/shutdown") == 0) {
    const char *ok_shutdown =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 13\r\n"
      "Connection: close\r\n\r\n"
      "Shutting down";
    sceNetSend(client_fd, ok_shutdown, strlen(ok_shutdown), 0);

    printf_notification("Web server shutting down...");
    printf_debug("[PS4-Websrv] Shutdown requested via web UI.\n");
    
    shutdown_server();
    return;
  }

  if (strcmp(path, "/notify") == 0) {
    const char *ok_notify =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 6\r\n"
      "Connection: close\r\n\r\n"
      "Notify";
    sceNetSend(client_fd, ok_notify, strlen(ok_notify), 0);
    printf_notification("Hello World...");
    printf_debug("[PS4-Websrv] Hello World requested via web UI.\n");
    return;
  }

  // Build full filesystem path
  char full_path[PATH_MAX];
  if (strcmp(path, "/") == 0)
    snprintf(full_path, sizeof(full_path), "%s/index.html", WEB_ROOT);
  else
    snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, path);

  // Ensure the resolved path still starts with WEB_ROOT (path traversal guard)
  if (strncmp(full_path, WEB_ROOT, strlen(WEB_ROOT)) != 0) {
    const char *forbidden =
      "HTTP/1.1 403 Forbidden\r\n"
      "Content-Length: 9\r\n"
      "Connection: close\r\n\r\n"
      "Forbidden";
    sceNetSend(client_fd, forbidden, strlen(forbidden), 0);
    return;
  }

  // index.html fallback for directory requests
  struct stat st;
  if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    size_t pl = strlen(full_path);
    if (pl + 11 < PATH_MAX) {
      if (full_path[pl - 1] != '/')
        full_path[pl++] = '/';
      strcpy(full_path + pl, "index.html");
    }
  }

  // Serve file
  int fd = open(full_path, O_RDONLY, 0);
  if (fd >= 0) {
    fstat(fd, &st);

    char response_header[512];
    snprintf(response_header, sizeof(response_header),
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %ld\r\n"
      "Cache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n"
      "\r\n",
      get_mime_type(full_path), (long)st.st_size);

    sceNetSend(client_fd, response_header, strlen(response_header), 0);
    int n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0)
      sceNetSend(client_fd, buffer, n, 0);
    close(fd);
  } else {
    const char *not_found =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Length: 9\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: close\r\n\r\n"
      "Not Found";
    sceNetSend(client_fd, not_found, strlen(not_found), 0);
  }
}

// Per-connection thread 
// Owns the client socket: handles the request then closes it.
// always has an accurate view of how many threads are live.

static void *client_thread_entry(void *arg) {
  int client_fd = (int)(intptr_t)arg;
  handle_client(client_fd);
  sceNetSocketClose(client_fd);
  __sync_fetch_and_sub(&g_client_count, 1);
  return NULL;
}

// HTTP server thread
// Auto-restarts when network drops / comes back.
// stores server_fd in g_server_fd so shutdown_server() can close it and
// unblock the sceNetAccept() call – without this, the accept never returns and
// the server thread lives past shutdown, causing a kernel panic. Thanks AI.
// checks g_running (in-memory) in addition to is_instance_active() (file).
// fail_streak reset to 0 on successful accept, and the outer loop now
//      re-enters correctly instead of returning NULL when not shutting down.
// That was a lot of commenting :)

void *http_server_thread(void *arg) {
  UNUSED(arg);

  // Make sure the loop is not infinitely looping.
  while (g_running && is_instance_active()) {
    int server_fd = sceNetSocket("http_server", AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      sceKernelSleep(2);
      continue;
    }

    int opt = 1;
    sceNetSetsockopt(server_fd, SOL_SOCKET, SCE_NET_SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_len         = sizeof(server_addr);
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = IN_ADDR_ANY;
    server_addr.sin_port        = sceNetHtons(SERVER_PORT);

    if (sceNetBind(server_fd,
                   (struct sockaddr *)&server_addr,
                   sizeof(server_addr)) < 0 ||
        sceNetListen(server_fd, 10) < 0) {
      sceNetSocketClose(server_fd);
      sceKernelSleep(2);
      continue;
    }

    // Publish the fd so shutdown_server() can close it from another thread.
    g_server_fd = server_fd;

    // Accept loop
    int fail_streak = 0;
    while (g_running && is_instance_active() && fail_streak < 5) {
      struct sockaddr_in client_addr;
      unsigned int client_len = sizeof(client_addr);
      int client_fd = sceNetAccept(server_fd,
                                   (struct sockaddr *)&client_addr,
                                   &client_len);
      if (client_fd < 0) {
        // If we were shut down, g_running == 0; break cleanly.
        if (!g_running || !is_instance_active()) break;
        fail_streak++;
        sceKernelSleep(1);
        continue;
      }
      fail_streak = 0;

      // Apply timeouts before handing off to a thread.
      set_socket_timeouts(client_fd);

      // FIX: cap threads.
      if (g_client_count >= MAX_CLIENTS) {
        const char *busy =
          "HTTP/1.1 503 Service Unavailable\r\n"
          "Content-Length: 4\r\n"
          "Connection: close\r\n\r\n"
          "Busy";
        sceNetSend(client_fd, busy, strlen(busy), 0);
        sceNetSocketClose(client_fd);
        continue;
      }

      __sync_fetch_and_add(&g_client_count, 1);

      ScePthread req_thread;
      if (scePthreadCreate(&req_thread, NULL,
                           client_thread_entry,
                           (void *)(intptr_t)client_fd,
                           "http_req") == 0) {
        scePthreadDetach(req_thread);
      } else {
        // Thread creation failed — handle inline and undo the counter.
        __sync_fetch_and_sub(&g_client_count, 1);
        handle_client(client_fd);
        sceNetSocketClose(client_fd);
      }
    }

    // Clear the global fd before closing so shutdown_server() doesn't
    // double-close it if it fires concurrently.
    g_server_fd = -1;
    sceNetSocketClose(server_fd);

    if (!g_running || !is_instance_active()) {
      printf_debug("[PS4-Websrv] Instance %u: server thread exiting.\n", g_instance_id);
      return NULL;
    }

    sceKernelSleep(2); // brief pause before retrying after a network drop
  }

  return NULL;
}

// Network monitor thread
// Polls the local IP every NET_POLL_SEC seconds
// Notifies user when network drops or comes back
// checks g_running so it exits promptly on shutdown without waiting for
// the next NET_POLL_SEC sleep to expire.

void *network_monitor_thread(void *arg) {
  UNUSED(arg);

  char prev_ip[16] = {0}; // empty = "no network"
  char curr_ip[16];

  while (g_running && is_instance_active()) {
    int has_net = get_local_ip(curr_ip);

    if (!has_net) {
      if (prev_ip[0] != '\0') {
        printf_notification("Web server pausing\u2026\nNetwork disconnected.");
        printf_debug("[PS4-Websrv] Network disconnected.");
        prev_ip[0] = '\0';
      }
    } else {
      if (strcmp(curr_ip, prev_ip) != 0) {
        if (SERVER_PORT == 80) {
          printf_notification("Web server started!\nhttp://%s", curr_ip);
          printf_debug("[PS4-Websrv] Web server started! http://%s", curr_ip);
        } else {
          printf_notification("Web server started!\nhttp://%s:%d", curr_ip, SERVER_PORT);
          printf_debug("[PS4-Websrv] Web server started! http://%s:%d", curr_ip, SERVER_PORT);
        }
        strcpy(prev_ip, curr_ip);
      }
    }

    sceKernelSleep(NET_POLL_SEC);
  }

  printf_debug("[PS4-Websrv] Instance %u: monitor thread exiting.\n", g_instance_id);
  return NULL;
}


//Instance ID management

static int is_instance_active(void) {
  // Check in-memory flag first — no file I/O needed after shutdown_server().
  if (!g_running) return 0;

  int fd = open(INSTANCE_FILE, O_RDONLY, 0);
  if (fd < 0) return 1; // if file is missing, assume we are active (edge case)

  char buf[16] = {0};
  read(fd, buf, sizeof(buf) - 1);
  close(fd);

  uint32_t active_id = (uint32_t)atoi(buf);
  return (active_id == g_instance_id);
}

static void update_instance_id(void) {
  // FIX: XOR time with a shifted counter to reduce 1-second collision window
  // when the payload is reloaded within the same second.
  static uint32_t boot_counter = 0;
  g_instance_id = (uint32_t)time(NULL) ^ (++boot_counter << 20);

  int fd = open(INSTANCE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  char buf[16];
  int len = snprintf(buf, sizeof(buf), "%u", g_instance_id);
  write(fd, buf, len);
  close(fd);
  printf_debug("[PS4-Websrv] New instance ID: %u\n", g_instance_id);
}

static void wakeup_previous_instance(void) {
  // Connect to the old instance's accept() to unblock it so it can read
  // the updated instance file and exit.
  int fd = sceNetSocket("wakeup", AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_len         = sizeof(addr);
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = IP(127, 0, 0, 1);
  addr.sin_port        = sceNetHtons(SERVER_PORT);

  sceNetConnect(fd, (struct sockaddr *)&addr, sizeof(addr));
  sceNetSocketClose(fd);
}
// Updating the shutdown to fix Kernel Panic on shutdown
// Now we:
//   1. Set g_running = 0  → all while-loops exit on their next check.
//   2. Write "0" to the instance file → is_instance_active() returns 0.
//   3. Close g_server_fd  → sceNetAccept() returns -1 immediately, so the
//      server thread wakes up, sees g_running == 0, and exits cleanly.
//      This is what prevents the kernel panic.
static void shutdown_server(void) {
  g_running = 0;

  // write 0 to the instance file so any thread that checks file-based identity exits.
  int fd = open(INSTANCE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    write(fd, "0", 1);
    close(fd);
  }

  // Close the listening socket.  This is the critical step: it forces
  // sceNetAccept() to return -1 immediately, unblocking the server thread.
  int sfd = g_server_fd;
  if (sfd >= 0) {
    g_server_fd = -1;
    sceNetSocketClose(sfd);
  }
}


// Entry point
int _main(struct thread *td) {
  UNUSED(td);
  initKernel();
  initLibc();
  initNetwork();
  initPthread();
  initSysUtil();
  jailbreak();

  setup_webroot();       // ensure /data/websrv exists before instance file ops
  update_instance_id();
  wakeup_previous_instance();

  ScePthread srv_thread, mon_thread;

  if (scePthreadCreate(&srv_thread, NULL,
                       http_server_thread, NULL,
                       "http_server") != 0) {
    printf_notification("Failed to start web server!");
    printf_debug("[PS4-Websrv] Failed to start web server!");
    return -1;
  }
  scePthreadDetach(srv_thread);

  if (scePthreadCreate(&mon_thread, NULL,
                       network_monitor_thread, NULL,
                       "net_monitor") != 0) {
    // Not fatal — server still runs, user just won't get IP notifications.
    printf_notification("Network monitor failed to start.");
    printf_debug("[PS4-Websrv] Network monitor failed to start.");
  } else {
    scePthreadDetach(mon_thread);
  }

  return 0;
}