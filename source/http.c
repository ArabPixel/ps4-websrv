#include "http.h"
#include "net.h"
#include "instance.h"

void handle_client(int client_fd) {
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

  // Read msg= query param before stripping the query string.
  // Used by the /notify endpoint so the caller can pass a custom message.
  char notify_msg[128] = "Hello World";
  char *query = strchr(path, '?');
  if (query) {
    *query = '\0';
    char *msg_param = strstr(query + 1, "msg=");
    if (msg_param) {
      msg_param += 4;
      int i = 0;
      char *p = msg_param;
      while (*p && i < (int)sizeof(notify_msg) - 1) {
        if (*p == '+') {
          notify_msg[i++] = ' ';
          p++;
        } else if (*p == '%' && *(p + 1) && *(p + 2)) {
          char hex[3] = { *(p + 1), *(p + 2), '\0' };
          notify_msg[i++] = (char)strtol(hex, NULL, 16);
          p += 3;
        } else {
          notify_msg[i++] = *p++;
        }
      }
      notify_msg[i] = '\0';
    }
  }

  // Endpoints

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
    printf_notification("%s", notify_msg);
    printf_debug("[PS4-Websrv] Notify: %s\n", notify_msg);
    return;
  }

  // Static file serving

  char full_path[PATH_MAX];
  if (strcmp(path, "/") == 0)
    snprintf(full_path, sizeof(full_path), "%s/index.html", WEB_ROOT);
  else
    snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, path);

  // Path traversal guard
  if (strncmp(full_path, WEB_ROOT, strlen(WEB_ROOT)) != 0) {
    const char *forbidden =
      "HTTP/1.1 403 Forbidden\r\n"
      "Content-Length: 9\r\n"
      "Connection: close\r\n\r\n"
      "Forbidden";
    sceNetSend(client_fd, forbidden, strlen(forbidden), 0);
    return;
  }

  // Directory → index.html fallback
  struct stat st;
  if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    size_t pl = strlen(full_path);
    if (pl + 11 < PATH_MAX) {
      if (full_path[pl - 1] != '/')
        full_path[pl++] = '/';
      strcpy(full_path + pl, "index.html");
    }
  }

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

void *client_thread_entry(void *arg) {
  int client_fd = (int)(intptr_t)arg;
  handle_client(client_fd);
  sceNetSocketClose(client_fd);
  __sync_fetch_and_sub(&g_client_count, 1);
  return NULL;
}