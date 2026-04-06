#include "webroot.h"
#include "index.h"
#include "loader.h"

static void write_default_file(const char *path,
                                const unsigned char *data,
                                unsigned int len) {
  if (file_exists((char *)path)) return; // SDK takes char*, not const char*
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

void setup_webroot(void) {
  struct stat st;
  char path[PATH_MAX];

  if (stat(WEB_ROOT, &st) != 0 || !S_ISDIR(st.st_mode))
    mkdir_recursive(WEB_ROOT);

  snprintf(path, sizeof(path), "%s/index.html", WEB_ROOT);
  write_default_file(path, index_html, index_html_len);

  snprintf(path, sizeof(path), "%s/loader.html", WEB_ROOT);
  write_default_file(path, loader_html, loader_html_len);
}