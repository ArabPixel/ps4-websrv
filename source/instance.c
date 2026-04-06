#include "instance.h"

// Global definitions
// These are declared extern in globals.h and used across all translation units.
uint32_t     g_instance_id   = 0;
volatile int g_running       = 1;
volatile int g_server_fd     = -1;
volatile int g_client_count  = 0;


// Instance management
int is_instance_active(void) {
  // Check in-memory flag first — no file I/O needed after shutdown_server().
  if (!g_running) return 0;

  int fd = open(INSTANCE_FILE, O_RDONLY, 0);
  if (fd < 0) return 1; // file missing: assume active (edge case on first run)

  char buf[16] = {0};
  read(fd, buf, sizeof(buf) - 1);
  close(fd);

  uint32_t active_id = (uint32_t)atoi(buf);
  return (active_id == g_instance_id);
}

void update_instance_id(void) {
  // XOR time with a shifted boot counter to avoid collision when the payload
  // is reloaded twice within the same second (time(NULL) resolution = 1s).
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

void wakeup_previous_instance(void) {
  // A dummy connect unblocks sceNetAccept() in the old instance so it can
  // read the updated INSTANCE_FILE and exit cleanly.
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

void shutdown_server(void) {
  g_running = 0;

  // Write 0 so any thread still doing file-based checks also exits.
  int fd = open(INSTANCE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    write(fd, "0", 1);
    close(fd);
  }

  // Close the listening socket — this is the critical step.
  // It forces sceNetAccept() to return -1 immediately, unblocking the
  // server thread and preventing threads from outliving the process (KP fix).
  int sfd = g_server_fd;
  if (sfd >= 0) {
    g_server_fd = -1;
    sceNetSocketClose(sfd);
  }
}