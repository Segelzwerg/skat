#pragma once

#include "action.h"
#include "card.h"
#include "card_collection.h"
#include "player.h"

typedef enum {
  EVENT_INVALID = 0,
  EVENT_ILLEGAL_ACTION,
  EVENT_SUSPEND_GAME,
  EVENT_START_GAME,
  EVENT_START_ROUND,
  EVENT_PLAYER_READY,
  EVENT_DISTRIBUTE_CARDS,
  EVENT_PLAY_CARD,
  EVENT_STICH_DONE
} event_type;

typedef struct {
  event_type type;
  action_id answer_to;
  player_id player;
  union {
	player_id ready_player;
	player_id current_active_players[3];
	card_collection hand;
	card_id card;
	player_id stich_winner;
	struct {
	  int score_round[3];
	  int total_score[3];
	}; 
  };
} event;
