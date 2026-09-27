#include "server.h"
#include "http.h"

#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BACKLOG 10

void run_server(const char *port) {
	struct addrinfo hints, *address_results;

	// Hints are used to tell getaddrinfo what kind of socket we want.
	// Think of them as address filters for the getaddrinfo function.
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;	 // IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM; // TCP
	// Use a wildcard address of the host machine
	hints.ai_flags = AI_PASSIVE; // (0.0.0.0 or ::)

	// getaddrinfo gets the address info. from the OS instead of hardcoding.
	// It returns a linked list of address results.
	// AF_UNSPEC gives us both IPv4 and IPv6 addresses (can mean >= 2 results).
	int status = getaddrinfo(NULL, port, &hints, &address_results);
	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(1);
	}

	// Try all address results and use the first socket we can bind.
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

		// Allow rebinding the port immediately after the server restarts.
		// Without this, the OS holds the port in TIME_WAIT and bind fails
		// with "Address already in use".
		int reuse = 1;
		int setsockopt_status = setsockopt(
			http_socket,
			SOL_SOCKET,
			SO_REUSEADDR,
			&reuse,
			sizeof(reuse)
		);
		if (setsockopt_status == -1) {
			perror("Failed to set socket options");
			close(http_socket);
			continue;
		}

		// Bind the socket to an address and port for it to be accessible by
		// other devices on the network.
		int bind_status = bind(
			http_socket,
			result->ai_addr,
			result->ai_addrlen
		);
		if (bind_status == -1) {
			perror("server: bind");
			close(http_socket);
			continue;
		}

		// Use the first result that successfully binds.
		break;
	}

	freeaddrinfo(address_results);

	// If no result successfully binds, exit with error.
	if (result == NULL) {
		fprintf(stderr, "Server: failed to bind\n");
		close(http_socket);
		exit(1);
	}

	// Start listening for incoming connections.
	int listen_status = listen(http_socket, BACKLOG);
	if (listen_status == -1) {
		perror("server: listen");
		close(http_socket);
		exit(1);
	}

	printf("Server listening on port %s...\n", port);

	while (true) {
		int client_socket = accept(http_socket, NULL, NULL);
		if (client_socket == -1) {
			perror("server: accept");
			continue;
		}

		// A buffer to store the request.
		char buffer[1024];

		// Read the request into the buffer.
		ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
		if (bytes_read == -1) {
			perror("Failed to read request");
			close(client_socket);
			continue;
		}

		// Null-terminate the buffer.
		buffer[bytes_read] = '\0';

		HttpRequest request;
		if (http_parse_request(buffer, &request) == -1) {
			const char *bad_request = "HTTP/1.1 400 Bad Request\r\n"
									  "Content-Length: 0\r\n"
									  "Connection: close\r\n"
									  "\r\n";
			send(client_socket, bad_request, strlen(bad_request), 0);
			close(client_socket);
			continue;
		}

		printf(
			"Parsed: method=%s path=%s version=%s\n",
			request.method,
			request.path,
			request.version
		);

		// Send the response to the client.
		const char *response = "HTTP/1.1 200 OK\r\n"
							   "Content-Length: 13\r\n"
							   "Connection: close\r\n"
							   "\r\n"
							   "Hello, World!";
		ssize_t bytes_sent = send(client_socket, response, strlen(response), 0);
		if (bytes_sent == -1) {
			perror("Failed to send response");
			close(client_socket);
			continue;
		}

		close(client_socket);
	}

	close(http_socket);
}
