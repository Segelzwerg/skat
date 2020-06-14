// clang-format off
#ifndef ACTION_HDR
#define ACTION_HDR

#define _GNU_SOURCE

#include "skat/card.h"
#include <stdint.h>

#ifndef STRINGIFY
#define STRINGIFY_ #x
#define STRINGIFY(x) STRINGIFY_(x)
#endif

#ifdef ACTION_HDR_TO_STRING
  #undef ACTION_HDR_TABLE_BEGIN
  #undef FIRST_ACTION
  #undef ACTION
  #undef ACTION_HDR_TABLE_END
  #define ACTION_HDR_TABLE_BEGIN char *action_name_table[] = {
  #define FIRST_ACTION(x) ACTION(x)
  #define ACTION(x) [ACTION_ ## x] = "ACTION_" #x 
  #define ACTION_HDR_TABLE_END , (void *)0};
#else
  #define ACTION_HDR_TABLE_BEGIN typedef enum {
  #define FIRST_ACTION(x) ACTION_ ## x = 0
  #define ACTION(x) ACTION_ ## x
  #define ACTION_HDR_TABLE_END } action_type;
#endif


ACTION_HDR_TABLE_BEGIN
  FIRST_ACTION(INVALID),
  ACTION(READY),
  ACTION(RULE_CHANGE),
  ACTION(PLAY_CARD)
ACTION_HDR_TABLE_END


#ifndef ACTION_HDR_TO_STRING
; // to make clang-format happy
// clang-format on

typedef int64_t action_id;

typedef struct {
  action_type type;
  action_id id;
  union {
	card_id card;
  };
} action;

extern char *action_name_table[];

#endif
#endif
