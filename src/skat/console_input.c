#include "skat/console_input.h"
#include "skat/card.h"
#include "skat/card_collection.h"
#include "skat/client.h"
#include "skat/console_command.h"
#include "skat/util.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void
print_card_array(card_id *arr, size_t length) {
  char buf[4];

  for (size_t i = 0; i < length; i++) {
	card_get_name(&arr[i], buf);
	printf(" %s", buf);
  }
}

static void
print_card_collection(card_collection *cc) {
  char buf[4];
  int result;
  int ix = 0;
  for (card_id cur = 0; cur < 32; cur++) {
	card_collection_contains(cc, &cur, &result);
	if (result) {
	  card_get_name(&cur, buf);
	  printf(" %s(%d)", buf, ix++);
	}
  }
}

typedef enum {
  PRINT_PLAYER_TURN_SHOW_HAND_MODE_ALWAYS,
  PRINT_PLAYER_TURN_SHOW_HAND_MODE_DEFAULT,
  PRINT_PLAYER_TURN_SHOW_HAND_MODE_NEVER
} print_player_turn_show_hand_mode;

static void
print_player_turn(client *c, print_player_turn_show_hand_mode mode) {
  int player_turn =
		  c->cs.sgs.active_players[(c->cs.sgs.curr_stich.vorhand
									+ c->cs.sgs.curr_stich.played_cards)
								   % 3];
  if (c->cs.my_index == player_turn)
	printf("It is YOUR turn.");
  else
	printf("It is %s's turn.", c->pls[player_turn]->name.name);

  if ((c->cs.my_index == player_turn
	   && mode != PRINT_PLAYER_TURN_SHOW_HAND_MODE_NEVER)
	  || mode == PRINT_PLAYER_TURN_SHOW_HAND_MODE_ALWAYS) {
	printf(" Your cards:");
	print_card_collection(&c->cs.my_hand);
  }
}

static void
print_info_exec(void *p) {
  client *c = p;

  client_acquire_state_lock(c);

  game_phase phase = c->cs.sgs.cgphase;
  stich *last_stich = &c->cs.sgs.last_stich;
  stich *stich = &c->cs.sgs.curr_stich;
  card_collection *hand = &c->cs.my_hand;
  card_collection *won_stiche = &c->cs.my_stiche;

  printf("--\n\n--------------------------\n");

  printf("You are %s[gupid=%d, active_player=%d]\n",
		 c->pls[c->cs.my_index]->name.name, c->cs.my_index,
		 c->cs.my_active_player_index);

  printf("You are playing with:");
  for (int i = 0; i < 4; i++) {
	player *pl = c->pls[i];
	if (pl == NULL)
	  continue;

	int ap = -1;
	for (int j = 0; j < 3; j++) {
	  if (c->cs.sgs.active_players[j] == pl->index) {
		ap = j;
		break;
	  }
	}
	printf(" %s[gupid=%d, active_player=%d]", pl->name.name, pl->index, ap);
  }
  printf("\n");

  printf("Game Phase: %s, Type: %d, Trumpf: %d\n", game_phase_name_table[phase],
		 c->cs.sgs.gr.type, c->cs.sgs.gr.trumpf);

  if (phase != GAME_PHASE_INVALID && phase != GAME_PHASE_SETUP
	  && phase != GAME_PHASE_BETWEEN_ROUNDS) {
	if (c->cs.ist_alleinspieler) {
	  printf("You are playing alone, the skat was:");
	  print_card_array(c->cs.skat, 2);
	  printf("\n");
	} else {
	  printf("You are playing with %s\n",
			 c->pls[c->cs.sgs.active_players[c->cs.my_partner]]->name.name);
	}

	if (c->cs.sgs.stich_num > 0) {
	  printf("Last Stich (num=%d, vorhand=%s, winner=%s):", c->cs.sgs.stich_num,
			 c->pls[c->cs.sgs.active_players[last_stich->vorhand]]->name.name,
			 c->pls[c->cs.sgs.active_players[last_stich->winner]]->name.name);
	  print_card_array(last_stich->cs, last_stich->played_cards);
	  printf("\n");
	}
	printf("Current Stich (num=%d, vorhand=%s):", c->cs.sgs.stich_num,
		   c->pls[c->cs.sgs.active_players[stich->vorhand]]->name.name);
	print_card_array(stich->cs, stich->played_cards);
	printf("\n");

	print_player_turn(c, PRINT_PLAYER_TURN_SHOW_HAND_MODE_NEVER);
	printf("\n");

	printf("Your hand:");
	print_card_collection(hand);
	printf("\n");

	printf("Your stiche:");
	print_card_collection(won_stiche);
	printf("\n");
  }

  printf("--------------------------\n\n> ");
  fflush(stdout);

  client_release_state_lock(c);
}

typedef struct {
  client_action_callback_hdr hdr;
} client_ready_callback_args;

static void
client_ready_callback(void *v) {
  client_ready_callback_args *args = v;
  if (args->hdr.e.type == EVENT_ILLEGAL_ACTION)
	printf("--\nBig unluck! You tried to ready yourself, but that was "
		   "illegal\n> ");
  else
	printf("--\nReady for battle. We are now in %s\n> ",
		   game_phase_name_table[args->hdr.c->cs.sgs.cgphase]);
  fflush(stdout);
  free(args);
}

static void
execute_ready_wrapper(void *v) {
  client *c = v;
  client_action_callback cac;
  client_ready_callback_args *cach = malloc(sizeof(client_ready_callback_args));
  cac.args = cach;
  cac.f = client_ready_callback;
  client_ready(c, &cac);
}

static void
execute_ready(client *c) {
  async_callback acb;

  acb = (async_callback){.do_stuff = execute_ready_wrapper, .data = c};

  exec_async(&c->acq, &acb);
}

struct client_play_card_args {
  client *c;
  unsigned int card_index;
};

typedef struct {
  client_action_callback_hdr hdr;
} client_play_card_callback_args;

static void
client_play_card_callback(void *v) {
  __label__ end;
  client_play_card_callback_args *args = v;

  client_acquire_state_lock(args->hdr.c);

  printf("--\n");

  if (args->hdr.e.type == EVENT_ILLEGAL_ACTION) {
	printf("Big unluck! You tried to play a card, but it -sadly- was the wrong "
		   "card");
	goto end;
  }

  char buf[4];
  card_get_name(&args->hdr.e.card, buf);
  printf("Successfully played card %s. Cards currently on table:", buf);
  if (args->hdr.c->cs.sgs.curr_stich.played_cards > 0)
	print_card_array(args->hdr.c->cs.sgs.curr_stich.cs,
					 args->hdr.c->cs.sgs.curr_stich.played_cards);
  else
	print_card_array(args->hdr.c->cs.sgs.last_stich.cs,
					 args->hdr.c->cs.sgs.last_stich.played_cards);

end:
  printf("\n> ");
  fflush(stdout);
  client_release_state_lock(args->hdr.c);
  free(args);
}

static void
client_play_card_wrapper(void *v) {
  struct client_play_card_args *args = v;
  client_action_callback cac;

  client_play_card_callback_args *cach =
		  malloc(sizeof(client_play_card_callback_args));
  cac.args = cach;
  cac.f = client_play_card_callback;
  client_play_card(args->c, args->card_index, &cac);
  free(args);
}

static void
execute_print_info(client *c) {
  async_callback acb;

  acb = (async_callback){.do_stuff = print_info_exec, .data = c};

  exec_async(&c->acq, &acb);
}

static void
execute_play_card(client *c, unsigned int card_index) {
  async_callback acb;

  struct client_play_card_args *args =
		  malloc(sizeof(struct client_play_card_args));
  args->c = c;
  args->card_index = card_index;

  acb = (async_callback){.do_stuff = client_play_card_wrapper, .data = args};

  exec_async(&c->acq, &acb);
}

void
io_handle_event(client *c, event *e) {
  char buf[4];

  printf("--\n");
  switch (e->type) {
	case EVENT_DISTRIBUTE_CARDS:
	  print_player_turn(c, PRINT_PLAYER_TURN_SHOW_HAND_MODE_ALWAYS);
	  break;
	case EVENT_TEMP_REIZEN_DONE:
	  if (c->cs.ist_alleinspieler) {
		printf("You are playing alone, the skat was:");
		print_card_array(c->cs.skat, 2);
	  } else
		printf("You are playing with %s",
			   c->pls[c->cs.sgs.active_players[c->cs.my_partner]]->name.name);
	  break;
	case EVENT_PLAY_CARD:
	  card_get_name(&e->card, buf);
	  printf("Card %s played. Cards currently on table:", buf);
	  if (c->cs.sgs.curr_stich.played_cards > 0) {
		print_card_array(c->cs.sgs.curr_stich.cs,
						 c->cs.sgs.curr_stich.played_cards);
		printf("\n");
		print_player_turn(c, PRINT_PLAYER_TURN_SHOW_HAND_MODE_DEFAULT);
	  } else
		print_card_array(c->cs.sgs.last_stich.cs,
						 c->cs.sgs.last_stich.played_cards);
	  break;
	case EVENT_STICH_DONE:
	  if (e->stich_winner == c->cs.my_index) {
		printf("You won the Stich! \\o/");
	  } else if (!c->cs.ist_alleinspieler
				 && e->stich_winner
							== c->cs.sgs.active_players[c->cs.my_partner]) {
		printf("Your partner won the Stich! \\o/");
	  } else {
		printf("You lost the Stich. Gid good.");
	  }
	  printf("\n");
	  print_player_turn(c, PRINT_PLAYER_TURN_SHOW_HAND_MODE_DEFAULT);
	  break;
	default:
	  printf("Something (%s) happened", event_name_table[e->type]);
  }
  goto skip;

skip:
  printf("\n> ");
  fflush(stdout);
}

static void
stop_client(client *c) {
  client_acquire_state_lock(c);
  client_prepare_exit(c);
  client_release_state_lock(c);
  exit(EXIT_SUCCESS);
}

#define MATCH_COMMAND(c, s) if (!strncmp(c, s, sizeof(s)))

void *
handle_console_input(void *v) {
  client *c = v;
  char *line = NULL;
  size_t line_size = 0;

  DEBUG_PRINTF("Started console input thread");

  for (;;) {
	printf("> ");
	fflush(stdout);

	// getline uses realloc on the given buffer, thus free is not required
	ssize_t read = getline(&line, &line_size, stdin);

	if (read == EOF) {
	  printf("Read EOF\n");
	  fflush(stdout);
	  break;
	}
	printf("Read: %s", line);

	console_command *cmd = console_command_create(line, read);
	if (cmd == NULL) {
	  printf("Invalid command: %s", line);
	  continue;
	}

	printf("command: %s\n", cmd->command);
	printf("%zu args:\n", cmd->args_length);
	for (size_t i = 0; i < cmd->args_length; i++)
	  printf("  %zu: %s\n", i, cmd->args[i]);

	// ready
	MATCH_COMMAND(cmd->command, "ready") { execute_ready(c); }

	// play <card index>
	else MATCH_COMMAND(cmd->command, "play") {
	  if (cmd->args_length != 1)
		printf("Invalid number of args for play: got %zu, but expected 1\n",
			   cmd->args_length);
	  else if (cmd->args[0][0] == '\0')
		printf("Invalid arg, got NULL\n");
	  else {
		errno = 0;
		char *end;
		unsigned int card_index =
				(unsigned int) strtoul(cmd->args[0], &end, 10);

		if (errno != 0)
		  printf("Unable to parse arg '%s': %s\n", cmd->args[0],
				 strerror(errno));
		else if (end[0] != '\0')
		  printf("Unable to parse arg '%s' fully, still left to parse: '%s'\n",
				 cmd->args[0], end);
		else
		  execute_play_card(c, card_index);
	  }
	}

	// info
	else MATCH_COMMAND(cmd->command, "info") {
	  execute_print_info(c);
	}

	// unknown command
	else {
	  printf("Invalid command: %s", cmd->command);
	}

	console_command_free(cmd);
  }

  if (line)
	free(line);

  stop_client(c);

  __builtin_unreachable();
}
