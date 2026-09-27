#include "server.h"

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void run_server(const char *port) {
	struct addrinfo hints, *address_results;

	// Hints are used to tell getaddrinfo what kind of socket we want.
	// Think of them as address filters for the getaddrinfo function.
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;	 // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE;	 // Use a wildcard address of the host machine (0.0.0.0 or ::)

	// getaddrinfo gets the address info. from the OS instead of hardcoding.
	// It returns a linked list of address results.
	// AF_UNSPEC gives us both IPv4 and IPv6 addresses (can mean >= 2 results).
	int status = getaddrinfo(NULL, port, &hints, &address_results);
	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(1);
	}

	// Try all address results and use the first socket we can create.
	struct addrinfo *result;
	int http_socket = -1;
	for (result = address_results; result != NULL; result = result->ai_next) {
		// socket() returns a file descriptor (fd).
		// An fd is an integer index to the process' open-file table.
		// Everything in Unix is a file. So fd can be a socket, pipe, file, etc.
		http_socket = socket(
			result->ai_family,	 // Address family (IPv4 or IPv6)
			result->ai_socktype, // Socket type: Stream
			result->ai_protocol	 // Protocol: TCP
		);
		if (http_socket == -1) {
			perror("server: socket");
			continue; // Try the next result
		}
		break; // Use the first result that works
	}

	if (result == NULL) {
		fprintf(stderr, "server: failed to create socket\n");
		freeaddrinfo(address_results);
		exit(1);
	}

	// Bind the socket to an address and port for it to be accessible by other devices on the network.
	int bind_status = bind(http_socket, result->ai_addr, result->ai_addrlen);
	if (bind_status == -1) {
		perror("server: bind");
		freeaddrinfo(address_results);
		close(http_socket);
		exit(1);
	}

	//  Start listening for incoming connections.
	int listen_status = listen(http_socket, 10);
	if (listen_status == -1) {
		perror("server: listen");
		freeaddrinfo(address_results);
		close(http_socket);
		exit(1);
	}

	printf("Server listening on port %s...\n", port);

	freeaddrinfo(address_results);
	close(http_socket);
}
