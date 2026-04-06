#include "net.h"

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

int get_local_ip(char *ip_out) {
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

void set_socket_timeouts(int fd) {
  struct timeval tv;
  tv.tv_sec  = 5;
  tv.tv_usec = 0;
  sceNetSetsockopt(fd, SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &tv, sizeof(tv));
  sceNetSetsockopt(fd, SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &tv, sizeof(tv));
}