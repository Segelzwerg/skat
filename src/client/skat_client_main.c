#include "conf.h"
#include "skat/client.h"
#include <errno.h>
#include <limits.h>
#include <skat/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
print_usage(const char *const name) {
  fprintf(stderr, "Usage: %s [-r] [-g] [-f] [-h host] [-p port] name\n", name);
}

int start_GRAPHICAL(int fullscreen);

int
main(int argc, char **argv) {
  int opt;
  char *remaining;
  char *host = "localhost";
  long port = DEFAULT_PORT;
  int resume = 0;
  int graphical = 0;
  int fullscreen = 0;

  while ((opt = getopt(argc, argv, "h:p:rgf")) != -1) {
	switch (opt) {
	  case 'r':
		resume = 1;
		break;
	  case 'g':
		graphical = 1;
		break;
	  case 'f':
		fullscreen = 1;
		break;
	  case 'h':
		host = argv[optind - 1];
		break;
	  case 'p':
		errno = 0;
		port = strtol(optarg, &remaining, 0);
		if (errno == 0 && *remaining == '\0' && port >= 0 && port < USHRT_MAX) {
		  break;
		}
		fprintf(stderr, "Invalid port: %s\n", optarg);
		if (errno != 0) {
		  perror("strtol");
		}

		__attribute__((fallthrough));
	  default:
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}
  }

  if (optind >= argc) {
	fprintf(stderr, "Expected name after options\n");
	print_usage(argv[0]);
	exit(EXIT_FAILURE);
  }

  printf("Options: port=%ld; host=%s; name=%s; resume=%d;\n", port, host,
		 argv[optind], resume);

  if (graphical) {
	// TODO: add graphical loop for render and skat logic
	DTODO_PRINTF("TODO: add graphical loop for render and skat logic");
	start_GRAPHICAL(fullscreen);
	exit(EXIT_SUCCESS);
  }

  client *c = malloc(sizeof(client));
  client_init(c, host, (int) port, argv[optind]);
  client_run(c, resume);
  __builtin_unreachable();
}
