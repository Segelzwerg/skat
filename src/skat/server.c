#include "skat/server.h"
#include "conf.h"
#include "skat/ctimer.h"
#include "skat/util.h"
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FOR_EACH_ACTIVE(s, var, block) \
  for (int var = 0; var < 4; var++) { \
	if (!server_is_player_active(s, var)) \
	  continue; \
	else \
	  block \
  }

int
server_is_player_active(server *s, int gupid) {
  return (s->playermask >> gupid) & 1;
}

int
server_has_player_name(server *s, player_name *pname) {
  for (int i = 0; i < 4; i++)// otherwise we can't recover connections
	if (s->ps[i] != NULL && player_name_equals(&s->ps[i]->name, pname))
	  return 1;
  return 0;
}

void
server_send_event(server *s, event *e, player *pl) {
  connection_s2c *c = server_get_connection_by_gupid(s, pl->index);
  if (c) {
	conn_enqueue_event(&c->c, e);
  }
}

player *
server_get_player_by_gupid(server *s, int gupid) {
  return s->ps[gupid];
}

void
server_distribute_event(server *s, event *ev,
						void (*mask_event)(event *, player *)) {
  event e;
  DEBUG_PRINTF("Distributing event of type %s", event_name_table[ev->type]);
  FOR_EACH_ACTIVE(s, i, {
	if (mask_event) {
	  e = *ev;
	  mask_event(&e, s->ps[i]);
	  server_send_event(s, &e, s->ps[i]);
	} else
	  server_send_event(s, ev, s->ps[i]);
  });
}

connection_s2c *
server_get_free_connection(server *s, int *n) {
  int pm, i;
  if (s->ncons > 4)
	return NULL;
  pm = ~s->playermask;
  i = __builtin_ctz(pm);
  s->playermask |= 1 << i;
  *n = i;
  return &s->conns[i];
}

void
server_add_player_for_connection(server *s, player *pl, int n) {
  s->ps[n] = pl;
  pl->index = n;
}

connection_s2c *
server_get_connection_by_pname(server *s, player_name *pname, int *n) {
  FOR_EACH_ACTIVE(s, i, {
	if (player_name_equals(&s->ps[i]->name, pname)) {
	  if (n)
		*n = i;
	  return &s->conns[i];
	}
  });
  return NULL;
}

connection_s2c *
server_get_connection_by_gupid(server *s, int gupid) {
  return &s->conns[gupid];
}

player *
server_get_player_by_pname(server *s, player_name *pname) {
  for (int i = 0; i < 4; i++)
	if (player_name_equals(&s->ps[i]->name, pname))
	  return s->ps[i];
  return NULL;
}

void
server_disconnect_connection(server *s, connection_s2c *c) {
  player *pl;
  pl = server_get_player_by_gupid(s, c->gupid);

  skat_state_notify_disconnect(&s->ss, pl, s);
  FOR_EACH_ACTIVE(s, i, {
	if (!player_equals_by_name(pl, s->ps[i]))
	  conn_notify_disconnect(&s->conns[i], pl);
  });
  conn_disable_conn(&c->c);
}

void
server_acquire_state_lock(server *s) {
  DPRINTF_COND(DEBUG_LOCK, "Acquiring server state lock from thread %d",
			   gettid());
  pthread_mutex_lock(&s->lock);
  DPRINTF_COND(DEBUG_LOCK, "Acquired server state lock from thread %d",
			   gettid());
}

void
server_release_state_lock(server *s) {
  DPRINTF_COND(DEBUG_LOCK, "Releasing server state lock from thread %d",
			   gettid());
  pthread_mutex_unlock(&s->lock);
  DPRINTF_COND(DEBUG_LOCK, "Released server state lock from thread %d",
			   gettid());
}

void
server_resync_player(server *s, player *pl, skat_client_state *cs) {
  DEBUG_PRINTF("Resync requested by player '%s'", pl->name.name);
  skat_resync_player(&s->ss, cs, pl);
}

void
server_tick(server *s) {
  DPRINTF_COND(DEBUG_TICK, "Server tick");

  server_acquire_state_lock(s);

  action a;
  event err_ev;
  FOR_EACH_ACTIVE(s, i, {
	if (!s->conns[i].c.active)
	  continue;

	while (conn_dequeue_action(&s->conns[i].c, &a)) {
	  if (!skat_server_state_apply(&s->ss, &a, s->ps[i], s)) {
		DEBUG_PRINTF("Received illegal action of type %s from player %s with "
					 "id %ld, rejecting",
					 action_name_table[a.type], s->ps[i]->name.name, a.id);
		err_ev.type = EVENT_ILLEGAL_ACTION;
		err_ev.answer_to = a.id;
		copy_player_name(&err_ev.player, &s->ps[i]->name);
		conn_enqueue_event(&s->conns[i].c, &err_ev);
	  }
	}
	skat_server_state_tick(&s->ss, s);
  });

  server_release_state_lock(s);
}

void
server_notify_join(server *s, int gupid) {
  player *pl = server_get_player_by_gupid(s, gupid);
  DEBUG_PRINTF("Player '%s' joined with gupid %d", pl->name.name, gupid);
  skat_state_notify_join(&s->ss, pl, s);
  FOR_EACH_ACTIVE(s, i, {
	if (i != gupid)
	  conn_notify_join(&s->conns[i], pl);
  });
}

typedef struct {
  server *s;
  int conn_fd;
} server_handler_args;

static void *
server_handler(void *args) {
  connection_s2c *conn;
  server_handler_args *hargs = args;
  DEBUG_PRINTF("Handler started, establishing connection with %d",
			   hargs->conn_fd);
  conn = establish_connection_server(hargs->s, hargs->conn_fd, pthread_self());
  if (!conn) {
	DEBUG_PRINTF("Establishing connection with %d failed", hargs->conn_fd);
	close(hargs->conn_fd);
	return NULL;
  }
  DEBUG_PRINTF("Connection with %d established, commencing normal operations",
			   hargs->conn_fd);
  for (;;) {
	if (!conn_handle_incoming_packages_server(hargs->s, conn)) {
	  DEBUG_PRINTF("Connection with %d closed", hargs->conn_fd);
	  return NULL;
	}
	conn_handle_events_server(conn);
  }
}

typedef struct {
  server *s;
  int socket_fd;
  struct sockaddr_in addr;
} server_listener_args;

_Noreturn static void *
server_listener(void *args) {
  server_listener_args *largs = args;
  server_handler_args *hargs;
  pthread_t h;
  size_t addrlen = sizeof(struct sockaddr_in);
  int conn_fd;
  long iMode = 0;
  for (;;) {
	listen(largs->socket_fd, 3);
	DEBUG_PRINTF("Listening for connections");
	conn_fd = accept(largs->socket_fd, (struct sockaddr *) &largs->addr,
					 (socklen_t *) &addrlen);
	ioctl(conn_fd, FIONBIO, &iMode);
	hargs = malloc(sizeof(server_handler_args));
	hargs->s = largs->s;
	hargs->conn_fd = conn_fd;
	DEBUG_PRINTF("Received connection %d", conn_fd);
	pthread_create(&h, NULL, server_handler, hargs);
  }
}

static void
server_start_conn_listener(server *s, int p) {
  server_listener_args *args;
  // int opt = 1;
  DEBUG_PRINTF("Starting connection listener");
  args = malloc(sizeof(server_listener_args));
  args->s = s;
  args->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  /*setsockopt(args->socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
			 sizeof(opt));*/
  args->addr.sin_family = AF_INET;
  args->addr.sin_addr.s_addr = INADDR_ANY;
  args->addr.sin_port = htons(p);
  if (bind(args->socket_fd, (struct sockaddr *) &args->addr, sizeof(args->addr))
	  == -1) {
	DERROR_PRINTF("bind: %s", strerror(errno));
	exit(EXIT_FAILURE);
  }
  pthread_create(&s->conn_listener, NULL, server_listener, args);
}

void
server_init(server *s, int port) {
  DEBUG_PRINTF("Initializing server on port '%d'", port);
  memset(s, '\0', sizeof(server));
  pthread_mutex_init(&s->lock, NULL);
  s->port = port;
  skat_state_init(&s->ss);
}

static void
server_tick_wrap(void *s) {
  server_tick(s);
}

_Noreturn void
server_run(server *s) {
  ctimer t;

  ctimer_create(&t, s, server_tick_wrap,
				(1000 * 1000 * 1000) / SERVER_REFRESH_RATE);// in Hz

  server_acquire_state_lock(s);
  server_start_conn_listener(s, s->port);
  server_release_state_lock(s);

  DEBUG_PRINTF("Running server");

  ctimer_run(&t);
  pause();
  DERROR_PRINTF("How did we get here? This is illegal");
  __builtin_unreachable();
}
