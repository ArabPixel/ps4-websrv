#include "ps4.h"
#include "index.h" // built by xxid -i index.html > index.h in Makefile

#define SERVER_PORT 80
#define WEB_ROOT "/data/websrv"
#define BUFFER_SIZE 8192

const char *get_mime_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)                                          return "application/octet-stream";
  if (strcmp(ext, ".html") == 0)                     return "text/html";
  if (strcmp(ext, ".css")  == 0)                     return "text/css";
  if (strcmp(ext, ".js")   == 0 ||
      strcmp(ext, ".mjs")  == 0)                     return "text/javascript";
  if (strcmp(ext, ".json") == 0)                     return "application/json";
  if (strcmp(ext, ".wasm") == 0)                     return "application/wasm";
  if (strcmp(ext, ".png")  == 0)                     return "image/png";
  if (strcmp(ext, ".jpg")  == 0 ||
      strcmp(ext, ".jpeg") == 0)                     return "image/jpeg";
  if (strcmp(ext, ".ico")  == 0)                     return "image/x-icon";
  if (strcmp(ext, ".svg")  == 0)                     return "image/svg+xml";
  return "application/octet-stream";
}

void write_default_file(const char *path, const unsigned char *data, unsigned int len) {
  int fd = open(path, O_RDONLY, 0);
  if (fd >= 0) { close(fd); return; }
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return;
  write(fd, data, len);
  close(fd);
}

void mkdir_recursive(const char *path) {
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

void setup_webroot(void) {
  struct stat st;
  char index_path[PATH_MAX];

  if (stat(WEB_ROOT, &st) != 0 || !S_ISDIR(st.st_mode))
    mkdir_recursive(WEB_ROOT);

  snprintf(index_path, sizeof(index_path), "%s/index.html", WEB_ROOT);
  if (stat(index_path, &st) != 0)
    write_default_file(index_path, index_html, index_html_len);
}

void *http_server_thread(void *arg) {
  UNUSED(arg);
  struct sockaddr_in server_addr, client_addr;
  unsigned int client_len = sizeof(client_addr);
  int server_fd, client_fd, bytes_read;

  server_fd = sceNetSocket("http_server", AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) return NULL;

  int opt = 1;
  sceNetSetsockopt(server_fd, SOL_SOCKET, SCE_NET_SO_REUSEADDR, &opt, sizeof(opt));

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_len         = sizeof(server_addr);
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = IN_ADDR_ANY;
  server_addr.sin_port        = sceNetHtons(SERVER_PORT);

  if (sceNetBind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 ||
      sceNetListen(server_fd, 10) < 0) {
    sceNetSocketClose(server_fd);
    return NULL;
  }

  while (1) {
    client_fd = sceNetAccept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) continue;

    char buffer[BUFFER_SIZE];
    bytes_read = sceNetRecv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read > 0) {
      buffer[bytes_read] = '\0';

      if (strncmp(buffer, "GET ", 4) == 0) {
        char *path     = buffer + 4;
        char *end_path = strchr(path, ' ');

        if (end_path) {
          *end_path = '\0';

          char *query = strchr(path, '?');
          if (query) *query = '\0';

          char full_path[PATH_MAX];
          if (strcmp(path, "/") == 0)
            snprintf(full_path, sizeof(full_path), "%s/index.html", WEB_ROOT);
          else
            snprintf(full_path, sizeof(full_path), "%s%s", WEB_ROOT, path);

          int fd = open(full_path, O_RDONLY, 0);
          if (fd >= 0) {
            struct stat st;
            fstat(fd, &st);

            char response_header[512];
            snprintf(response_header, sizeof(response_header),
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %ld\r\n"
              "Cache-Control: no-store\r\n"
              "Connection: close\r\n"
              "\r\n",
              get_mime_type(full_path), (long)st.st_size);

            sceNetSend(client_fd, response_header, strlen(response_header), 0);
            while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0)
              sceNetSend(client_fd, buffer, bytes_read, 0);
            close(fd);
          } else {
            const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
            sceNetSend(client_fd, not_found, strlen(not_found), 0);
          }
        }
      }
    }
    sceNetSocketClose(client_fd);
  }
  return NULL;
}

int _main(struct thread *td) {
  UNUSED(td);
  initKernel();
  initLibc();
  initNetwork();
  initPthread();
  initSysUtil();
  jailbreak();

  setup_webroot();

  // Get local IP via UDP trick (no data is sent)
  char ip_str[16] = "unknown";
  int tmp_fd = sceNetSocket("ip_check", AF_INET, SOCK_DGRAM, 0);
  if (tmp_fd >= 0) {
    struct sockaddr_in remote, local;
    unsigned int addr_len = sizeof(local);
    memset(&remote, 0, sizeof(remote));
    remote.sin_family      = AF_INET;
    remote.sin_addr.s_addr = IP(8, 8, 8, 8);
    remote.sin_port        = sceNetHtons(53);
    sceNetConnect(tmp_fd, (struct sockaddr *)&remote, sizeof(remote));
    if (sceNetGetsockname(tmp_fd, (struct sockaddr *)&local, &addr_len) == 0)
      sceNetInetNtop(AF_INET, &local.sin_addr, ip_str, sizeof(ip_str));
    sceNetSocketClose(tmp_fd);
  }

  ScePthread thread;
  if (scePthreadCreate(&thread, NULL, http_server_thread, NULL, "http_server_thread") == 0)
  if (SERVER_PORT == 80){
    printf_notification("Web server started!\nhttp://%s", ip_str);
  }else {
    printf_notification("Web server started!\nhttp://%s:%d", ip_str, SERVER_PORT);
  }
  else
    printf_notification("Failed to start web server!");

  return 0;
}