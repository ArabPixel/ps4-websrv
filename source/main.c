#include "globals.h"
#include "webroot.h"
#include "instance.h"
#include "server.h"

int _main(struct thread *td) {
  UNUSED(td);
  initKernel();
  initLibc();
  initNetwork();
  initPthread();
  initSysUtil();
  jailbreak();

  setup_webroot();          // create /data/websrv and write default files
  update_instance_id();     // register this instance before spawning threads
  wakeup_previous_instance(); // unblock any previous instance's accept()

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
    // Not fatal — server still runs, user just won't get IP notifications
    printf_notification("Network monitor failed to start.");
    printf_debug("[PS4-Websrv] Network monitor failed to start.");
  } else {
    scePthreadDetach(mon_thread);
  }

  return 0;
}