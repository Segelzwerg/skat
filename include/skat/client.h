#pragma once

#include "skat/connection.h"
#include "skat/exec_async.h"
#include "skat/package.h"
#include "skat/skat.h"
#include "skat/util.h"
#include <pthread.h>

typedef struct client {
  pthread_mutex_t lock;
  pthread_t conn_thread;
  pthread_t exec_async_handler;
  pthread_t io_handler;
  async_callback_queue acq;
  connection_c2s c2s;
  int port;
  char *host;
  char *name;
  skat_client_state cs;
  player *pls[4];
} client;

void client_acquire_state_lock(client *c);
void client_release_state_lock(client *c);

void client_disconnect_connection(client *c, connection_c2s *conn);
void client_handle_resync(client *c, payload_resync *pl);
void client_notify_join(client *, payload_notify_join *);
void client_notify_leave(client *, payload_notify_leave *);

void client_init(client *c, char *host, int port, char *name);
_Noreturn void client_run(client *c, int resume);
