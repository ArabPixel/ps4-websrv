#include "server.h"
#include "http.h"
#include "net.h"
#include "instance.h"

void *http_server_thread(void *arg) {
  UNUSED(arg);

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

    // Publish fd so shutdown_server() can close it from another thread,
    // which forces sceNetAccept() to return and the loop to exit cleanly.
    g_server_fd = server_fd;

    int fail_streak = 0;
    while (g_running && is_instance_active() && fail_streak < 5) {
      struct sockaddr_in client_addr;
      unsigned int client_len = sizeof(client_addr);
      int client_fd = sceNetAccept(server_fd,
                                   (struct sockaddr *)&client_addr,
                                   &client_len);
      if (client_fd < 0) {
        if (!g_running || !is_instance_active()) break;
        fail_streak++;
        sceKernelSleep(1);
        continue;
      }
      fail_streak = 0;

      set_socket_timeouts(client_fd);

      // Reject when at capacity rather than spawning unbounded threads
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
        // Thread creation failed — handle inline and undo the counter
        __sync_fetch_and_sub(&g_client_count, 1);
        handle_client(client_fd);
        sceNetSocketClose(client_fd);
      }
    }

    // Clear before closing to prevent a concurrent shutdown_server() double-close
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

void *network_monitor_thread(void *arg) {
  UNUSED(arg);

  char prev_ip[16] = {0}; // empty = no network
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
        if (SERVER_PORT == 80)
          printf_notification("Web server started!\nhttp://%s", curr_ip);
        else
          printf_notification("Web server started!\nhttp://%s:%d", curr_ip, SERVER_PORT);
        printf_debug("[PS4-Websrv] Web server started! http://%s", curr_ip);
        strcpy(prev_ip, curr_ip);
      }
    }

    sceKernelSleep(NET_POLL_SEC);
  }

  printf_debug("[PS4-Websrv] Instance %u: monitor thread exiting.\n", g_instance_id);
  return NULL;
}