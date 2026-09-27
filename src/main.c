#include "server.h"

#include <stdio.h>

int main(
	int argc,	 // Number of arguments. Always at least 1.
	char *argv[] // Arguments. argv[0] is the program name.
) {
	// If more than one argument is provided, print usage and exit.
	if (argc > 2) {
		fprintf(stderr, "Usage: %s [port]\n", argv[0]);
		return 1;
	}

	char *port = (argc == 2) ? argv[1] : "8080";

	run_server(port);

	return 0;
}
